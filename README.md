# AOSV-Project
Whit this code I have created a linux driver for a multimode (stream/packet) device file, using linux modules. 
This work has been done as project for the exam "Advanced Operating System and Virtualization", 
taken in the 2016 at University La Sapienza.


The request of the project was:


Project - Multimode (stream/packet) device file
This specification relates to the implementation of a special device file that is accessible according to 
FIFO style semantic (via open/close/read/write services). Any data segment that is posted to the stream associated 
with the file is seen either as an independent data unit (a packet) or a portion of a unique stream.
An ioctl command must be supported to specify whether the current operating mode of the device file is stream or packet. 
This only affects read operations, since write operations simply post data onto the device file unique logical stream. 
Packets, say stream segments, currently buffered within the device file are delivered upon read operations according to a 
streaming (TCP-like) rule if the stream operating mode is posted via the ioctl command. Otherwise they are delivered just 
like individual packets. Packet delivery must lead to discard portions of a data segment in case the requested amount of 
bytes to be delivered is smaller that the actual segment size. On the other hand, the residual of undelivered bytes must 
not be discarded if the current operating mode of the defice file is stream. Such residual will figure out, in its turn, 
as an independed data segment.
The device file needs to be multi-instance (by having the possibility to manage at least 256 different instances) so that 
mutiple FIFO style streams (characterized by the above semantic) can be concurrently accessed by active processes/threads. 
File system nodes associated with the same minor number need to be allowed, whch can be managed concurrently.
The device file needs to also support other ioctl commands in order to define the run time behavior of any I/O session 
targeting it (such as whether read and/or write operations on a session need to be performed according to blocking or 
non-blocking rules).
Parameters that are left to the designer, which should be selected via reasonable policies, are:
• the maximum size of device file buffered data or individual data segments (this might also be made tunable via ioctl up to an absolute upper limit)
• the maximum storage that can be (dynamically) reserved for any individual instance of the device file
• the range of device file minor numbers supported by the driver (it could be the interval [0-255] or not).
