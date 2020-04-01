/* Implementation of Real Time Clock functions
 * */

#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include "sctrltp/us_sctp_timer.h"

namespace sctrltp {

/* Opens timer device file and tests if periodic interrupts can be enabled*/
__s32 timer_init (struct sctp_timer *desc, char *dev, __u32 freq)
{
	__s32 retval;
	/*Open real time clock*/
	desc->fd = open (dev, O_RDONLY);
	if (desc->fd < 0) {
		perror("timer_init");
		return errno;
	}

	/*Set frequency of periodic timer interrupts*/
	retval = ioctl (desc->fd, RTC_IRQP_SET, freq);
	if (retval < 0) {
		perror ("timer_init set freq");
		close (desc->fd);
		return errno;
	}

	/*Enable periodic timer interrupts*/
	retval = ioctl (desc->fd, RTC_PIE_ON, 0);
	if (retval < 0) {
		perror ("timer_init enable irqs");
		close (desc->fd);
		return errno;
	}

	return 0;
}

void timer_poll (struct sctp_timer *desc)
{
	__s32 tmp;
	__s32 retval;
	retval = read (desc->fd, &tmp, sizeof (__s32));
	if (retval < 0)
		perror ("timer_poll");
	static int run = 0;
	run++;
}

/* Closes timer device file */
void timer_close (struct sctp_timer *desc)
{
	__s32 retval;

	/*Disable periodic interrupts*/
	retval = ioctl (desc->fd, RTC_PIE_OFF, 0);
	if (retval < 0)
		perror ("timer_close disable irqs");

	close (desc->fd);
}

} // namespace sctrltp
