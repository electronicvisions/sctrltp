#pragma once

#include <chrono>
#include <boost/asio/ip/address_v4.hpp>

#include "sctrltp/sctrltp_defines.h"

namespace sctrltp {

// fwd decls
template<typename P>
class packet;
template<typename P>
class ARQStreamImpl;

struct ARQStreamSettings
{
	// IPv4 address of remote link partner; format "x.x.x.x"
	std::string ip;
	// on startup send packet to reset SEQ/ACK and check parameter mismatch
	bool reset = true;
	// on construction send loopback packet to check receive queue flushing status
	bool init_flush_lb_packet = true;
	// timeout for receive queue flushing on construction
	std::chrono::microseconds init_flush_timeout = std::chrono::milliseconds(400);
	// timeout to keep waiting for packets when destructing
	std::chrono::milliseconds destruction_timeout = std::chrono::milliseconds(500);
};

template<typename P = Parameters<>>
class ARQStream {
public:
	typedef boost::asio::ip::address_v4 ip_t;
	typedef unsigned short udpport_t;

	enum Mode {
		// from Software ARQ!
		NOTHING  = 0x00,
		NONBLOCK = 0x02,
		FLUSH    = 0x04
	};

	ARQStream(
		std::string const name,
		std::string const source_ip,  // unused on hw
		udpport_t source_port,        // unused on hw
		std::string const target_ip,
		udpport_t target_port,        // unused on hw
		bool const reset = true
	);

	ARQStream(std::string const name, bool const reset = true);

	ARQStream(ARQStreamSettings const settings);

	~ARQStream();

	// for compatibility with NCSIM, no-op on real hw
	void start();
	void stop();
	void trigger_send();
	void trigger_receive();

	std::string get_remote_ip() const;

	// queue packet or false (no false on hw, it blocks)
	bool send(packet<P>, Mode mode = FLUSH);

	// receive packet or false (no false on hw, it blocks)
	bool receive(packet<P>&, Mode mode = NONBLOCK);

	// TODO: add queue cmd

	// no-op in simulation, flushed tx cache
	void flush();

	// check whether a packet is in receive buffer
	bool received_packet_available();

	// drops all incoming packets. Returns when timeout since last received packet is reached
	// returned value is number of dropped words
	// if control packet is set, a loopback packet will be sent to FPGA and checked if packet is received
	size_t drop_receive_queue(
	    std::chrono::microseconds timeout = std::chrono::microseconds(10000),
	    bool with_control_packet = false);

	// check whether all packets have been sent from sender buffer
	bool all_packets_sent();

	// notify that send buffer is full
	bool send_buffer_full();


private:
	std::string name;
	std::string rip;
	int const max_wait_for_completion_upon_destruction_in_ms;

#ifdef NCSIM
// NCSIM-based testmodes want it public
public:
#endif
	ARQStreamImpl<P> * pimpl; // no extra deps here, plain ptr!

	// not copyable
	ARQStream(ARQStream const &);

	// not assignable
	ARQStream& operator=(ARQStream const &);

};

} // namespace sctrltp
