#include "sctrltp/ARQStream.h"
#ifndef NCSIM
// this is NOT for NCSIM

#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeindex>
#include <unordered_map>

#include "sctrltp/us_sctp_if.h"
#include "sctrltp/libhostarq.h"

#include "sctrltp/ARQFrame.h"

using namespace std::chrono_literals;
using namespace std::chrono;

namespace {

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


// create a unique name for a ARQStream connection
std::string create_name(sctrltp::ARQStreamSettings const s)
{
	return s.ip + "-" + std::to_string(s.port_data) + "-" + std::to_string(s.port_reset);
}

} // namespace

namespace sctrltp {

static std::unordered_map<std::type_index, std::string> hostarq_daemon_names = {
#define PARAMETERISATION(Name, name) {std::type_index(typeid(Name)), "hostarq_daemon_" #name},
#include "sctrltp/parameters.def"
};

template<typename P>
struct ARQStreamImpl {
	sctp_descr<P> * desc;
	hostarq_handle* handle;
	std::string name;
	unique_queue_set_t unique_queue_set;

	ARQStreamImpl(
	    std::string name,
	    std::string rip,
	    udpport_t port_data,
	    udpport_t port_reset,
	    udpport_t local_port_data,
	    unique_queue_set_t unique_queues,
	    bool reset) :
	    name(name), unique_queue_set(unique_queues)
	{
		if (name.empty() || rip.empty()) {
			throw std::runtime_error("ARQStream name and IP have to be set");
		}
		// start HostARQ server (and reset link)
		handle = new hostarq_handle;
		hostarq_create_handle(
		    handle, name.c_str(), rip.c_str(), port_data, port_reset, local_port_data, reset,
		    unique_queue_set);
		hostarq_open<P>(handle, hostarq_daemon_names.at(std::type_index(typeid(P))).c_str());
		desc = open_conn<P>(name.c_str()); // name of software arq session
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

	void fill_and_send_buf(sctrltp::packet<P> const& t, typename ARQStream<P>::Mode mode)
	{
		buf_desc<P> buffer;
		__s32 ret;

		// they cannot fail in blocking mode
		acq_buf(desc, &buffer, 0); // TODO: add mode handling
		init_buf(&buffer);

		// will fail if previous packet wasn't flushed
#if (__GNUC__ >= 9)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif
		ret = append_words(&buffer, t.pid, t.len, reinterpret_cast<__u64 const*>(t.pdu));
#if (__GNUC__ >= 9)
#pragma GCC diagnostic pop
#endif
		if (ret < 0)
			throw std::runtime_error(name + ": payload error");

		ret = send_buf(desc, &buffer, mode);
		if (ret < 0)
			throw std::runtime_error(name + ": send error");
	}
};


template <typename P>
ARQStream<P>::ARQStream(
    std::string const,
    std::string const,
    udpport_t,
    std::string const rip,
    udpport_t,
    bool const reset) :
    name(create_name(ARQStreamSettings{.ip = rip})),
    rip(rip),
    max_wait_for_completion_upon_destruction_in_ms(500),
    pimpl(new ARQStreamImpl<P>(
        name,
        rip,
        ARQStreamSettings().port_data,
        ARQStreamSettings().port_reset,
        ARQStreamSettings().local_port_data,
        unique_queue_set_t(),
        reset))
{
	drop_receive_queue(400ms, true);
}

template <typename P>
ARQStream<P>::ARQStream(std::string const rip, bool const reset) :
    name(create_name(ARQStreamSettings{.ip = rip})),
    rip(rip),
    max_wait_for_completion_upon_destruction_in_ms(500),
    pimpl(new ARQStreamImpl<P>(
        name,
        rip,
        ARQStreamSettings().port_data,
        ARQStreamSettings().port_reset,
        ARQStreamSettings().local_port_data,
        unique_queue_set_t(),
        reset))
{
	drop_receive_queue(400ms, true);
}

template<typename P>
ARQStream<P>::ARQStream(ARQStreamSettings const settings) :
    name(create_name(settings)),
    rip(settings.ip),
    max_wait_for_completion_upon_destruction_in_ms(settings.destruction_timeout.count()),
    pimpl(new ARQStreamImpl<P>(
        name,
        settings.ip,
        settings.port_data,
        settings.port_reset,
        settings.local_port_data,
        settings.unique_queues,
        settings.reset))
{
	drop_receive_queue(settings.init_flush_timeout, settings.init_flush_lb_packet);
}

template<typename P>
ARQStream<P>::~ARQStream() {
	static_assert(sizeof(__u64) == sizeof(uint64_t), "Non-matching typedefs");

	// ECM (2018-02-14): send one last packet to flush (there's no dedicated tear down)
	{
		packet<P> curr_pck;
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


template<typename P>
void ARQStream<P>::start() {}

template<typename P>
void ARQStream<P>::stop() {}

template<typename P>
void ARQStream<P>::trigger_send() {}

template<typename P>
void ARQStream<P>::trigger_receive() {}

template<typename P>
std::string ARQStream<P>::get_remote_ip() const {
	return rip;
}

template <typename P>
void ARQStream<P>::send_direct(packet<P> const& t, Mode const mode)
{
	pimpl->fill_and_send_buf(t, mode);
}

template <typename P>
bool ARQStream<P>::send(packet<P> t, Mode const mode)
{
	// change to network byte order (like ARQStream does)
	for (size_t i = 0; i < t.len; i++)
		t.pdu[i] = htobe64(t.pdu[i]);

	send_direct(t, mode);

	return true;
}

template<typename P>
bool ARQStream<P>::receive(packet<P>& t, Mode mode) {
	__s32 ret;
	buf_desc<P> buffer;

	ret = recv_buf<P>(pimpl->desc, &buffer, mode);
	if (ret == SC_EMPTY)
		return false;
	else if (ret < 0)
		throw std::runtime_error(name + ": receive error");

	t.pid = sctpreq_get_typ(buffer.arq_sctrl);
	t.len = sctpreq_get_len(buffer.arq_sctrl);

	// copy and change to host byte order (like ARQStream does)
	for (size_t i = 0; i < t.len; i++)
		t.pdu[i] = be64toh(buffer.payload[i]);

	ret = rel_buf<P>(pimpl->desc, &buffer, 0);
	if (ret < 0)
		throw std::runtime_error(name + ": release error");

	return true;
}

template<typename P>
bool ARQStream<P>::receive(packet<P>& t, packetid_t pid, Mode mode)
{
	__s32 ret;
	struct buf_desc<P> buffer;

	if (!has_unique_queue(pid)) {
		throw std::runtime_error(
		    name + ": There exists no unique queue of pid " + std::to_string(pid));
	}

	size_t const idx = get_unique_queue_idx(pid);
	ret = recv_buf<P>(pimpl->desc, &buffer, mode, idx);
	if (ret == SC_EMPTY)
		return false;
	else if (ret < 0)
		throw std::runtime_error(
		    name + ": receive error for unique queue of pid " + std::to_string(pid));

	t.pid = sctpreq_get_typ(buffer.arq_sctrl);
	t.len = sctpreq_get_len(buffer.arq_sctrl);

	// copy and change to host byte order (like ARQStream does)
	for (size_t i = 0; i < t.len; i++)
		t.pdu[i] = be64toh(buffer.payload[i]);

	ret = rel_buf<P>(pimpl->desc, &buffer, 0);
	if (ret < 0)
		throw std::runtime_error(name + ": release error");

	return true;
}

template<typename P>
void ARQStream<P>::flush() {
	// clear TX cache
	__s32 ret = send_buf<P>(pimpl->desc, NULL, MODE_FLUSH);
	if (ret < 0)
		throw std::runtime_error(name + ": flushing error");
}

template<typename P>
bool ARQStream<P>::received_packet_available() const
{
	return !static_cast<bool>(rx_queue_empty(pimpl->desc)) ||
	       !static_cast<bool>(rx_recv_buf_empty(pimpl->desc));
}

template<typename P>
bool ARQStream<P>::received_packet_available(packetid_t pid) const
{
	if (!has_unique_queue(pid)) {
		throw std::runtime_error(
		    name + ": There exists no unique queue of pid " + std::to_string(pid));
	}
	size_t const idx = get_unique_queue_idx(pid);
	return !static_cast<bool>(rx_queue_empty(pimpl->desc, idx)) ||
	       !static_cast<bool>(rx_recv_buf_empty(pimpl->desc, idx));
}

template<typename P>
size_t ARQStream<P>::drop_receive_queue(microseconds timeout, bool with_control_packet)
{
	size_t dropped_words = 0;
	microseconds accumulated_sleep(0);
	size_t sleep_interval_idx = 0;

	// sleep timings in us (back-off to longer times; don't use >= 1s!)
	std::vector<microseconds> const sleep_intervals = {5us,    10us,   50us,    100us,   500us,
	                                                   1000us, 5000us, 10000us, 50000us, 100000us};
	packet<P> my_packet;
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
			throw std::runtime_error(
				name + ": not all packets send after timeout of " +
				std::to_string(duration_cast<milliseconds>(timeout).count()) + "ms");
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
		    std::to_string(duration_cast<milliseconds>(timeout).count()) + "ms");
	}

	return dropped_words;
}

template<typename P>
bool ARQStream<P>::all_packets_sent() {
	return static_cast<bool>(tx_queue_empty<P>(pimpl->desc));
}

template<typename P>
bool ARQStream<P>::send_buffer_full() {
	return static_cast<bool>(tx_queue_full<P>(pimpl->desc));
}

template<typename P>
bool ARQStream<P>::has_unique_queue(packetid_t pid) const
{
	return (pimpl->unique_queue_set.count(pid) > 0);
}

template<typename P>
size_t ARQStream<P>::get_unique_queue_idx(packetid_t pid) const
{
	assert(pimpl->unique_queue_set.count(pid) > 0);
	for (__u64 idx = 0; idx < pimpl->desc->trans->unique_queue_map.size; ++idx) {
		if (pid == pimpl->desc->trans->unique_queue_map.type[idx]) {
			return idx;
		}
	}
	throw std::runtime_error("Queue definitions of ARQStream and sctp_core do not match.");
}

template<typename P>
std::string ARQStream<P>::get_name() {
	return name;
}

#define PARAMETERISATION(Name, name) template class ARQStream<Name>;
#include "sctrltp/parameters.def"

#endif // !NCSIM

} // namespace sctrltp
