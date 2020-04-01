#pragma once
/* timer libary for initializing hpet timer 
 * (WILL BE USED IF RTC IS NOT WORKING PROPERLY)*/

#include <stdio.h>
#include <fcntl.h>
#include <linux/hpet.h>
#include <sys/ioctl.h>
#include <signal.h>

struct sctp_hpet {
	__s32   fd;
	sig_t   old_sigh;
}
