#ifndef __ARQ_STREAM_WRAP_H__
#define __ARQ_STREAM_WRAP_H__

#include "systemc.h"

// and we have to use HW toolset... gcc 4.1 :)
#define WINDOW_SIZE 512
#define RECEIVE_WINDOW_SIZE WINDOW_SIZE

#define SEQ_SIZE 65536 // (UINT_MAX+1) // must be < maximum seq_t value / 2
#define SEQ_MAX (SEQ_SIZE - 1)

// payload is counted in quadwords (64-bit)
#define MAX_PDUWORDS 180

// 14 + 20 + 8; // size of header shit... just for calculating waiting time for sendUDP
#define WAIT_SENDUDP 42


#include "sctrltp/ARQStream.h"
#include "sctrltp/ARQFrame.h"

#include "ethernet_software_if.h"
#ifdef NCSIM
#include "eth_stim.h"
#endif
#include "eth_if.h"

#include <boost/array.hpp>

namespace sctrltp
{

#ifdef NCSIM
// for simulation: call send/receive triggers periodically from separate SystemC thread
SC_MODULE(arq_stream_trigger)
{
	SC_CTOR(arq_stream_trigger)
	{
		SC_THREAD(trigger_func_send);
		SC_THREAD(trigger_func_rec);
	}

	void trigger_func_send();
	void trigger_func_rec();
};


struct ARQStreamImpl
{
	std::string name;
	double sender_drop_rate;
	double receiver_drop_rate;

	// connection props
	ARQStream::ip_t source_ip;
	ARQStream::udpport_t source_port;
	ARQStream::ip_t target_ip;
	ARQStream::udpport_t target_port;

	EthernetSoftwareIF eth_soft;

	// global variable to check run status
	bool running;

	bool trigger_send_running;

	packet::seq_t ack;               // last packt we acked (window start position)
	packet::seq_t rack;              // max packet acknowledged by remote
	packet::seq_t rseq;              // last received sequence number
	bool acked;                      // last received sequence number was in-window?
	packet::seq_t last_seq_inserted; // last pushed packet into send queue
	sc_time last_time_racked;

	sc_time timeout;
	sc_time resend_timeout;

	// pointer to network simulator interface
	eth_if* pEthIf;

	// window buffers, declare struct and defines below
	template <size_t WS = WINDOW_SIZE>
	struct window_buffer : boost::array<packet, WS>
	{
		size_t first_idx;
		size_t size_valid;

		size_t window_idx() const { return first_idx; }

		size_t window_size() const { return size_valid; }

		size_t window_space() const { return WS - size_valid; }

		bool window_empty() const { return !window_size(); }

		bool window_full() const { return !window_notfull(); }

		bool window_notfull() const { return size_valid < WS; }

		packet& window_at(size_t ii)
		{
			if ((ii >= window_size()) || (first_idx > WS))
				throw std::runtime_error("access to out-of-window element");
			return (*this)[(first_idx + ii) % WS];
		}

		// returns copy of first element, then drops in window
		packet pop_front()
		{
			if (window_empty())
				throw std::runtime_error("nothing to pop");
			size_t tmp = first_idx;
			first_idx = (first_idx + 1) % WS;
			size_valid--;
			return (*this)[tmp];
		}

		void drop_front(size_t N = 0)
		{
			for (size_t i = 0; i < N; i++)
				pop_front();
		}

		void push_back(packet p)
		{
			if (window_full())
				throw std::runtime_error("window full, can't push");
			size_t idx = (first_idx + window_size()) % WS;
			(*this)[idx] = p;
			size_valid++;
		}

		window_buffer()
		    : boost::array<packet, WS>(),
		      // no valid data upon creation
		      first_idx(0),
		      size_valid(0)
		{
		}
	};
	window_buffer<WINDOW_SIZE>* send_buffers;
	window_buffer<RECEIVE_WINDOW_SIZE>* receive_buffers;


	struct cache
	{
		boost::array<bool, SEQ_SIZE>* status;
		boost::array<packet, SEQ_SIZE>* data;
		cache() : status(new boost::array<bool, SEQ_SIZE>), data(new boost::array<packet, SEQ_SIZE>)
		{
			for (size_t i = 0; i < SEQ_SIZE; i++)
				(*status)[i] = false;
		}
	} receive_cache;

	bool send(packet tmp);
	bool receive(packet& out);
	void trigger_send();
	void trigger_receive();
};


#endif // NCSIM

} // namespace

#endif // __ARQ_STREAM_WRAP_H__
