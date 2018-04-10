#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <memory.h>
#include <malloc.h>
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <linux/hpet.h>


extern int  hpet_init (const char *, int);
extern void hpet_poll ();
extern void hpet_close ();

#include <sys/poll.h>
#include <sys/ioctl.h>
#include <signal.h>

#define FREQ 10     /* [Hz] */
#define ITER 10     /* Runs */

int main (int argc, const char ** argv) {
	int retval;
	char *device = "/dev/hpet";

	retval = hpet_init (device, sizeof(device));

	if (retval) {
		fprintf(stderr, "hpet_init failed\n");
		goto out;
	}

	/* do some timer polling */
	hpet_poll ();

out:
	hpet_close();
	return;
}


struct pollfd pfd;
unsigned long freq;
int iterations, fd;
struct timeval stv, etv;
struct timezone tz;
long usec;
struct hpet_info info;

int hpet_init (const char *device, int len) {
	freq = FREQ;
	iterations = ITER;

	fd = open(device, O_RDONLY);

	if (fd < 0) {
		fprintf(stderr, "hpet_poll: open of %s failed\n", device);
		return;
	}

	if (ioctl(fd, HPET_IRQFREQ, freq) < 0) {
		fprintf(stderr, "hpet_poll: HPET_IRQFREQ failed\n");
		goto error;
	}

	if (ioctl(fd, HPET_INFO, &info) < 0) {
		fprintf(stderr, "hpet_poll: failed to get info\n");
		goto error;
	}

	fprintf(stderr, "hpet_poll: info.hi_flags 0x%lx\n", info.hi_flags);

	if (info.hi_flags && (ioctl(fd, HPET_EPI, 0) < 0)) {
		fprintf(stderr, "hpet_poll: HPET_EPI failed\n");
		goto error;
	}

	if (ioctl(fd, HPET_IE_ON, 0) < 0) {
		fprintf(stderr, "hpet_poll, HPET_IE_ON failed\n");
		goto error;
	}

	pfd.fd = fd;
	pfd.events = POLLIN;

	return 0;

error:
	fprintf (stderr, "argl\n");
	close(fd);
	return -1;
}

void hpet_poll () {
	int i;

	for (i = 0; i < iterations; i++) {
		pfd.revents = 0;
		gettimeofday(&stv, &tz);
		if (poll(&pfd, 1, -1) < 0)
			fprintf(stderr, "hpet_poll: poll failed\n");
		else {
			long data;

			gettimeofday(&etv, &tz);
			usec = stv.tv_sec * 1000000 + stv.tv_usec;
			usec = (etv.tv_sec * 1000000 + etv.tv_usec) - usec;

			fprintf(stderr,
				"hpet_poll: expired time = 0x%lx\n", usec);

			fprintf(stderr, "hpet_poll: revents = 0x%x\n",
				pfd.revents);

			if (read(fd, &data, sizeof(data)) != sizeof(data)) {
				fprintf(stderr, "hpet_poll: read failed\n");
			}
			else
				fprintf(stderr, "hpet_poll: data 0x%lx\n",
					data);
		}
	}


}

void hpet_close () {
	close(fd);
	return;
}
