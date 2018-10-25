/* Library for HostARQ communication */

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <linux/types.h>

#include "sctrltp/sctrltp_defines.h"

#define HOSTARQ_EXIT_SIGNAL SIGUSR2
#define HOSTARQ_FAIL_SIGNAL SIGPIPE


namespace sctrltp {

/** `hostarq_handle` represents a connection to a HostARQ daemon. */
struct hostarq_handle
{
	pid_t pid;
	char* shm_name;
	char* shm_path;
	char* remote_ip;
	__u16 udp_data_port;
	__u16 udp_reset_port;
	__u16 udp_data_local_port;
	bool init;
	unique_queue_set_t unique_queues;
};


/** `hostarq_create_handle` constructs a handle.
 *
 * @param handle A valid pointer to a struct hostarq_handle which will
 *               be filled by this function.
 * @param shm_name A filename which will represent the shm region
 *                 (located under `/dev/shm`).
 * @param remote_ip The IP(v4) address as a char string.
 * @param udp_data_port The FPGA's HostARQ-via-UDP port.
 * @param udp_reset_port The FPGA's UDP port for HostARQ reset frames.
 * @param udp_data_local_port The local UDP port used.
 * @param init If true, a HostARQ reset frame is sent to the FPGA.
 * @param unique_queues set of unique packet queues.
 */
void hostarq_create_handle(
	struct hostarq_handle* handle,
	char const shm_name[],
	char const remote_ip[],
	__u16 const udp_data_port,
	__u16 const udp_reset_port,
	__u16 const udp_data_local_port,
	bool const init,
	unique_queue_set_t unique_queues);


/** `hostarq_free_handle` frees the data structure
 *
 * @param handle The contents of the handle are freed. In case of a heap-allocated
 *               structure the user is required to free the structure afterwards.
 */
void hostarq_free_handle(struct hostarq_handle* handle);


/** `hostarq_open` spawns the HostARQ daemon and sets up a shared memory-based
 * communication mechanism between the calling software and the
 * protocol-handling threads (RX/TX/RETRANSMISSION).
 *
 * @param hostarq_handle A fresh handle containing shm_name, remote_ip and init.
 *
 * @return The pid and shm_path are set after this call.
 */
template<typename P>
void hostarq_open(
	struct hostarq_handle* handle, char const* hostarq_daemon_string = "hostarq_daemon");


/** `hostarq_close` tries to close the HostARQ connection.
 *
 * It sends the HOSTARQ_EXIT_SIGNAL to the hostarq_daemon and
 * waits with a 1s timeout for its termination.
 *
 * @param hostarq_handle The pid and shm_path are used to stop the daemon and
 *                       perform cleanup.
 *
 * @return If the shutdown succeeded, the PID is returned;
 *         if the shm file is gone, return 0;
 *         if the shm file could not be deleted, return -1.
 */
int hostarq_close(struct hostarq_handle* handle);

} // namespace sctrltp
