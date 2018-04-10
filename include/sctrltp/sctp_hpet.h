/* timer libary for initializing hpet timer 
 * (WILL BE USED IF RTC IS NOT WORKING PROPERLY)*/

#ifndef _US_SCTP_HPET
#define _US_SCTP_HPET

#include <stdio.h>
#include <fcntl.h>
#include <linux/hpet.h>
#include <sys/ioctl.h>
#include <signal.h>

struct sctp_hpet {
	__s32   fd;
	sig_t   old_sigh;
}

#endif
