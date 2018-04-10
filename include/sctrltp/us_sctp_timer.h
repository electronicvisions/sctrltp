/* TIMER based time measurement routines 
 * (UNDER DEVELOPMENT!!!)*/

#ifndef _US_SCTP_TIMER
#define _US_SCTP_TIMER

#include <fcntl.h>
#include <linux/hpet.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <unistd.h>
#include <stdio.h>
#include <poll.h>

#include <errno.h>
#include <assert.h>

struct sctp_timer {
	__s32 fd;                 /* File descriptor of the opened timer driver file */
	/* HPET / polling stuff */
	struct hpet_info info;
	struct pollfd pfd;
} __attribute__ ((packed));

/* Opens timer device file and tests if periodic interrupts can be enabled*/
__s32 timer_init (struct sctp_timer *desc, char *dev, __u32 freq);

/* Waits for timer event*/
void timer_poll (struct sctp_timer *desc);

/* Enables/Disables periodic interrupts (flag > 0: enable flag <= 0: disable)*/
__s32 timer_disenable (struct sctp_timer *desc, __u8 flag);

/* Closes timer device file */
void timer_close (struct sctp_timer *desc);

#endif
