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

#ifndef PARAMETERS
#define PARAMETERS Parameters<>
#endif

static __s32 exiting = 0;
static char const* our_shm_name = NULL;

using namespace sctrltp;

/* used for abnormal termination triggered by some signal */
template <typename P>
void termination_handler(int signum)
{
	/* we could see multiple signals during exit => ignore them */
	if (cmpxchg(&(exiting), 0, 1) == 0)
		SCTP_CoreDown<P>();
	if (signum == HOSTARQ_EXIT_SIGNAL)
		exit(EXIT_SUCCESS);
	exit(signum);
}

int main(int argc, const char *argv[])
{
	char const *remote_ip;
	int fd;
	__u16 port_data;
	__u16 port_reset;
	__u16 local_port_data;
	bool init;
	__u64 unique_queues_size;
	__u16* unique_queues;

	__u64 const min_args = 9;
	if ((__u64)argc < min_args) {
		fprintf(
		    stderr, "Usage: %s [SHM_NAME] [FD] [REMOTE_IP] [DATA_PORT] [RESET_PORT] [DATA_LOCAL_PORT] [INIT] [QUEUE_SIZE] [QUEUES ...]\n",
		    argv[0]);
		for (int i=0; i<argc; ++i) {
			fprintf(stderr, "%s\n", argv[i]);
		}
		exit(EXIT_FAILURE);
	}

	/* parse argv strings */
	our_shm_name = argv[1];
	fd = atoi(argv[2]);
	remote_ip = argv[3];
	port_data = (__u16) atoi(argv[4]);
	port_reset = (__u16) atoi(argv[5]);
	local_port_data = (__u16) atoi(argv[6]);
	init = atoi(argv[7]);
	unique_queues_size = atoi(argv[8]);
	if ((argc - min_args) != unique_queues_size) {
		fprintf(
		    stderr, "Queue size %llu but only %llu provided\n", unique_queues_size,
		    (argc - min_args));
		exit(EXIT_FAILURE);
	}
	if (PARAMETERS::MAX_UNIQUE_QUEUES < unique_queues_size) {
		fprintf(
		    stderr, "MAX_UNIQUE_QUEUES is %lu but %llu requested\n", PARAMETERS::MAX_UNIQUE_QUEUES,
		    unique_queues_size);
		exit(EXIT_FAILURE);
	}
	unique_queues = static_cast<__u16*>(malloc(unique_queues_size * sizeof(__u16)));
	for (__u64 i = 0; i < unique_queues_size; ++i) {
		*(unique_queues + i) = atoi(argv[9 + i]);
		printf("%d;", *(unique_queues + i));
	}
	if (unique_queues_size) {
		printf("\n");
	}

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
	new_action.sa_handler = termination_handler<PARAMETERS>;
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

	/* child me up */
	if (SCTP_CoreUp<PARAMETERS>(our_shm_name, remote_ip, port_data, port_reset, local_port_data, init, unique_queues, unique_queues_size) < 1) {
		fprintf(stderr, "Error occurred when starting up HostARQ software\n");
		return EXIT_FAILURE;
	}

	/* Signal parent that core is up, by changing file status flag */
	fcntl(fd, F_SETFL, O_NONBLOCK);
	close(fd);

	free(unique_queues);
	unique_queues = NULL;
	unique_queues_size = 0;

	while (true)
		pause();

	return 0;
}
