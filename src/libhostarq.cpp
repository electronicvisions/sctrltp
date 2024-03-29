#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "sctrltp/libhostarq.h"
#include "sctrltp/us_sctp_defs.h"

/* We cannot #include <linux/fcntl.h> on Debian Jessie,
 * so we copied the define */
#ifndef O_TMPFILE // FIXME Issue 3523: extra changeset?
#define O_TMPFILE 020200000
#endif

/* Time interval in us for the parent processes when waiting for init
 * completion. The total time (P::RESET_TIMEOUT) is defined in a header file. */
#define HOSTARQ_PARENT_SLEEP_INTERVAL 10000

/* 20 bytes for integers-to-char-string conversion should be enough... */
#define MAX_INT_STRING_SIZE 20
#define MAX_PORT_STRING_SIZE 6
#define MAX_PID_STRING_SIZE 6
#define MAX_QUEUE_SIZE_STRING_SIZE 21

namespace sctrltp {

void hostarq_create_handle(
    struct hostarq_handle* handle,
    char const shm_name[],
    char const remote_ip[],
    __u16 const udp_data_port,
    __u16 const udp_reset_port,
    __u16 const udp_data_local_port,
    bool const init,
    unique_queue_set_t unique_queues)
{
	const char shm_name_prefix[] = "/dev/shm/";

	/* parameter checking */
	if (handle == NULL) {
		SCTRL_LOG_ERROR("handle parameter is unset");
		abort();
	}

	if (shm_name == NULL) {
		SCTRL_LOG_ERROR("shm_name parameter is unset");
		abort();
	} else if (strlen(shm_name) >= NAME_MAX) {
		SCTRL_LOG_ERROR("Filename for shared-memory communication is too long");
		abort();
	}

	if (remote_ip == NULL) {
		SCTRL_LOG_ERROR("remote_ip parameter is unset");
		abort();
	}

	handle->unique_queues = unique_queues;

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


template<typename P>
void hostarq_open(struct hostarq_handle* handle, char const* const hostarq_daemon_string)
{
	int ret, ret2, fd, flag;
	char fd_string[MAX_INT_STRING_SIZE], init_string[MAX_INT_STRING_SIZE];
	char const lockdir[] = "/var/run/lock/hicann";
	char lockdir_startupfile[] = "/var/run/lock/hicann/hostarq_startup_XXXXXX";
	char udp_data_port_string[MAX_PORT_STRING_SIZE], udp_reset_port_string[MAX_PORT_STRING_SIZE],
	    udp_data_local_port_string[MAX_PORT_STRING_SIZE];
	std::vector<std::string> queue_string;
	struct stat lockdir_stat;

	/* parameter checking */
	if (handle->pid != 0) {
		SCTRL_LOG_ERROR("pid is already set");
		abort();
	}

	if (handle->shm_name == NULL) {
		SCTRL_LOG_ERROR("shm_name is unset");
		abort();
	}

	if (handle->shm_path == NULL) {
		SCTRL_LOG_ERROR("shm_path is unset");
		abort();
	}

	if (handle->remote_ip == NULL) {
		SCTRL_LOG_ERROR("remote_ip is unset");
		abort();
	}

	/* try create "hicann" lock dir if does not exist */
	ret = mkdir(lockdir, 0777);
	if ((ret < 0) && (errno == EEXIST)) {
		/* path already exists */
		ret2 = stat(lockdir, &lockdir_stat);
		if (ret2 < 0) {
			SCTRL_LOG_ERROR("Could not perform stat() on lockdir (%s): %s\n",
			          lockdir, strerror(errno));
			abort();
		} else if (!S_ISDIR(lockdir_stat.st_mode)) {
			SCTRL_LOG_ERROR("lockdir (%s) exists but is not a directory!\n",
			          lockdir);
			abort();
		} else if (!(lockdir_stat.st_mode  & (S_IRWXU | S_IRWXG | S_IRWXO))) {
			SCTRL_LOG_ERROR("lockdir (%s) exists but has wrong mode\n",
			          lockdir);
			abort();
		}
	} else if (ret < 0) {
		/* unknown error */
		SCTRL_LOG_ERROR("Failed to create lockdir (%s): %s\n",
		          lockdir, strerror(errno));
		abort();
	} else {
		/* created directory; now set mode */
		ret2 = chmod(lockdir, 0777);
		if (ret2 != 0) {
			SCTRL_LOG_ERROR("Could not set lockdir (%s) mode: %s\n",
			          lockdir, strerror(errno));
			abort();
		}
	}


	fd = mkstemp(lockdir_startupfile);
	if (fd < 0) {
		SCTRL_LOG_ERROR("Could not create/open startup lockfile (template: %s): %s\n",
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

	if (handle->unique_queues.size() >= MAX_QUEUE_SIZE_STRING_SIZE) {
		fprintf(stderr, "u64 to string conversion required too many bytes?\n");
		abort();
	}

	if (handle->unique_queues.size() > P::MAX_UNIQUE_QUEUES) {
		fprintf(stderr, "too many unique unique_queues specified (limited by MAX_UNIQUE_QUEUES)\n");
		abort();
	}

	for(auto queue_pid : handle->unique_queues) {
		queue_string.push_back(std::to_string(queue_pid));
	}

	/* the child will signal its startup completion using fd */
	flag = fcntl(fd, F_GETFL);
	if (flag < 0) {
		perror("Could not stat lockfile");
		abort();
	}

	//ignore signals from hostarq daemon during startup
	struct sigaction old_action{}, tmp_action{};
	tmp_action.sa_handler = SIG_IGN;
	sigaction(HOSTARQ_FAIL_SIGNAL, &tmp_action, &old_action);

	handle->pid = fork();
	/* the child shares the same set of file descriptors (except for those with O_CLOEXEC) */
	if (handle->pid == 0 /* child */) {
		{
			// set signal mask to a known state, because it might have been altered in parent
			sigset_t sigset;
			sigfillset(&sigset);
			sigprocmask(SIG_UNBLOCK, &sigset, NULL);
		}

		/* execvp's params are `char * const *`, so we have to provide non-const parameters */
		char** params = static_cast<char**>(malloc(sizeof(char*) * (10 + handle->unique_queues.size())));
		params[0] = const_cast<char*>(hostarq_daemon_string);
		params[1] = handle->shm_name;
		params[2] = fd_string;
		params[3] = handle->remote_ip;
		params[4] = udp_data_port_string;
		params[5] = udp_reset_port_string;
		params[6] = udp_data_local_port_string;
		params[7] = init_string;
		params[8] = const_cast<char*>(std::to_string(handle->unique_queues.size()).c_str());
		for (__u64 i = 0; i < handle->unique_queues.size(); ++i) {
			params[9 + i] = const_cast<char*>(queue_string.at(i).c_str());
			SCTRL_LOG_INFO("unique queue pid %s specified", queue_string.at(i).c_str());
		}
		params[9 + handle->unique_queues.size()] = NULL;
		execvp(params[0], params);
		free(params);
		perror("libhostarq tried to spawn HostARQ daemon");
		abort();
	} else if (handle->pid > 0 /* parent */) {
		int tmp;
		/* wait for child to change fd flag on finished init */
		while (true) {
			if (((tmp = fcntl(fd, F_GETFL)) == -1) && (errno == EAGAIN)) {
				continue;
			}
			if (tmp != flag) {
				/*flag was changed by child*/
				break;
			}
			int status;
			if (waitpid(handle->pid, &status, WNOHANG) != 0) {
				if (WIFEXITED(status)) {
					auto const returncode_raw = WEXITSTATUS(status);
					ExitCode const returncode{returncode_raw};
					std::string fail_reason;
					switch(returncode) {
						case ExitCode::UNSPECIFIED_FAILURE :
							fail_reason = "Unspecified error";
							break;
						case ExitCode::RESET_TIMEOUT :
							fail_reason = "FPGA reset timeout reached";
							break;
						case ExitCode::FPGA_SETTINGS_MISMATCH :
							fail_reason = "Settings mismatch between Host and FPGA";
							break;
						case ExitCode::MAX_RESENDS :
							fail_reason = "Maximum number of resends reached";
							break;
						case ExitCode::SUCCESS :
							fail_reason = "Success. oO?";
							break;
						default :
							fail_reason = "Unknown return code: " + std::to_string(returncode_raw);
							break;
					}
					throw std::runtime_error("HostARQ daemon terminated unexpectedly. Reason: " + fail_reason);
				} else if (WIFSIGNALED(status)) {
					int termsignal = WTERMSIG(status);
					throw std::runtime_error("HostARQ daemon terminated due to signal: " + std::to_string(termsignal));
				} else {
					throw std::runtime_error("HostARQ daemon terminated for unknown reason");
				}
			}
			usleep(HOSTARQ_PARENT_SLEEP_INTERVAL);
		};
		close(fd);
		//restore previous signal handler
		sigaction(HOSTARQ_FAIL_SIGNAL, &old_action, NULL);
	} else {
		perror("Could not fork HostARQ processes");
		abort();
	}
}


int
hostarq_close(struct hostarq_handle* handle)
{
	int ret_waitpid = 0, ret_access = 0;

	if (handle->pid == 0) {
		SCTRL_LOG_ERROR("pid isn't set");
		abort();
	}

	if (handle->shm_path == NULL) {
		SCTRL_LOG_ERROR("shm_path isn't set");
		abort();
	}

	/* check if shared memory file exists */
	if (access(handle->shm_path, F_OK) < 0) {
		SCTRL_LOG_ERROR("shm_path invalid: \"%s\"?", handle->shm_path);
		return -1;
	}

	/* request child process termination and wait 1s max */
	for (int i = 0; i < 1000 * 1000 / HOSTARQ_PARENT_SLEEP_INTERVAL; i++) {
		/* if child process didn't change state until now:
		 *   - send kill signal (could get lost)
		 *   - check for state change */
		if (ret_waitpid == 0) {
			kill(handle->pid, HOSTARQ_EXIT_SIGNAL);
			ret_waitpid = waitpid(handle->pid, NULL, WNOHANG);
			if (ret_waitpid < 0) {
				SCTRL_LOG_ERROR("Unexpected error checking process state: \"%i\"", errno);
				return -1;
			}
		}
		/* check shm file. It should be deleted under normal process termination */
		ret_access = access(handle->shm_path, F_OK);
		if ((ret_access < 0) && (errno != ENOENT)) {
			SCTRL_LOG_ERROR("Unexpected access error to shm file: \"%i\"", errno);
			return -1;
		}
		/* check for correct shutdown (i.e. process and shm file both gone) */
		if ((ret_waitpid > 0) && ((ret_access < 0) && (errno == ENOENT)))
			return ret_waitpid;
		/* if the process still exists, or if
		 * shm file still there => wait for a bit longer*/
		usleep(HOSTARQ_PARENT_SLEEP_INTERVAL);
	}
	if (ret_waitpid > 0) {
		SCTRL_LOG_ERROR("Shared memory file %s still existing after 1s wait; "
			"unlinking anyways...",
			handle->shm_path);
		if (unlink(handle->shm_path) < 0)
			perror("Failed to unlink shared mem file");
	} else {
		perror("Failed to kill hostarq process");
	}
	return -1;
}

#define PARAMETERISATION(Name, name)                                                               \
	template void hostarq_open<Name>(                                                              \
	    struct hostarq_handle * handle, char const* const hostarq_daemon_string);
#include "sctrltp/parameters.def"
} // namespace sctrltp
