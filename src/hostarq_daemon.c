#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include "sctrltp/libhostarq.h"
#include "sctrltp/packets.h"
#include "sctrltp/start_core.h"
#include "sctrltp/us_sctp_core.h"

volatile sig_atomic_t post_init = 0;
static __s32 exiting = 0;
static char const* our_shm_name = NULL;

void call_exit() {
	if (our_shm_name != NULL) {
		/* thread unsafe... but who cares? let's delete it multiple times! */
		shm_unlink(our_shm_name);
	}
}

/* used for abnormal termination triggered by some signal */
void termination_handler (int signum) {
	if (!post_init) {
		fprintf(stderr, "Got signal %d but HostARQ init was not completed, "
				"don't know what to do => aborting\n", signum);
		/* we're dying illegally => signal to parent... */
		kill(getppid(), SIGINT);
		exit(EXIT_FAILURE);
	}
	/* we could see multiple signals during exit => ignore them */
	if (cmpxchg(&(exiting), 0, 1) == 0)
		SCTP_CoreDown();
	exit(EXIT_SUCCESS);
}

int main(int argc, const char *argv[])
{
	char const *remote_ip;
	int fd;
	bool init;

	if (argc != 5) {
		fprintf(stderr, "Usage: %s [SHM_NAME] [FD] [REMOTE_IP] [INIT]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	/* parse argv strings */
	our_shm_name = argv[1];
	fd = atoi(argv[2]);
	remote_ip = argv[3];
	init = atoi(argv[4]);

	if (fd <= 2) {
		fprintf(stderr, "Descriptor for lockfile seems broken: %d\n", fd);
		exit(EXIT_FAILURE);
	} else if (fcntl(fd, F_GETFD) < 0) {
		perror("hostarq_daemon");
		exit(EXIT_FAILURE);
	}

	/* inform child of dying parents */
	prctl(PR_SET_PDEATHSIG, HOSTARQ_EXIT_SIGNAL);

	/* activate signal (termination) handler for some standard signals:
	 * SIGINT, SIGHUP, SIGTERM, SIGQUIT, SIGCHLD
	 * but if top-level ignores, we also ignore */
	struct sigaction new_action, old_action;
	new_action.sa_handler = termination_handler;
	sigfillset(&new_action.sa_mask); /* ignore all signals during signal handling */
	new_action.sa_flags = 0;

#define ADDSIGNALHANDLER(SIGNAL) \
	sigaction(SIGNAL, NULL, &old_action); \
	if (old_action.sa_handler != SIG_IGN) \
		sigaction(SIGNAL,  &new_action, NULL);
	ADDSIGNALHANDLER(SIGINT);
	ADDSIGNALHANDLER(SIGHUP);
	ADDSIGNALHANDLER(SIGTERM);
	ADDSIGNALHANDLER(SIGQUIT);
	ADDSIGNALHANDLER(SIGCHLD);
	/* now our "custom" signal to trigger a shutdown */
	ADDSIGNALHANDLER(HOSTARQ_EXIT_SIGNAL);
#undef ADDSIGNALHANDLER

	/* register exit handler (handles shutdown of core) */
	atexit(call_exit);

	/* child me up */
	if (SCTP_CoreUp(our_shm_name, remote_ip, init) < 1) {
		fprintf(stderr, "Error occurred when starting up HostARQ software\n");
		return EXIT_FAILURE;
	}

	/* we're done */
	post_init = 1;

	/* Signal parent that core is up, by changing file status flag */
	fcntl(fd, F_SETFL, O_NONBLOCK);
	close(fd);

	while (true)
		pause();

	return 0;
}
