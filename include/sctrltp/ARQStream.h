#pragma once

#include <boost/asio/ip/address_v4.hpp>

namespace sctrltp {

// fwd decls
class packet;
class ARQStreamImpl;


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

	~ARQStream();

	// for compatibility with NCSIM, no-op on real hw
	void start();
	void stop();
	void trigger_send();
	void trigger_receive();

	std::string get_remote_ip() const;

	// queue packet or false (no false on hw, it blocks)
	bool send(packet, Mode mode = FLUSH);       

	// receive packet or false (no false on hw, it blocks)
	bool receive(packet&, Mode mode = NONBLOCK);

	// TODO: add queue cmd

	// no-op in simulation, flushed tx cache
	void flush();

	// check whether a packet is in receive buffer
	bool received_packet_available();

	// check whether all packets have been sent from sender buffer
	bool all_packets_sent();

	// notify that send buffer is full
	bool send_buffer_full();


private:
	std::string name;
	std::string rip;
#ifdef NCSIM
// NCSIM-based testmodes want it public
public:
#endif
	ARQStreamImpl * pimpl; // no extra deps here, plain ptr!

	// not copyable
	ARQStream(ARQStream const &);

	// not assignable
	ARQStream& operator=(ARQStream const &);

};

} // namespace sctrltp
