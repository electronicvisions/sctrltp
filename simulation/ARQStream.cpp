#ifndef NCSIM
#error This code is FPGA simulation code... only!
#endif

/* very simple ARQ implementation written to test Vitali's VHDL code */

#include "ARQStreamWrap.h"

#include <cassert>
#include <iostream>

#include <boost/array.hpp>
#include <boost/asio/ip/address.hpp>

#include <ctime>

extern "C" {
#include <stdint.h>    // uint32_t
#include <arpa/inet.h> // htonl, etc.
}

#include "helpers.h"

#include "eth_utils.h"

namespace sctrltp
{

// register ARQ instance for simulation -> required for send/receive trigger calls
#ifdef NCSIM
ARQStream<ParametersFcpBss1>* arq_stream_ptr = NULL;
#endif


template <typename P>
ARQStream<P>::ARQStream(std::string const name, std::string const source_ip, udpport_t source_port,
                     std::string const target_ip, udpport_t target_port, bool reset)
    : name(name), max_wait_for_completion_upon_destruction_in_ms(0), pimpl(new ARQStreamImpl<P>), rip(target_ip)
{
	pimpl->name = name;
	pimpl->sender_drop_rate = 0.0;
	pimpl->receiver_drop_rate = 0.0;
	pimpl->source_ip = ip_t::from_string(source_ip);
	pimpl->source_port = source_port;
	pimpl->target_ip = ip_t::from_string(target_ip);
	pimpl->target_port = target_port;
	// sequence/ack numbers
	pimpl->ack = SEQ_MAX;
	pimpl->rack = SEQ_MAX;
	pimpl->rseq = SEQ_MAX;
	pimpl->last_seq_inserted = SEQ_MAX;
	pimpl->last_time_racked = SC_ZERO_TIME;
	pimpl->timeout = sc_time(1, SC_US);
	pimpl->resend_timeout = sc_time(300, SC_US);
	// simulator "network" interface
	pimpl->pEthIf = eth_if::getInstance();
	// packet buffers
	pimpl->send_buffers = new typename ARQStreamImpl<P>::template window_buffer<P::MAX_WINSIZ>;
	pimpl->receive_buffers = new typename ARQStreamImpl<P>::template window_buffer<P::MAX_WINSIZ>;
	assert((SEQ_MAX + 1) / 2 >= P::MAX_WINSIZ);

// set interface to RGMII for FACETS board
// (ECM: proudly copied from some other test,
// JP: but for Kintex7, we use SGMII..)
#ifndef FPGA_BOARD_BS_K7
	pimpl->pEthIf->setIfType(ETH_IF_RGMII);
#endif
	pimpl->pEthIf->setIfTXMode(true); // enable blocking send method

	std::cout << "Init: " << pimpl->eth_soft.init(target_port) << std::endl;

#ifdef NCSIM
	arq_stream_ptr = this;
	std::cout << "Blocking mode: " << (pimpl->pEthIf->isTXBlocking()) << std::endl;

	eth_dbg::setDbgLevel(ETH_COMPONENT_RAW, ETH_DBG_WARN);
	eth_dbg::setDbgLevel(ETH_COMPONENT_UDP, ETH_DBG_WARN);
	eth_dbg::setDbgLevel(ETH_COMPONENT_ICMP, ETH_DBG_WARN);
	eth_dbg::setDbgLevel(ETH_COMPONENT_ARP, ETH_DBG_WARN);
	eth_dbg::setDbgLevel(ETH_COMPONENT_IPV4, ETH_DBG_WARN);
	eth_dbg::setDbgLevel(ETH_COMPONENT_MDIO, ETH_DBG_WARN);
	eth_dbg::setDbgLevel(ETH_COMPONENT_SOCK, ETH_DBG_WARN);
#endif
}


template <typename P>
ARQStream<P>::~ARQStream()
{
	stop();

#ifdef NCSIM
	arq_stream_ptr = NULL; // assume there is only one ARQStream instance
#endif
}


template <typename P>
void ARQStream<P>::start()
{
	pimpl->running = true;

	// reset the arq and ram
	wait(110.0, SC_US); // wait for memory to initialize
	uint32_t payload = HW_HOSTARQ_MAGICWORD;
	pimpl->eth_soft.sendUDP(pimpl->target_ip.to_ulong(), ARQStreamSettings().port_reset, &payload, sizeof(payload));
	wait(10.0, SC_US); // wait for memory to initialize
	while (!received_packet_available()) {
		wait(10.0, SC_US); // wait for reset response packet
	}
	packet<P> response;
	receive(response, ARQStream<P>::Mode::NONBLOCK);
	if (response.pid != PTYPE_CFG_TYPE) {
		throw std::runtime_error(
			"wrong reset response packet type: " + std::to_string(response.pid));
	}
	if (response.len < 3) {
		throw std::runtime_error(
			"reset response packet too short: " + std::to_string(response.len) +
			", should be at least: 3");
	}
	if (response.pdu[0] != P::MAX_NRFRAMES) {
		throw std::runtime_error(
			"mismatch between FPGA(" + std::to_string(response.pdu[0]) + ") and host(" +
			std::to_string(P::MAX_NRFRAMES) + ") sequence size");
	}
	if (response.pdu[1] != P::MAX_WINSIZ) {
		throw std::runtime_error(
			"mismatch between FPGA(" + std::to_string(response.pdu[1]) + ") and host(" +
			std::to_string(P::MAX_WINSIZ) + ") window size");
	}
	if (response.pdu[2] != P::MAX_PDUWORDS) {
		throw std::runtime_error(
			"mismatch between FPGA(" + std::to_string(response.pdu[2]) + ") and host(" +
			std::to_string(P::MAX_PDUWORDS) + ") max pdu size");
	}
}


template <typename P>
void ARQStream<P>::stop()
{
	if (!pimpl->running)
		return;
	pimpl->running = false;
}


/* send: copy packet to send buffer and return true, else return false to indicate "block" */
template <typename P>
bool ARQStream<P>::send(packet<P> tmp, ARQStream<P>::Mode)
{

	return pimpl->send(tmp);
}


/* receive: pop packet from receive buffer and return true, else return false*/
template <typename P>
bool ARQStream<P>::receive(packet<P>& out, ARQStream<P>::Mode mode)
{
	if (!(mode & ARQStream<P>::NONBLOCK))
		throw std::runtime_error("only NONBLOCK mode supported");
	return pimpl->receive(out);
}


template <typename P>
bool ARQStream<P>::received_packet_available() const
{
	return (!pimpl->receive_buffers->window_empty());
}


template <typename P>
bool ARQStream<P>::all_packets_sent()
{
	return (pimpl->send_buffers->window_empty());
}


template <typename P>
bool ARQStream<P>::send_buffer_full()
{
	return (pimpl->send_buffers->window_full());
}


template <typename P>
void ARQStream<P>::trigger_send()
{
	pimpl->trigger_send();
}


template <typename P>
void ARQStream<P>::trigger_receive()
{
	pimpl->trigger_receive();
}

template <typename P>
std::string ARQStream<P>::get_remote_ip() const {
	return rip;
}

#ifdef NCSIM
void arq_stream_trigger::trigger_func_send()
{
	while (arq_stream_ptr == NULL)
		wait(10.0, SC_US);

	std::cout << "arq_stream_trigger: Starting send trigger at " << (int)(sc_simulation_time())
	          << "ns" << std::endl;

	arq_stream_ptr->pimpl->trigger_send_running = false;
	while (true) {
		wait(10.0, SC_US);
		if (arq_stream_ptr != NULL) {
			arq_stream_ptr->trigger_send();
		}
	}
}

void arq_stream_trigger::trigger_func_rec()
{
	while (true) {
		wait(1.0, SC_US);
		if (arq_stream_ptr != NULL) {
			arq_stream_ptr->trigger_receive();
		}
	}
}


template <typename P>
bool ARQStreamImpl<P>::send(packet<P> tmp)
{
	if (send_buffers->window_full()) {
		std::cout << name << " window full during send()" << std::endl;
		return false;
	}

	typename packet<P>::seq_t latest = next_seq<P>(last_seq_inserted);
	tmp.seq = latest;
	send_buffers->push_back(tmp);
	last_seq_inserted = latest;

	return true;
}

template <typename P>
bool ARQStreamImpl<P>::receive(packet<P>& out)
{
	if (!running)
		throw std::runtime_error("not running anymore!");
	if (!receive_buffers->window_empty()) {
		out = receive_buffers->pop_front(); // return copy!
		std::cout << " received new data from software ARQ " << std::endl;
		return true;
	}
	return false;
}

template <typename P>
void ARQStreamImpl<P>::trigger_send()
{
	if (!running)
		return;

	if (trigger_send_running)
		std::cout << "ARQStreamImpl WARNING: calling trigger_send() when it is still running -> "
		             "must be triggered from two independent SC_THREADS, which is not supported."
		          << std::endl;

	trigger_send_running = true;

	if (!running) {
		std::cerr << "arq_stream_trigger:: not running but called" << std::endl;
		return;
	}

	static typename packet<P>::seq_t seq = SEQ_MAX;
	static typename packet<P>::seq_t next_seq_to_send = 0;
	static sc_time last_time_sent = SC_ZERO_TIME;
	static size_t idx = 0;

	// if last received ack is in current send window slide send window
	if (in_window<P>(rack, seq, seq + send_buffers->window_size())) {
		std::cout << name << " rack was " << std::dec << rack << " dist " << dist_window<P>(rack, seq)
		          << std::endl;
		assert(dist_window<P>(rack, seq) <= P::MAX_WINSIZ);
		std::cout << name << " shifting window by " << dist_window<P>(rack, seq) << std::endl;
		send_buffers->drop_front(dist_window<P>(rack, seq));
		idx -= dist_window<P>(rack, seq);
		seq = rack;
		idx = 0;
	}

	bool timeouted = (last_time_racked + resend_timeout) < sc_time_stamp();
	if (timeouted) {
		std::cout << "resend timeout triggered!" << std::endl;
		idx = 0;
	}

	// packets exist
	size_t pending = send_buffers->window_size();
	if (pending) {
		std::cout << name << " has " << pending << " non-acked packets (idx is " << idx
		          << ") at time " << sc_time_stamp() << std::endl;
		// send all pending packets
		for (; idx < pending; idx++) {

			packet<P>& pckt = send_buffers->window_at(idx);
			if (!timeouted && pckt.seq != next_seq_to_send) {
				std::cout << name << " skipping (not sending): " << pckt.seq << ", expected "
				          << next_seq_to_send << std::endl;
				continue;
			}

			pckt.ack = rseq;
			std::cout << name << " sending pending packet (seq: " << pckt.seq
			          << ", ack: " << pckt.ack << " at buffer " << idx
			          << "th position, size: " << pckt.size() << ")" << std::endl;
			next_seq_to_send = next_seq<P>(pckt.seq);
			if (rand() < RAND_MAX * (1.0 - sender_drop_rate)) { // random dropping ;)
				std::cout << "called sendUDP with " << target_ip.to_ulong() << " " << target_port
				          << " size " << pckt.size() << std::endl;

				// copy because of sendUDP fuckup
				packet<P> pckt_copy((pckt));

				double start_send_t = sc_simulation_time();
				std::cout << "sending ARQ packet with seq. " << std::dec << pckt_copy.seq
				          << " at time " << (int)(sc_simulation_time()) << "ns" << std::endl;

				// convert to network byte order
				pckt_copy.ack = htonl(pckt_copy.ack);
				pckt_copy.seq = htonl(pckt_copy.seq);
				pckt_copy.pid = htons(pckt_copy.pid);
				size_t len = pckt_copy.len; // save for loop
				pckt_copy.len = htons(pckt_copy.len);

				for (size_t idx = 0; idx < len; idx++)
					pckt_copy.pdu[idx] = htobe64(pckt_copy.pdu[idx]);

				const size_t min_pckt_size = 700; // Bytes

				eth_soft.sendUDP(target_ip.to_ulong(), target_port,
				                 reinterpret_cast<void*>(&pckt_copy),
				                 (pckt.size() < min_pckt_size ? min_pckt_size : pckt.size()));
				std::cout << "sendUDP returned at time " << (int)(sc_simulation_time())
				          << "ns (difference: " << (sc_simulation_time() - start_send_t) << "ns)"
				          << std::endl;
			} else {
				std::cout << name << " randomly drops: (" << pckt.seq << ", " << pckt.ack << ", v"
				          << ")" << std::endl;
			}
			ack = pckt.ack;
			last_time_sent = sc_time_stamp();

			if (timeouted) {
				// reset timeout (no rack received)
				last_time_racked = sc_time_stamp();
				std::cout << name << " updated last_time_racked to " << last_time_racked
				          << std::endl;
				// we did first resent above, break out!
				break;
			}
		}
	} else {
		last_time_racked = sc_time_stamp();
	}

	if (last_time_sent + timeout < sc_time_stamp()) {
		// nothing sent but ack timeout elasped => send ack-only frame
		typename packet<P>::seq_t l_rseq = rseq;
		if (ack != l_rseq || !acked) {
			std::cout << name << " acking for " << l_rseq << " (ack was " << ack << ")"
			          << std::endl;
			ack_frame<P> tmp;
			tmp.ack = htonl(l_rseq);

			eth_soft.sendUDP(target_ip.to_ulong(), target_port, reinterpret_cast<void*>(&tmp),
			                 sizeof(ack_frame<P>));
			// update ack
			ack = l_rseq;
		}
		last_time_sent = sc_time_stamp();
	}

	acked = true;

	trigger_send_running = false;
}


template <typename P>
void ARQStreamImpl<P>::trigger_receive()
{
	if (!eth_soft.hasReceivedUDP()) {
		return;
	}

	rx_entry_t* rec_data = new rx_entry_t();
	if (!eth_soft.receiveUDP(*rec_data)) {
		throw std::runtime_error("WTF?");
	} else {
		std::cout << name << " found new frame with " << std::dec << rec_data->pData.size()
		          << " bytes" << std::endl;
	}


	if (rec_data->uiSourceIP != target_ip.to_ulong())
		throw std::runtime_error("got wrong ip");
	if (rec_data->uiSourcePort != target_port)
		throw std::runtime_error("got wrong port");
	// check for port?

	bool has_data = rec_data->pData.size() > 4;

	packet<P> p;
	char* recv_buf = reinterpret_cast<char*>(&p);
	memcpy(recv_buf, &(*rec_data->pData.begin()), rec_data->pData.size());

	delete rec_data;

	// not all fields are 32bit
	p.ack = ntohl(p.ack);

	acked = false;


	// received ack -> in current send window?
	if (in_window<P>(p.ack, rack, rack + P::MAX_WINSIZ)) {
		// got valid acknowledge, update variable for other thread
		rack = p.ack;
		last_time_racked = sc_time_stamp();
		std::cout << name << " receive got new rack (" << p.ack << ")" << std::endl;
	} else {
		std::cout << name << " old ack (" << rack << ", " << (rack + P::MAX_WINSIZ) % SEQ_MAX
		          << "): " << rack << " " << p.ack << " " << (rack + P::MAX_WINSIZ) % SEQ_MAX
		          << std::endl;
	}

	// valid data piggy-back?
	if (has_data) {

		p.seq = ntohl(p.seq);
		p.pid = ntohs(p.pid);
		p.len = ntohs(p.len);

		// all data is 64bit aligned
		for (size_t idx = 0; idx < p.len; idx++)
			p.pdu[idx] = be64toh(p.pdu[idx]);
		std::cout << name << " packet marked as valid with length " << p.len << ", pid " << std::hex << p.pid << ", pdu[0]" << p.pdu[0] << std::endl;
		// FIXME, for now we just accept sequential packets...
		typename packet<P>::seq_t l_rseq = next_seq<P>(rseq);
		if (p.seq == l_rseq) {
			double r = 1.0 * rand() / RAND_MAX;
			if (r < receiver_drop_rate) { // random dropping
				return; // FIXME: wait here?
			}
			if (receive_buffers->window_full()) {
				return; // FIXME: wait here?
			}
			receive_buffers->push_back(p);
			std::cout << name << " accepted data packet with seq " << p.seq << std::endl;
			rseq = l_rseq;

			// use data from cache to fill holes
			for (size_t i = 0; i < P::MAX_WINSIZ; i++) {
				l_rseq = next_seq<P>(l_rseq);
				if ((*receive_cache.status)[l_rseq]) {
					std::cout << name << " using cached data: (seq: " << l_rseq << ")" << std::endl;
					(*receive_cache.status)[l_rseq] = false;
					receive_buffers->push_back((*receive_cache.data)[l_rseq]);
					rseq = l_rseq;
				} else
					break;
			}
		} else if (in_window<P>(p.seq, ack, ack + P::MAX_WINSIZ)) {
			// cache data after gap
			(*receive_cache.data)[p.seq] = p;
			(*receive_cache.status)[p.seq] = true;
		} else {
			// else: already received
		}
	} else {
		std::cout << "ack only" << std::endl;
	}
	std::cout << name << " received packet" << std::endl;
}


NCSC_MODULE_EXPORT(arq_stream_trigger)

// Instantiate ARQStream for the supported template type parameter(s)
template class ARQStream<ParametersFcpBss1>;

#endif
} // namespace
