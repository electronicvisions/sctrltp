#ifndef NCSIM
// this is NOT for NCSIM

#include <chrono>
#include <iostream>
#include <string>
#include <stdexcept>
#include <sstream>
#include <thread>

extern "C"{
#include "sctrltp/us_sctp_if.h"
#include "sctrltp/libhostarq.h"
}

#include "sctrltp/ARQStream.h"
#include "sctrltp/ARQFrame.h"

using namespace std::chrono_literals;
using namespace std::chrono;

namespace sctrltp {

struct ARQStreamImpl {
	struct sctp_descr * desc;
	hostarq_handle* handle;

	ARQStreamImpl(std::string name, std::string rip, bool reset) {
		// start HostARQ server (and reset link)
		handle = new hostarq_handle;
		hostarq_create_handle(handle, name.c_str(), rip.c_str(), reset /*reset HostARQ*/);
		hostarq_open(handle);
		desc = open_conn(name.c_str()); // name of software arq session
		if (!desc) {
			std::ostringstream ss;
			ss << "Error: Software ARQ Session " << name << " not found" << std::endl;
			throw std::runtime_error(name + ": cannot open connection to Software ARQ daemon");
		}
	}

	~ARQStreamImpl() {
		close_conn(desc); // TODO: we should check the return value?
		hostarq_close(handle);
		hostarq_free_handle(handle);
		delete handle;
	}
};

// sleep for given number of nano seconds; sleeping >= 1s not supported
void sleep(std::string const& name, nanoseconds const sleep)
{
	if (sleep > 1e9ns) {
		std::stringstream err_msg;
		err_msg << name + ": sleeping of " << sleep.count() << "ns >= 1s not supported";
		throw std::runtime_error(err_msg.str());
	}
	timespec towait;
	towait.tv_sec = 0;
	towait.tv_nsec = sleep.count();
	int ret;
	do {
		ret = nanosleep(&towait, NULL);
	} while (ret == EINTR); // sleep again if interrupted ;)
	if (ret > 0) {
		throw std::runtime_error(name + ": cannot sleep (ret was " + std::to_string(ret) + ")");
	}
}

ARQStream::ARQStream(
		std::string const name,
		std::string const,
		udpport_t,
		std::string const rip,
		udpport_t,
		bool reset) :
	name(name),
	rip(rip),
	max_wait_for_completion_upon_destruction_in_ms(500),
	pimpl(new ARQStreamImpl(name, rip, reset))
{
	drop_receive_queue(1ms, true);
}


ARQStream::ARQStream(std::string const name, bool reset) :
	name(name),
	rip(name),
	max_wait_for_completion_upon_destruction_in_ms(500),
	pimpl(new ARQStreamImpl(name, name, reset))
{
	drop_receive_queue(1ms, true);
}


ARQStream::~ARQStream() {
	STATIC_ASSERT(sizeof(__u64) == sizeof(uint64_t));

	// ECM (2018-02-14): send one last packet to flush (there's no dedicated tear down)
	{
		packet curr_pck;
		curr_pck.pid = PTYPE_FLUSH;
		curr_pck.len = 1;
		curr_pck.pdu[0] = 0;
		// trigger send directly
		send(curr_pck, Mode::FLUSH);
	}

	// ECM (2018-02-09): flush may throw, catch it and log error (related to bug #2731)
	bool catched_exception = false;
	try {
		flush();
		auto start_of_flush = std::chrono::steady_clock::now();
		while (!all_packets_sent()) {
			auto waited_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - start_of_flush).count();
			if (waited_in_ms > max_wait_for_completion_upon_destruction_in_ms) {
				throw std::runtime_error("Wait for completion of transfer timed out");
			} else {
				using namespace std::chrono_literals;
				std::this_thread::sleep_for(50ms);
			}
		}
	} catch (std::exception const& e) {
		catched_exception = true;
		std::cerr << "ARQStream::~ARQStream: " << e.what() << std::endl;
	}

	// ECM (2018-02-12): let's clean up in any case but abort if exception was handled above
	delete pimpl;
	pimpl = NULL;

	if (catched_exception) {
		abort();
	}
}


void ARQStream::start() {}
void ARQStream::stop() {}
void ARQStream::trigger_send() {}
void ARQStream::trigger_receive() {}

std::string ARQStream::get_remote_ip() const {
	return rip;
}


bool ARQStream::send(packet t, Mode mode) {
	__s32 ret;
	struct buf_desc buffer;

	// change to network byte order (like ARQStream does)
	for (size_t i = 0; i < t.len; i++)
		t.pdu[i] = htobe64(t.pdu[i]);

	// they cannot fail in blocking mode
	acq_buf(pimpl->desc, &buffer, 0); // TODO: add mode handling
	init_buf(&buffer);

	// will fail if previous packet wasn't flushed
	ret = append_words(&buffer, t.pid, t.len, reinterpret_cast<__u64*>(&t.pdu[0]));
	if (ret < 0)
		throw std::runtime_error(name + ": payload error");

	ret = send_buf(pimpl->desc, &buffer, mode);
	if (ret < 0)
		throw std::runtime_error(name + ": send error");

	return true;
}


bool ARQStream::receive(packet& t, Mode mode) {
	__s32 ret;
	struct buf_desc buffer;

	ret = recv_buf(pimpl->desc, &buffer, mode);
	if (ret == SC_EMPTY)
		return false;
	else if (ret < 0)
		throw std::runtime_error(name + ": receive error");

	t.pid = sctpreq_get_typ(buffer.arq_sctrl);
	t.len = sctpreq_get_len(buffer.arq_sctrl);

	// copy and change to host byte order (like ARQStream does)
	for (size_t i = 0; i < t.len; i++)
		t.pdu[i] = be64toh(buffer.payload[i]);

	ret = rel_buf(pimpl->desc, &buffer, 0);
	if (ret < 0)
		throw std::runtime_error(name + ": release error");

	return true;
}


void ARQStream::flush() {
	// clear TX cache
	__s32 ret = send_buf(pimpl->desc, NULL, MODE_FLUSH);
	if (ret < 0)
		throw std::runtime_error(name + ": flushing error");
}


bool ARQStream::received_packet_available() {
	return ! static_cast<bool>(rx_queues_empty(pimpl->desc));
}

size_t ARQStream::drop_receive_queue(microseconds timeout, bool with_control_packet)
{
	size_t dropped_words = 0;
	microseconds accumulated_sleep(0);
	size_t sleep_interval_idx = 0;

	// sleep timings in us (back-off to longer times; don't use >= 1s!)
	std::vector<microseconds> const sleep_intervals = {5us,    10us,   50us,    100us,   500us,
	                                                   1000us, 5000us, 10000us, 50000us, 100000us};
	packet my_packet;
	// we need a random seed that differs between each experiment run
	size_t const seed = duration_cast<milliseconds>(
							system_clock::now().time_since_epoch())
							.count();
	std::srand(seed);
	size_t const magic_number = std::rand();

	if (with_control_packet) {
		my_packet.pid = PTYPE_LOOPBACK;
		my_packet.len = 1;
		my_packet.pdu[0] = magic_number;
		send(my_packet, Mode::FLUSH);

		while (accumulated_sleep < timeout) {
			if (!all_packets_sent()) {
				sleep(name, duration_cast<nanoseconds>(sleep_intervals[sleep_interval_idx]));
				accumulated_sleep += sleep_intervals[sleep_interval_idx];
				if (sleep_interval_idx + 1 < sleep_intervals.size()) {
					sleep_interval_idx++;
				}
			} else {
				break;
			}
		}
		if (!all_packets_sent()) {
			throw std::runtime_error(name + ": not all packets send after timeout");
		}
	}

	sleep_interval_idx = 0;
	accumulated_sleep = 0ms;
	bool loopback_found = false;
	while (!loopback_found && accumulated_sleep < timeout) {
		if (received_packet_available()) {
			// fetch packet, analyse if loopback else drop
			sleep_interval_idx = 0;
			receive(my_packet);
			if (with_control_packet && my_packet.pid == PTYPE_LOOPBACK) {
				if (my_packet.len != 1) {
					throw std::runtime_error(
					    name + ": received loopback packet size larger 1: " +
					    std::to_string(my_packet.len));
				}
				if (my_packet.pdu[0] != magic_number) {
					throw std::runtime_error(
					    name + ": received magic word " + std::to_string(my_packet.pdu[0]) +
					    " differs from sent word " + std::to_string(magic_number));
				}
				if (received_packet_available()) {
					throw std::runtime_error(
					    name + ": still packets available after loopback received");
				}
				loopback_found = true;
				break;
			}
			dropped_words += my_packet.len;
		} else {
			sleep(name, sleep_intervals[sleep_interval_idx]);
			accumulated_sleep += sleep_intervals[sleep_interval_idx];
			if (sleep_interval_idx + 1 < sleep_intervals.size()) {
				sleep_interval_idx++;
			}
		}
	}

	if (with_control_packet && !loopback_found) {
		throw std::runtime_error(
		    name + ": no loopback packet response after timeout of " +
		    std::to_string(timeout.count()) + "ms");
	}

	return dropped_words;
}


bool ARQStream::all_packets_sent() {
	return static_cast<bool>(tx_queues_empty(pimpl->desc));
}


bool ARQStream::send_buffer_full() {
	return static_cast<bool>(tx_queues_full(pimpl->desc));
}


#endif // !NCSIM

} // namespace sctrltp
