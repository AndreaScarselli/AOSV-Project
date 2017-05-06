#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio_ext.h>
//
int selected;
#define MAX_SEGMENT_SIZE (1<<10) //1KB of max segment size (both for pkt and stream) //UPPER BOUND
//WHAT IS TUNABLE
#define ACTUAL_MAXIMUM_SIZE_CTL 0
#define ACTUAL_MODE_CTL 1
#define ACTUAL_BLOCKING_CTL 6 
#define ACTUAL_MAXIMUM_SEGMENT_SIZE_CTL 3
#define PURGE_OPTION_CTL 9
//ACTUAL_BLOCKING_CTL options
#define BLOCKING_MODE (unsigned long) 0
#define NON_BLOCKING_MODE (unsigned long) 1
//ACTUAL_MODE_CTL options
#define STREAMING_MODE (unsigned long)0
#define PACKET_MODE (unsigned long) 1
//PURGE PURGE_OPTION_CTL options
#define PURGE 1
#define NO_PURGE 0
//GET
#define GET_MAX_SEGMENT_SIZE 7
#define GET_FREE_SIZE 8

char read_buf[MAX_SEGMENT_SIZE];
char string[MAX_SEGMENT_SIZE];
int fd;


void scrivi(){
    int max_dim = ioctl(fd,GET_MAX_SEGMENT_SIZE,0); 
    int free_size = ioctl(fd, GET_FREE_SIZE, 0);
    printf("cosa vuoi scrivere? (DIM>%d sarà scartato), (%dB ancora liberi): ", max_dim, free_size);
				
    fgets(string, MAX_SEGMENT_SIZE, stdin);
    __fpurge(stdin);
                
    int stl = strlen(string);
                
    int res = write(fd, string, stl-1);
    if(res == -1) 
        puts("write: sei in modalita' non blocking e non c'è spazio a sufficienza");
    else if (res<stl-1)
        printf("Il tuo messaggio ha superato la MAX SEGMENT SIZE ED È STATO SCARTATO. La MSS è pari a %d\n", res);
}

int leggi(){
    puts("quanti byte vuoi leggere?");
    int to_read = leggiIntero();
	//printf("read_buf prima della lettura contiene %s\n\n", read_buf);
    int letti = read(fd, read_buf, to_read);
				
    if(letti==-1){
        puts("read: sei in modalita' non blocking e non ci sono byte a sufficienza nel buffer");
        return -1;
    }

    //anche se buf sporco mi fermo a letti
    read_buf[letti]= '\0';
    return 0;
}

int leggiIntero(){
	char buf[16];
	fgets(buf, 16, stdin);
	return atoi(buf);
}

int main(int argc, char** argv){
	if(argc!=3){
		printf("usage ./a.out <MAJOR> <MINOR>\n");
		return -1;
	}
	int major = atoi(argv[1]);
	int minor = atoi(argv[2]);
	char pathname[64];
	int res = 0;
	dev_t device = makedev(major, minor);
	
	
	sprintf(pathname,"/dev/prog%d", minor);

	if( res = (mknod(pathname, 0666 | S_IFCHR, device)) == -1){
		if(errno==17)
			printf("File already exists... continue :) \n");
		else{
			printf("error, errno is %d (IF 13 TRY SUDO)\n", errno);
			return -1;
		}
	}
		
	fd = open(pathname, 0666);
	
	if(fd==-1){
		printf("open failed... errno is %d (IF 13 try sudo)\n", errno);
		return -1;
	}
	
	while(1){
		int option = -1;
		
		puts("inserisci 1 per scrivere, 2 per leggere, 3 per ioctl, 4 per uscire");
		selected = leggiIntero();
		
		switch(selected){
			case 1:
				scrivi();	
				break;
			
			case 2:
				
                if(leggi() != -1)
                    printf("%s\n", read_buf);
				break;
			case 3:
				puts("Inserisci:\n1 per modificare blocking/nonblocking \
							\n2 per modificare pkt/streaming\
							\n3 per modificare la max segment size\
							\n4 per modificare la max size per il device\
                            \n5 per modificare PURGE/NO_PURGE");
				option = leggiIntero();
				switch(option){
					case 1:
							puts("digita 1 per blocking, 2 per non blocking (DEFAULT: BLOCKING)");
							option = leggiIntero();
							if(option==1){
								if(ioctl(fd,ACTUAL_BLOCKING_CTL,BLOCKING_MODE)==-1){
									puts("ioctl: sei in modalità non blocking ed il device è occupato");
								}
							}
							else{
								if(ioctl(fd,ACTUAL_BLOCKING_CTL,NON_BLOCKING_MODE)==-1){
									puts("ioctl: sei in modalità non blocking ed il device è occupato");
								}
							}
							break;
					case 2:
							puts("digita 1 per streaming, 2 per pkt (DEFAULT: STREAMING)");
							option = leggiIntero();
							if(option==1){
								if(ioctl(fd,ACTUAL_MODE_CTL,STREAMING_MODE)==-1){
									puts("ioctl: sei in modalità non blocking ed il device è occupato");
								}
							}
							else{
								if(ioctl(fd,ACTUAL_MODE_CTL,PACKET_MODE)==-1){
									puts("ioctl: sei in modalità non blocking ed il device è occupato");
								}
							}
							break;
					case 3:
						puts("Inserisci la nuova max segment size");
						option = leggiIntero();
                        res = ioctl(fd,ACTUAL_MAXIMUM_SEGMENT_SIZE_CTL,option);
						if(res==-EINVAL)
							puts("hai inserito una dimensione più grande del massimo, annullato");
                        else if(res==-1)
                            puts("ioctl: sei in modalità non blocking ed il device è occupato");
						break;
					case 4:
						puts("Inserisci la nuova dimensione massima per il device");
						option = leggiIntero();
                        res = ioctl(fd,ACTUAL_MAXIMUM_SIZE_CTL,option);
						if(res==-EINVAL)
							puts("hai inserito una dimensione più grande del massimo, annullato");
                        else if(res==-1)
                            puts("ioctl: sei in modalità non blocking ed il device è occupato");
						break;
                    case 5:
						puts("Inserisci 1 per il purge, 0 per il no purge (DEFAULT: NO PURGE)");
						option = leggiIntero();
						switch(option){
                            case 0:
                                if(ioctl(fd,PURGE_OPTION_CTL,NO_PURGE)==-1){
									puts("ioctl: sei in modalità non blocking ed il device è occupato");
								}
                            case 1:
                                if(ioctl(fd,PURGE_OPTION_CTL,PURGE)==-1){
									puts("ioctl: sei in modalità non blocking ed il device è occupato");
								}
                        }
						break;
					default:
						puts("Non valido.");
				}
				break;
			case 4:
				close(fd);
				return 0;
		}
		
		//READY FOR A NEW ITERATION
	}
	
	
}

