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
#include <unistd.h>

#include "sctrltp/libhostarq.h"
#include "sctrltp/us_sctp_defs.h"

/* We cannot #include <linux/fcntl.h> on Debian Jessie,
 * so we copied the define */
#ifndef O_TMPFILE // FIXME Issue 3523: extra changeset?
#define O_TMPFILE 020200000
#endif

/* Time interval in us for the parent processes when waiting for init
 * completion. The total time (RESET_TIMEOUT) is defined in a header file. */
#define HOSTARQ_PARENT_SLEEP_INTERVAL 10000

/* 20 bytes for integers-to-char-string conversion should be enough... */
#define MAX_INT_STRING_SIZE 20
#define MAX_PORT_STRING_SIZE 6

using namespace sctrltp;

void hostarq_create_handle(
    struct hostarq_handle* handle,
    char const shm_name[],
    char const remote_ip[],
    __u16 const udp_data_port,
    __u16 const udp_reset_port,
    __u16 const udp_data_local_port,
    bool const init)
{
	const char shm_name_prefix[] = "/dev/shm/";

	/* parameter checking */
	if (handle == NULL) {
		LOG_ERROR("handle parameter is unset");
		abort();
	}

	if (shm_name == NULL) {
		LOG_ERROR("shm_name parameter is unset");
		abort();
	} else if (strlen(shm_name) >= NAME_MAX) {
		LOG_ERROR("Filename for shared-memory communication is too long");
		abort();
	}

	if (remote_ip == NULL) {
		LOG_ERROR("remote_ip parameter is unset");
		abort();
	}

	/* construction begins */
	handle->pid = 0;
	handle->shm_name = static_cast<char*>(malloc(strlen(shm_name) + 1));
	if (handle->shm_name == NULL) {
		perror("malloc of handle->shm_name failed");
		abort();
	}
	strcpy(handle->shm_name, shm_name);

	handle->shm_path = static_cast<char*>(malloc(strlen(shm_name_prefix) + NAME_MAX + 1));
	if (handle->shm_path == NULL) {
		perror("malloc for handle->shm_path failed");
		abort();
	}
	strcpy(handle->shm_path, shm_name_prefix);
	strncat(handle->shm_path, handle->shm_name, NAME_MAX);

	handle->remote_ip = static_cast<char*>(malloc(strlen(remote_ip) + 1));
	if (handle->remote_ip == NULL) {
		perror("malloc of handle->remote_ip failed");
		abort();
	}
	strcpy(handle->remote_ip, remote_ip);

	handle->udp_data_port = udp_data_port;
	handle->udp_reset_port = udp_reset_port;
	handle->udp_data_local_port = udp_data_local_port;

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


void hostarq_open(struct hostarq_handle* handle, char const* const hostarq_daemon_string)
{
	int ret, ret2, i, fd, flag;
	char fd_string[MAX_INT_STRING_SIZE], init_string[MAX_INT_STRING_SIZE];
	char const lockdir[] = "/var/run/lock/hicann";
	char lockdir_startupfile[] = "/var/run/lock/hicann/hostarq_startup_XXXXXX";
	char udp_data_port_string[MAX_PORT_STRING_SIZE], udp_reset_port_string[MAX_PORT_STRING_SIZE],
	    udp_data_local_port_string[MAX_PORT_STRING_SIZE];
	struct stat lockdir_stat;

	/* parameter checking */
	if (handle->pid != 0) {
		LOG_ERROR("pid is already set");
		abort();
	}

	if (handle->shm_name == NULL) {
		LOG_ERROR("shm_name is unset");
		abort();
	}

	if (handle->shm_path == NULL) {
		LOG_ERROR("shm_path is unset");
		abort();
	}

	if (handle->remote_ip == NULL) {
		LOG_ERROR("remote_ip is unset");
		abort();
	}

	/* try create "hicann" lock dir if does not exist */
	ret = mkdir(lockdir, 0777);
	if ((ret < 0) && (errno == EEXIST)) {
		/* path already exists */
		ret2 = stat(lockdir, &lockdir_stat);
		if (ret2 < 0) {
			LOG_ERROR("Could not perform stat() on lockdir (%s): %s\n",
			          lockdir, strerror(errno));
			abort();
		} else if (!S_ISDIR(lockdir_stat.st_mode)) {
			LOG_ERROR("lockdir (%s) exists but is not a directory!\n",
			          lockdir);
			abort();
		} else if (!(lockdir_stat.st_mode  & (S_IRWXU | S_IRWXG | S_IRWXO))) {
			LOG_ERROR("lockdir (%s) exists but has wrong mode\n",
			          lockdir);
			abort();
		}
	} else if (ret < 0) {
		/* unknown error */
		LOG_ERROR("Failed to create lockdir (%s): %s\n",
		          lockdir, strerror(errno));
		abort();
	} else {
		/* created directory; now set mode */
		ret2 = chmod(lockdir, 0777);
		if (ret2 != 0) {
			LOG_ERROR("Could not set lockdir (%s) mode: %s\n",
			          lockdir, strerror(errno));
			abort();
		}
	}


	fd = mkstemp(lockdir_startupfile);
	if (fd < 0) {
		LOG_ERROR("Could not create/open startup lockfile (template: %s): %s\n",
		          lockdir_startupfile, strerror(errno));
		abort();
	}

	/* convert other function parameters to strings... */
	if ((snprintf(fd_string, MAX_INT_STRING_SIZE, "%d", fd) >= MAX_INT_STRING_SIZE) ||
	    (snprintf(init_string, MAX_INT_STRING_SIZE, "%d", handle->init) >= MAX_INT_STRING_SIZE) ||
	    (snprintf(udp_data_port_string, MAX_PORT_STRING_SIZE, "%u", handle->udp_data_port) >=
	     MAX_PORT_STRING_SIZE) ||
	    (snprintf(udp_reset_port_string, MAX_PORT_STRING_SIZE, "%u", handle->udp_reset_port) >=
	     MAX_PORT_STRING_SIZE) ||
	    (snprintf(
	         udp_data_local_port_string, MAX_PORT_STRING_SIZE, "%u", handle->udp_data_local_port) >=
	     MAX_PORT_STRING_SIZE)) {
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
		char* const params[9] = {const_cast<char*>(hostarq_daemon_string),
		                         handle->shm_name,
		                         fd_string,
		                         handle->remote_ip,
		                         udp_data_port_string,
		                         udp_reset_port_string,
		                         udp_data_local_port_string,
		                         init_string,
		                         NULL};
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
		LOG_ERROR("pid isn't set");
		abort();
	}

	if (handle->shm_path == NULL) {
		LOG_ERROR("shm_path isn't set");
		abort();
	}

	/* check if shared memory file exists */
	if (access(handle->shm_path, F_OK) < 0) {
		LOG_ERROR("shm_path invalid: \"%s\"?", handle->shm_path);
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
	LOG_WARN("Shared memory file %s still existing after 1s wait; "
	         "unlinking anyways...",
	         handle->shm_path);
	if (unlink(handle->shm_path) < 0) {
		perror("Failed to unlink shared mem file");
		return -1;
	}
	return 0;
}