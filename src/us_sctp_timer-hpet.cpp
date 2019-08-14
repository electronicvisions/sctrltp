/* Implementation of High Precision Event Timer functions
 * */

#include "sctrltp/us_sctp_timer.h"

namespace sctrltp {

/* Opens timer device file and tests if periodic interrupts can be enabled*/
__s32 timer_init (struct sctp_timer *desc, char *dev, __u32 freq)
{
	/* Open HPET */
	desc->fd = open (dev, O_RDONLY);
	if (desc->fd < 0) {
		perror("timer_init");
		return errno;
	}

	/*Set frequency of periodic timer interrupts*/
	if (ioctl(desc->fd, HPET_IRQFREQ, freq) < 0) {
		fprintf(stderr, "hpet_poll: HPET_IRQFREQ failed\n");
		goto error;
	}

	if (ioctl(desc->fd, HPET_INFO, &desc->info) < 0) {
		fprintf(stderr, "hpet_poll: failed to get info\n");
		goto error;
	}

	fprintf(stderr, "hpet_poll: info.hi_flags 0x%lx\n", desc->info.hi_flags);

	if (desc->info.hi_flags && (ioctl(desc->fd, HPET_EPI, 0) < 0)) {
		fprintf(stderr, "hpet_poll: HPET_EPI failed\n");
		goto error;
	}

	if (ioctl(desc->fd, HPET_IE_ON, 0) < 0) {
		fprintf(stderr, "hpet_poll, HPET_IE_ON failed\n");
		goto error;
	}

	desc->pfd.fd = desc->fd;
	desc->pfd.events = POLLIN;

	return 0;

error:
	fprintf (stderr, "argl\n");
	close(desc->fd);
	return -1;

}


void timer_poll (struct sctp_timer *desc)
{
	__s32 retval;
	long tmp;

	desc->pfd.revents = 0;
	do {
		retval = poll(&desc->pfd, 1, -1);
	} while (retval < 0 && errno == EINTR);

	assert (retval >= 0); /* 0 == timeout, negative indicates error */

	do {
		retval = read(desc->fd, &tmp, sizeof(tmp));
	} while ((retval < 0) && (errno == EINTR));
	if (retval != sizeof(tmp)) {
		fprintf(stderr, "hpet_poll: read failed\n");
	}
}

	/* Closes timer device file */
void timer_close (struct sctp_timer *desc)
{
	close(desc->fd);
	return;

}

} // namespace sctrltp
