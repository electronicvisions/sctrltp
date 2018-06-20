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
ARQStream* arq_stream_ptr = NULL;
#endif


ARQStream::ARQStream(std::string const name, std::string const source_ip, udpport_t source_port,
                     std::string const target_ip, udpport_t target_port, bool reset)
    : name(name), pimpl(new ARQStreamImpl)
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
	pimpl->send_buffers = new ARQStreamImpl::window_buffer<WINDOW_SIZE>;
	pimpl->receive_buffers = new ARQStreamImpl::window_buffer<RECEIVE_WINDOW_SIZE>;
	assert((SEQ_MAX + 1) / 2 >= WINDOW_SIZE);

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


ARQStream::~ARQStream()
{
	stop();

#ifdef NCSIM
	arq_stream_ptr = NULL; // assume there is only one ARQStream instance
#endif
}


void ARQStream::start()
{
	pimpl->running = true;

	// reset the arq and ram
	wait(110.0, SC_US); // wait for memory to initialize
	uint32_t payload = 0xABABABAB;
	pimpl->eth_soft.sendUDP(pimpl->target_ip.to_ulong(), 0xaffe, &payload, sizeof(payload));
	wait(10.0, SC_US); // wait for memory to initialize
}


void ARQStream::stop()
{
	if (!pimpl->running)
		return;
	pimpl->running = false;
}


/* send: copy packet to send buffer and return true, else return false to indicate "block" */
bool ARQStream::send(packet tmp, ARQStream::Mode)
{

	return pimpl->send(tmp);
}


/* receive: pop packet from receive buffer and return true, else return false*/
bool ARQStream::receive(packet& out, ARQStream::Mode mode)
{
	if (!(mode & ARQStream::NONBLOCK))
		throw std::runtime_error("only NONBLOCK mode supported");
	return pimpl->receive(out);
}


bool ARQStream::received_packet_available()
{
	return (!pimpl->receive_buffers->window_empty());
}


bool ARQStream::all_packets_sent()
{
	return (pimpl->send_buffers->window_empty());
}


bool ARQStream::send_buffer_full()
{
	return (pimpl->send_buffers->window_full());
}


void ARQStream::trigger_send()
{
	pimpl->trigger_send();
}


void ARQStream::trigger_receive()
{
	pimpl->trigger_receive();
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


bool ARQStreamImpl::send(packet tmp)
{
	if (send_buffers->window_full()) {
		std::cout << name << " window full during send()" << std::endl;
		return false;
	}

	packet::seq_t latest = next_seq(last_seq_inserted);
	tmp.seq = latest;
	send_buffers->push_back(tmp);
	last_seq_inserted = latest;

	return true;
}

bool ARQStreamImpl::receive(packet& out)
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

void ARQStreamImpl::trigger_send()
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

	static packet::seq_t seq = SEQ_MAX;
	static packet::seq_t next_seq_to_send = 0;
	static sc_time last_time_sent = SC_ZERO_TIME;
	static size_t idx = 0;

	// if last received ack is in current send window slide send window
	if (in_window(rack, seq, seq + send_buffers->window_size())) {
		std::cout << name << " rack was " << std::dec << rack << " dist " << dist_window(rack, seq)
		          << std::endl;
		assert(dist_window(rack, seq) <= WINDOW_SIZE);
		std::cout << name << " shifting window by " << dist_window(rack, seq) << std::endl;
		send_buffers->drop_front(dist_window(rack, seq));
		idx -= dist_window(rack, seq);
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

			packet& pckt = send_buffers->window_at(idx);
			if (!timeouted && pckt.seq != next_seq_to_send) {
				std::cout << name << " skipping (not sending): " << pckt.seq << ", expected "
				          << next_seq_to_send << std::endl;
				continue;
			}

			pckt.ack = rseq;
			std::cout << name << " sending pending packet (seq: " << pckt.seq
			          << ", ack: " << pckt.ack << " at buffer " << idx
			          << "th position, size: " << pckt.size() << ")" << std::endl;
			next_seq_to_send = next_seq(pckt.seq);
			if (rand() < RAND_MAX * (1.0 - sender_drop_rate)) { // random dropping ;)
				std::cout << "called sendUDP with " << target_ip.to_ulong() << " " << target_port
				          << " size " << pckt.size() << std::endl;

				// copy because of sendUDP fuckup
				packet pckt_copy((pckt));

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
		packet::seq_t l_rseq = rseq;
		if (ack != l_rseq || !acked) {
			std::cout << name << " acking for " << l_rseq << " (ack was " << ack << ")"
			          << std::endl;
			ack_frame tmp;
			tmp.ack = htonl(l_rseq);

			eth_soft.sendUDP(target_ip.to_ulong(), target_port, reinterpret_cast<void*>(&tmp),
			                 sizeof(ack_frame));
			// update ack
			ack = l_rseq;
		}
		last_time_sent = sc_time_stamp();
	}

	acked = true;

	trigger_send_running = false;
}


void ARQStreamImpl::trigger_receive()
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

	packet p;
	char* recv_buf = reinterpret_cast<char*>(&p);
	memcpy(recv_buf, &(*rec_data->pData.begin()), rec_data->pData.size());

	delete rec_data;

	// not all fields are 32bit
	p.ack = ntohl(p.ack);

	acked = false;


	// received ack -> in current send window?
	if (in_window(p.ack, rack, rack + WINDOW_SIZE)) {
		// got valid acknowledge, update variable for other thread
		rack = p.ack;
		last_time_racked = sc_time_stamp();
		std::cout << name << " receive got new rack (" << p.ack << ")" << std::endl;
	} else {
		std::cout << name << " old ack (" << rack << ", " << (rack + WINDOW_SIZE) % SEQ_MAX
		          << "): " << rack << " " << p.ack << " " << (rack + WINDOW_SIZE) % SEQ_MAX
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
		std::cout << name << " packet marked as valid with length " << p.len << std::endl;
		// FIXME, for now we just accept sequential packets...
		packet::seq_t l_rseq = next_seq(rseq);
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
			for (size_t i = 0; i < WINDOW_SIZE; i++) {
				l_rseq = next_seq(l_rseq);
				if ((*receive_cache.status)[l_rseq]) {
					std::cout << name << " using cached data: (seq: " << l_rseq << ")" << std::endl;
					(*receive_cache.status)[l_rseq] = false;
					receive_buffers->push_back((*receive_cache.data)[l_rseq]);
					rseq = l_rseq;
				} else
					break;
			}
		} else if (in_window(p.seq, ack, ack + WINDOW_SIZE)) {
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


#endif
} // namespace
