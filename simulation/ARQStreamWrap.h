#ifndef __ARQ_STREAM_WRAP_H__
#define __ARQ_STREAM_WRAP_H__

#include "systemc.h"
#include "sctrltp/sctrltp_defines.h"

#define SEQ_MAX (P::MAX_NRFRAMES - 1)

// 14 + 20 + 8; // size of header... just for calculating waiting time for sendUDP
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

template<typename P>
struct ARQStreamImpl
{
	std::string name;
	double sender_drop_rate;
	double receiver_drop_rate;

	// connection props
	typename ARQStream<P>::ip_t source_ip;
	udpport_t source_port;
	typename ARQStream<P>::ip_t target_ip;
	udpport_t target_port;

	EthernetSoftwareIF eth_soft;

	// global variable to check run status
	bool running;

	bool trigger_send_running;

	typename packet<P>::seq_t ack;               // last packt we acked (window start position)
	typename packet<P>::seq_t rack;              // max packet acknowledged by remote
	typename packet<P>::seq_t rseq;              // last received sequence number
	bool acked;                      // last received sequence number was in-window?
	typename packet<P>::seq_t last_seq_inserted; // last pushed packet into send queue
	sc_time last_time_racked;

	sc_time timeout;
	sc_time resend_timeout;

	// pointer to network simulator interface
	eth_if* pEthIf;

	// window buffers, declare struct and defines below
	template <size_t WS = P::MAX_WINSIZ>
	struct window_buffer : boost::array<packet<P>, WS>
	{
		size_t first_idx;
		size_t size_valid;

		size_t window_idx() const { return first_idx; }

		size_t window_size() const { return size_valid; }

		size_t window_space() const { return WS - size_valid; }

		bool window_empty() const { return !window_size(); }

		bool window_full() const { return !window_notfull(); }

		bool window_notfull() const { return size_valid < WS; }

		packet<P>& window_at(size_t ii)
		{
			if ((ii >= window_size()) || (first_idx > WS))
				throw std::runtime_error("access to out-of-window element");
			return (*this)[(first_idx + ii) % WS];
		}

		// returns copy of first element, then drops in window
		packet<P> pop_front()
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

		void push_back(packet<P> p)
		{
			if (window_full())
				throw std::runtime_error("window full, can't push");
			size_t idx = (first_idx + window_size()) % WS;
			(*this)[idx] = p;
			size_valid++;
		}

		window_buffer()
		    : boost::array<packet<P>, WS>(),
		      // no valid data upon creation
		      first_idx(0),
		      size_valid(0)
		{
		}
	};
	window_buffer<P::MAX_WINSIZ>* send_buffers;
	window_buffer<P::MAX_WINSIZ>* receive_buffers;


	struct cache
	{
		boost::array<bool, P::MAX_NRFRAMES>* status;
		boost::array<packet<P>, P::MAX_NRFRAMES>* data;
		cache() : status(new boost::array<bool, P::MAX_NRFRAMES>), data(new boost::array<packet<P>, P::MAX_NRFRAMES>)
		{
			for (size_t i = 0; i < P::MAX_NRFRAMES; i++)
				(*status)[i] = false;
		}
	} receive_cache;

	bool send(packet<P> tmp);
	bool receive(packet<P>& out);
	void trigger_send();
	void trigger_receive();
};


#endif // NCSIM

} // namespace

#endif // __ARQ_STREAM_WRAP_H__
