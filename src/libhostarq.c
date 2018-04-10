#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "sctrltp/libhostarq.h"
#include "sctrltp/us_sctp_defs.h"

/* We cannot #include <linux/fcntl.h> on Debian Jessie,
 * so we copied the define */
#define O_TMPFILE 020200000

/* Time interval in us for the parent processes when waiting for init
 * completion. The total time (RESET_TIMEOUT) is defined in a header file. */
#define HOSTARQ_PARENT_SLEEP_INTERVAL 10000

/* 20 bytes for integers-to-char-string conversion should be enough... */
#define MAX_INT_STRING_SIZE 20

void hostarq_create_handle(
	struct hostarq_handle* handle, char const shm_name[], char const remote_ip[], bool const init)
{
	const char shm_name_prefix[] = "/dev/shm/";

	/* parameter checking */
	if (handle == NULL) {
		fprintf(stderr, "handle parameter is unset\n");
		abort();
	}

	if (shm_name == NULL) {
		fprintf(stderr, "shm_name parameter is unset\n");
		abort();
	} else if (strlen(shm_name) >= NAME_MAX) {
		fprintf(stderr, "Filename for shared-memory communication is too long\n");
		abort();
	}

	if (remote_ip == NULL) {
		fprintf(stderr, "remote_ip parameter is unset\n");
		abort();
	}

	/* construction begins */
	handle->pid = 0;
	handle->shm_name = malloc(strlen(shm_name) + 1);
	if (handle->shm_name == NULL) {
		perror("malloc of handle->shm_name failed");
		abort();
	}
	strcpy(handle->shm_name, shm_name);

	handle->shm_path = malloc(strlen(shm_name_prefix) + NAME_MAX + 1);
	if (handle->shm_path == NULL) {
		perror("malloc for handle->shm_path failed");
		abort();
	}
	strcpy(handle->shm_path, shm_name_prefix);
	strncat(handle->shm_path, handle->shm_name, NAME_MAX);

	handle->remote_ip = malloc(strlen(remote_ip) + 1);
	if (handle->remote_ip == NULL) {
		perror("malloc of handle->remote_ip failed");
		abort();
	}
	strcpy(handle->remote_ip, remote_ip);

	handle->init = init;
}


void
hostarq_free_handle(struct hostarq_handle* handle)
{
	free(handle->shm_name);
	handle->shm_name = NULL;
	free(handle->shm_path);
	handle->shm_path = NULL;
	free(handle->remote_ip);
	handle->remote_ip = NULL;
}


void
hostarq_open(struct hostarq_handle* handle)
{
	int i, fd, flag;
	char hostarq_daemon_string[] = "hostarq_daemon";
	char fd_string[MAX_INT_STRING_SIZE], init_string[MAX_INT_STRING_SIZE];

	/* parameter checking */
	if (handle->pid != 0) {
		fprintf(stderr, "pid is already set\n");
		abort();
	}

	if (handle->shm_name == NULL) {
		fprintf(stderr, "shm_name is unset\n");
		abort();
	}

	if (handle->shm_path == NULL) {
		fprintf(stderr, "shm_path is unset\n");
		abort();
	}

	if (handle->remote_ip == NULL) {
		fprintf(stderr, "remote_ip is unset\n");
		abort();
	}

	fd = open("/var/run/lock/hicann", O_TMPFILE | O_WRONLY | O_EXCL, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		perror("Could not create/open startup lockfile");
		abort();
	}

	/* convert other function parameters to strings... */
	if ((snprintf(fd_string, MAX_INT_STRING_SIZE, "%d", fd) >= MAX_INT_STRING_SIZE) ||
	    (snprintf(init_string, MAX_INT_STRING_SIZE, "%d", handle->init) >= MAX_INT_STRING_SIZE)) {
		fprintf(stderr, "int to string conversion required too many bytes?\n");
		abort();
	}

	/* the child will signal its startup completion using fd */
	flag = fcntl(fd, F_GETFL);
	if (flag < 0) {
		perror("Could not stat lockfile");
		abort();
	}

	handle->pid = fork();
	/* the child shares the same set of file descriptors (except for those with O_CLOEXEC) */
	if (handle->pid == 0 /* child */) {
		/* execvp's params are `char * const *`, so we have to provide non-const parameters */
		char* const params[6] = {hostarq_daemon_string, handle->shm_name, fd_string,
		                         handle->remote_ip,     init_string,      NULL};
		execvp(params[0], params);
		perror("libhostarq tried to spawn HostARQ daemon");
		abort();
	} else if (handle->pid > 0 /* parent */) {
		/* wait for child to change fd flag on finished init */
		for (i = 0; i < RESET_TIMEOUT; i += HOSTARQ_PARENT_SLEEP_INTERVAL) {
			usleep(HOSTARQ_PARENT_SLEEP_INTERVAL);
			if (fcntl(fd, F_GETFL) != flag) {
				/*flag was changed by child*/
				break;
			}
		}
		close(fd);
	} else {
		perror("Could not fork HostARQ processes");
		abort();
	}
}


int
hostarq_close(struct hostarq_handle* handle)
{
	int i, ret;

	if (handle->pid == 0) {
		fprintf(stderr, "pid isn't set\n");
		abort();
	}

	if (handle->shm_path == NULL) {
		fprintf(stderr, "shm_path isn't set\n");
		abort();
	}

	/* check if shared memory file exists */
	if (access(handle->shm_path, F_OK) < 0) {
		perror("testing for shm_path failed");
		fprintf(stderr, "shm_path points to \"%s\"\n", handle->shm_path);
		return -1;
	}

	/* let's wait 1s max */
	ret = 0;
	for (i = 0; i < 1000 * 1000 / HOSTARQ_PARENT_SLEEP_INTERVAL; i++) {
		/* if child didn't change state until now:
		 *   - send kill signal (could get lost)
		 *   - check for state change
		 */
		if (ret == 0) {
			kill(handle->pid, HOSTARQ_EXIT_SIGNAL);
			ret = waitpid(handle->pid, NULL, WNOHANG);
		}
		/* check for correct shutdown (i.e. process and shm file both gone) */
		if ((ret > 0) && (access(handle->shm_path, F_OK) < 0))
			return ret;
		/* bail out if the shm file was deleted but the process still exists */
		if (access(handle->shm_path, F_OK) < 0)
			return 0;
		/* if process state check failed and file still there retry kill/check */
		if ((ret == -1) && (errno == EINTR))
			ret = 0;
		/* shm file still there => wait for a bit longer */
		usleep(HOSTARQ_PARENT_SLEEP_INTERVAL);
	}
	fprintf(
	    stderr, "Shared memory file %s still existing after 1s wait; "
	            "unlinking anyways...\n",
	    handle->shm_path);
	if (unlink(handle->shm_path) < 0) {
		perror("Failed to unlink shared mem file");
		return -1;
	}
	return 0;
}
