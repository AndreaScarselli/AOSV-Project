#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>	
#include <linux/pid.h>		/* For pid types */
#include <linux/tty.h>		/* For the tty declarations */
#include <linux/version.h>	/* For LINUX_VERSION_CODE */
#include <linux/slab.h> //KMALLOC
#include <linux/delay.h>
#include <linux/wait.h>

////
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Scarselli");

typedef struct node{
	int size;
	struct node* next;
	char* payload;
} node;


static int multimode_open(struct inode *, struct file *);
static int multimode_release(struct inode *, struct file *);
static ssize_t multimode_write(struct file *, const char *, size_t, loff_t *);
static long multimode_ctl (struct file *filp, unsigned int param1, unsigned long param2);
static ssize_t multimode_read (struct file *filp, char *buff, size_t len, loff_t *off);
static ssize_t stream_read (struct file *filp, char *buff, size_t len);
static ssize_t package_read (struct file *filp, char *buff, size_t len);
static void purge(int current_device);


#define DEVICE_NAME "multimode"  
#define CURRENT_DEVICE iminor(filp->f_path.dentry->d_inode)
#define MAX_STORAGE (1<<20) //1MB of max storage ///UPPER LIMIT
#define MAX_SEGMENT_SIZE (1<<10) //1KB of max segment size (both for pkt and stream) ///UPPER LIMIT
#define MAX_INSTANCES (256) //supports 256 different instances
//WHAT IS TUNABLE
#define ACTUAL_MAXIMUM_SIZE_CTL 0
#define ACTUAL_MODE_CTL 1
#define ACTUAL_BLOCKING_MODE_CTL 6 
#define ACTUAL_MAXIMUM_SEGMENT_SIZE_CTL 3
#define PURGE_OPTION_CTL 9
//ACTUAL_BLOCKING_CTL options
#define BLOCKING_MODE 0
#define NON_BLOCKING_MODE 1
//ACTUAL_MODE_CTL options
#define STREAMING_MODE 0
#define PACKET_MODE 1
//PURGE PURGE_OPTION_CTL options
#define PURGE 1
#define NO_PURGE 0
//GET
#define GET_MAX_SEGMENT_SIZE 7
#define GET_FREE_SIZE 8
///



static int Major;           
static node* head[MAX_INSTANCES];
static node* tail[MAX_INSTANCES];
static int used[MAX_INSTANCES];
static int current_mode[MAX_INSTANCES];
static int current_blocking_mode[MAX_INSTANCES];
static int actual_maximum_size[MAX_INSTANCES]; 
static int actual_maximum_segment_size[MAX_INSTANCES];
static int actual_purge_option[MAX_INSTANCES];
static spinlock_t lock[MAX_INSTANCES];
static wait_queue_head_t wait_queues[MAX_INSTANCES];

static int multimode_open(struct inode *inode, struct file *filp){
	try_module_get(THIS_MODULE);
	return 0;
}

static int multimode_release(struct inode *inode, struct file *file){
    module_put(THIS_MODULE);
	return 0;

}


//IOCTL
static long multimode_ctl (struct file *filp, unsigned int cmd, unsigned long arg){
	
	///CRITICAL SECTION
	//printk("MULTIMODE: ioctl. cmd is %d arg is %ld\n");
	int current_device = CURRENT_DEVICE;
    int to_ret;
	if(current_blocking_mode[current_device]==NON_BLOCKING_MODE && (spin_trylock(&lock[CURRENT_DEVICE]))==0)
		//LOCK FAILED
		return -1;
	else if(current_blocking_mode[current_device]==BLOCKING_MODE)
		spin_lock(&lock[CURRENT_DEVICE]);
	
	switch(cmd){
		case ACTUAL_MODE_CTL: //STREAMING OR PACKET
			printk("MULTINODE: modifica in corso dello strm/pkt\n");
			current_mode[current_device] = arg;
            spin_unlock(&lock[current_device]);
			break;
		case ACTUAL_BLOCKING_MODE_CTL: //BLOCKING OR NON BLOCKING
			printk("MULTINODE: modifica in corso del blocking/non blocking\n");
			current_blocking_mode[current_device] = arg;
            spin_unlock(&lock[current_device]);
			break;
		case ACTUAL_MAXIMUM_SIZE_CTL:
			printk("MULTINODE: modifica in corso della maximum size\n");
			if(arg > 0 && arg<=MAX_STORAGE){ ///absolute upper limit
				actual_maximum_size[current_device] = arg;
                if(actual_purge_option[current_device]==PURGE){
                    purge(current_device);
                    ///L'UNLOCK QUI LO FA PURGE
                }
                else
                    spin_unlock(&lock[current_device]);
                printk("MULTIMODE: ACTUAL_MAXIMUM_SIZE is %d\n", actual_maximum_size[current_device]);
            }
			else{
				spin_unlock(&lock[current_device]);
				return -EINVAL;
			}
			break;
		case ACTUAL_MAXIMUM_SEGMENT_SIZE_CTL:
			printk("MULTINODE: modifica in corso della maximum segment size\n");
			if(arg<=MAX_SEGMENT_SIZE){ ///absoloute upper limit
				actual_maximum_segment_size[current_device] = arg;
                spin_unlock(&lock[current_device]);
            }
			else{
				spin_unlock(&lock[current_device]);
				return -EINVAL;
			}
			break;
		case GET_MAX_SEGMENT_SIZE:
            to_ret = actual_maximum_segment_size[current_device];
			spin_unlock(&lock[current_device]);
			return to_ret;
		case GET_FREE_SIZE:
            to_ret = actual_maximum_size[current_device] - used[current_device];
			spin_unlock(&lock[current_device]);
			return to_ret;
        case PURGE_OPTION_CTL:
            printk("MULTIMODE DEBUG: actual purge cmd\n");
            actual_purge_option[current_device] = arg;
            if(actual_purge_option[current_device]==PURGE) //magari avevo sforato e l'opzione era NO_PURGE.. ora PURGE!
                purge(current_device); ///L'unlock qui lo fa purge
            else
                spin_unlock(&lock[current_device]);
            break;
		default:
			printk("MULIMODE: DEFAULT\n");
			spin_unlock(&lock[current_device]);
			return -ENOTTY;
	}
	return 0;
}



///RITORNA LA DIMENSIONE DI QUELLO CHE È STATO COPIATO
///TUTTE EFFETTUANO IL LOCK (BLOCKING O NON BLOCKING NON IMPORTA)
///LE READ CHE RIESCONO A LEGGERE QELLO CHE VOGLIONO PASSANO
//QUELLE CHE NON CI RIESCONO: SI ADDORMENTANO SE SONO BLOCKING, FALLISCONO SE SONO NON BLOCKING
static ssize_t multimode_read (struct file *filp, char *buff, size_t len, loff_t *off){
	int ret = 0;
    int current_device = CURRENT_DEVICE;
	
    ///GLI UNLOCK VANNO GESTITI SULLE FUNZIONI
    spin_lock(&lock[CURRENT_DEVICE]);
    
     //NON CI SONO SCRITTE ABBASTANZE COSE
    while(len>used[current_device]){
        if(current_blocking_mode[current_device]==NON_BLOCKING_MODE){
            printk("sto qua con len=%d, used=%d\n", len, used[current_device]);
            spin_unlock(&lock[current_device]);
            return -1;
        }
        printk("sto qua ma non mi sono fermato nell'if\n");
        spin_unlock(&lock[current_device]);
        //wait_event_interruptible(queue, condition)
        //anche se questa mi controlla la condizione, io dopo la devo comunque ricontrollare col lock acquisito
        if(wait_event_interruptible(wait_queues[current_device], len<=used[current_device])!=0){ //interrotto da un segnale
            printk("Interrotto da Segnale\n");
            return -ERESTARTSYS;
        }
        spin_lock(&lock[current_device]);
    }
		
	//A QUESTO PUNTO IN ENTRAMBI I CASI HO PRESO IL LOCK
	if(current_mode[current_device]==STREAMING_MODE)
		ret = stream_read(filp,buff,len);
	else
		ret = package_read(filp,buff,len);
	
	return ret;
}

static ssize_t stream_read (struct file *filp, char *buff, size_t len){
	int calling_device = CURRENT_DEVICE;
	int offset = 0;
	int ok; //to return, len viene decrementato in lettura
	char ker_buf[len];	
	node* to_die_list = NULL;
	node* my_new_head = NULL; ///INSERISCO UN FERMAPOSTO FINCHÈ DEVO LIBERARE. SE QUALCUNO POI VA AVANTI SE LO LIBERA LUI
	ok=len;
	
	while(head[calling_device]!=NULL && len>=head[calling_device]->size){ 
		if(to_die_list==NULL)
			to_die_list = head[calling_device];
		memcpy(ker_buf + offset, head[calling_device]->payload, head[calling_device]->size);
		len -= head[calling_device]->size;
		offset += head[calling_device]->size;
		used[calling_device]-= head[calling_device]->size;
        if(tail[calling_device]==head[calling_device])
			tail[calling_device]=NULL;
		head[calling_device] = head[calling_device]->next;
	}
	
	if(head[calling_device]!= NULL && len<head[calling_device]->size && len!=0){
		
		int new_size = head[calling_device]->size - len;
		
		///CHE UTILIZZO IL BLOCCO GIÀ ALLOCATO FIN QUI (CHE A QUESTO PUNTO SARÀ SOVRADIMENSIONATO)
		///MA NON DO FASTIDIO AL BUDDY
		memcpy(ker_buf + offset, head[calling_device]->payload, len);
		memcpy(head[calling_device]->payload, (head[calling_device]->payload) + len, new_size);
		head[calling_device]->size = new_size;
		used[calling_device]-= len;

	}
	
	my_new_head = head[calling_device];

	
	spin_unlock(&lock[CURRENT_DEVICE]);
    
    ///QUELLI CHE DORMIVANO PER LA WRITE SENZA SPAZIO DEVONO ESSERE SVEGLIATI.. MAGARI ORA CE LA FANNo
    wake_up(&wait_queues[calling_device]);
	
    copy_to_user(buff,ker_buf,ok);
    
	//ORA FACCIO LE FREE FUORI DALLA SEZIONE CRITICA
	while(to_die_list!=NULL && to_die_list != my_new_head){
		node* to_die = to_die_list;
		to_die_list = to_die_list->next;
		kfree(to_die->payload);
		kfree(to_die);
	}
		
	return ok;
}

static ssize_t package_read (struct file *filp, char *buff, size_t len){
	///QUESTO FUNZIONA CHE SE MI CHIEDI 11, IL PRIMO È GRANDE 10 ED IL SECONDO 3
	///TI DO I DIECI DEL PRIMO, 1 DEL SECONDO E BUTTO I RESTANTI DUE
	int calling_device = CURRENT_DEVICE;
	char ker_buf[len];
    int done = 0;
	int offset = 0; //buono anche per tener traccia di quanti byte letti
	node* to_die_list = NULL;
    node* last_head = NULL; //last head to del
	node* to_die = NULL;
    
	while(head[calling_device]!= NULL && len!=0){
		//LEGGO LEN SE IL PACCHETTO È PIÙ GRANDE DI QUELLO CHE VOGLIO
		//LEGGERE, ALTRIMENTI LEGGO LA DIMENSIONE DEL PACCHETTO
		int to_read = len>head[calling_device]->size? head[calling_device]->size : len;
        if(to_die_list==NULL)
            to_die_list = head[calling_device];
            
        last_head = head[calling_device];
		
		memcpy(ker_buf + offset, head[calling_device]->payload, to_read);
		
		offset+=to_read;
		len-=to_read;
		
		//IN OGNI CASO SCARTO TUTTO IL PACCHETTO E LA TESTA DIVENTA LA SUCCESSIVA
		used[calling_device]-= head[calling_device]->size;
		head[calling_device] = head[calling_device]->next;
		//SE LA NUOVA TESTA È NULL, LA CODA È NULL
		if(head[calling_device]==NULL)
			tail[calling_device] = NULL;
		
	}
	
	spin_unlock(&lock[calling_device]);
    
    ///QUELLI CHE DORMIVANO PER LA WRITE SENZA SPAZIO DEVONO ESSERE SVEGLIATI.. MAGARI ORA CE LA FANNo
    wake_up(&wait_queues[calling_device]);
    
    copy_to_user(buff,ker_buf,offset);
	
    while(to_die_list!=NULL && !done){
        if(to_die_list==last_head)
            done=1;
        to_die = to_die_list;
        to_die_list = to_die_list->next;
        kfree(to_die->payload);
        kfree(to_die);
    }
	
	return offset;
}


///LO SPIN LOCK QUINDI NON HA NULLA A CHE FARE CON BLOCKING O NON BLOCKING. TUTTI DEVO FARE SPIN_LOCK
///RITORNA IL NUMERO DI BYTE EFFETTIVAMENTE SCRITTI
///RICORDA CHE QUA PKT O STREAM NON IMPORTA
///SE SONO IN NO BLOCKING E NON C'HO SPAZIO RITORNO -1
///SE SONO IN BLOCKING E NON C'È SPAZIO MUOIO SULLA WAIT QUEUE
///SE FAI UNA SCRITTURA MAGGIORE DEL MAX SEGMENT SIZE RITORNO LA MAXIMUM SEGMENT SIZE
static ssize_t multimode_write(struct file *filp, const char *buff, size_t len, loff_t *off){
	int calling_device = CURRENT_DEVICE;
	node *new;
    spin_lock(&lock[calling_device]);
    
	///QUESTO DEVE ESSERE FATTO NECESSARIAMENTE GIA IN SEZIONE CRITICA. Le grandezze massime
	///POTREBBERO VARIARE ED INOLTRE IL BUFFER SI POTREBBE RIEMPIRE..
    
    //CHECK se il pacchetto è più grande della dimensione max, in questo caso fallisco
	if(len > actual_maximum_segment_size[calling_device]){
		printk("Message cancelled. len=%zu, actual_maximum_size=%d \n", len, actual_maximum_segment_size[calling_device]);
		spin_unlock(&lock[calling_device]);
        return actual_maximum_segment_size[calling_device];
	}
	
    //È OCCUPATO PIÙ DEL DISPONIBILE (MAX SIZE CAMBIATA) oppure non c'è spazio
    while((actual_maximum_size[calling_device])-used[calling_device] <= 0 || len>(actual_maximum_size[calling_device])-used[calling_device] ){
        if(current_blocking_mode[calling_device]==NON_BLOCKING_MODE){
            spin_unlock(&lock[calling_device]);
            return -1;
        }
        spin_unlock(&lock[calling_device]);
        int res = wait_event_interruptible(wait_queues[calling_device], len<=(actual_maximum_size[calling_device]-used[calling_device]));
        if(res!=0){
            printk("Interrotto da segnale \n");
            return -ERESTARTSYS;
        }
        else
            printk("res is %d\n", res);
        spin_lock(&lock[calling_device]);
    }
		
	//ALLOCAZIONE E RESET
	new = kmalloc(sizeof(node),0);
	memset(new,0,sizeof(node));
	new->payload = kmalloc(len,0);
	memset(new->payload,0,len);
	
	//COPIARE IL BUFFER NEL NODO
	copy_from_user (new->payload,buff,len);
	
	new->size = len;
	new->next = NULL;
	if(head[calling_device]==NULL){ 
		head[calling_device] = new;
		tail[calling_device] = new;
	}
	else{
		tail[calling_device]->next = new; 
		tail[calling_device] = new;
	}
	
	used[calling_device] += len;

	
	spin_unlock(&lock[calling_device]);
    
    wake_up(&wait_queues[calling_device]);
   
	return len;
}

///QUESTO DEVE FARE L'UNLOCK DELL'IOCTL. DEVE FARLO LUI PERCHE LUI DEVE FARE LE FREE
///FUORI DALLA SEZIONE CRITICA
static void purge(int current_device){
    //QUESTO SCARTA SEMPRE IL PACCHETTO INTERO (POTREBBE INAVVERTITAMENTE CORROMPERE PACCHETTI ALTRIMENTI)
    node* to_die_list = NULL;
    node* last_head = NULL; //ULTIMA DA CANCELLARE
    node* to_die = NULL;
    int done = 0;
    while(used[current_device] > actual_maximum_size[current_device]){
        if(to_die_list==NULL)
            to_die_list = head[current_device];
        last_head = head[current_device];
        used[current_device] -= head[current_device]->size;
        head[current_device] = head[current_device]->next;
        if(head[current_device]==NULL)
            tail[current_device] = NULL;
    }
    
    spin_unlock(&lock[current_device]);
    
    while(to_die_list!=NULL && !done){
        if(to_die_list==last_head)
            done=1;
        to_die = to_die_list;
        to_die_list = to_die_list->next;
        kfree(to_die->payload);
        kfree(to_die);
    }
}

static struct file_operations fops = {
  .owner = THIS_MODULE, //This field is used to prevent the module from being unloaded while its operations are in use
  .write = multimode_write,
  .open =  multimode_open,
  .release = multimode_release,
  .read = multimode_read,
  .unlocked_ioctl = multimode_ctl
};

int init_module(void){
	int i;

	Major = register_chrdev(0, DEVICE_NAME, &fops);
	
	for(i=0;i<MAX_INSTANCES;i++){
		actual_maximum_size[i] = (MAX_STORAGE) / 2;
		actual_maximum_segment_size[i] = (MAX_SEGMENT_SIZE) / (2);
        actual_purge_option[i] = NO_PURGE;
        current_blocking_mode[i] = BLOCKING_MODE;
        current_mode[i] = STREAMING_MODE;
		spin_lock_init(&lock[i]);
        init_waitqueue_head(&wait_queues[i]);
	}

	if (Major < 0) {
	  printk("Registering noiser device failed\n");
	  return Major;
	}

	printk(KERN_INFO "multimode device registered, it is assigned major number %d\n", Major);


	return 0;
}

void cleanup_module(void){
	int i = 0;
	
	for(i=0;i<MAX_INSTANCES;i++){
		if(head[i]!=NULL){
			while(head[i]!=NULL){
				
				node* to_die=head[i];
				head[i]=head[i]->next;
				kfree(to_die->payload);
				kfree(to_die);
			}
		}
	}
	
	unregister_chrdev(Major, DEVICE_NAME);

	printk(KERN_INFO "multimode device unregistered, it was assigned major number %d\n", Major);
}



