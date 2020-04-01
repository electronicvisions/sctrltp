#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <thread>

#include "sctrltp/ARQFrame.h"
#include "sctrltp/ARQStream.h"

/**
 * This class provides a framework to test the HostARQ link.
 * It sends packets configurable, varying size and payload to the FPGA as loop back packets. The
 * returned words are checked for correct payload. During run various stats are recorded.
 */
template <typename P = sctrltp::Parameters<>>
class LoopbackTest
{
public:
	typedef typename sctrltp::packet<P>::entry_t WordType;

	/**
	 * Enum encoding the mode in which way packets are formed
	 * Corner: Send n packets (defined by setting corner) with length one then one max length packet
	 * Max: Maximum packet length
	 * Min: Minimum packet length (1)
	 * Random: random packet size
	 * SequenceIncremental: Ascending packet length with max packet length wrap around
	 * SequenceDecremental: Descending packet length with max packet length wrap around
	 */
	enum class PacketMode
	{
		Corner = 'c',
		Random = 'r',
		Max = 'm',
		Min = 'n',
		SequenceIncremental = 's',
		SequenceDecremental = 'd'
	};
	/**
	 * Enum encoding the mode in which way word payload is filled
	 * Random: random payload
	 * SequenceIncremental: Ascending payload with 64bit wrap around
	 * SequenceIncremental: Descending payload with 64bit wrap around
	 */
	enum class PayloadMode
	{
		Random = 'r',
		SequenceIncremental = 's',
		SequenceDecremental = 'd'
	};

	/**
	 * Settings under which a test run is executed
	 */
	struct Settings
	{
		// runtime of the test
		std::chrono::steady_clock::duration runtime;
		// time to wait after runtime for packets still in flight
		std::chrono::steady_clock::duration timeout;
		PacketMode packet_mode;
		PayloadMode payload_mode;
		// seeds for random engines
		uint64_t payload_seed;
		uint32_t length_seed;
		// max packet length for sequential loopback
		size_t packet_length;
		// number of packets between max-length packets for corner case testing
		size_t corner;
		bool print_progress;
		// maximum number off errors before hard break
		size_t max_retries;

		Settings();
	};

	/**
	 * Statistics curated during test run
	 */
	struct Stats
	{
		size_t sent_payload_counter;
		size_t received_payload_counter;
		size_t sent_packet_counter;
		size_t received_packet_counter;
		size_t error_counter;
		double approx_bandwidth; // Mbit/s
	};

	LoopbackTest(std::string ip, Settings const& settings = Settings());

	/**
	 * Update test with new settings
	 */
	void set_settings(Settings const&);
	Settings get_settings() const;


	/**
	 * Executes one test run and returns statistic of that run
	 */
	Stats run();

private:
	std::thread get_sending_thread();
	std::thread get_receiving_thread();
	std::thread get_timer_thread();
	std::thread get_progress_thread();

	void stats_reset();

	sctrltp::ARQStream<P> m_arq_stream;

	sctrltp::packet<P> test_packet;
	sctrltp::packet<P> received_packet;

	Stats m_stats;
	Settings m_settings;
	WordType m_expected_next_word;
	WordType m_next_word_to_send;

	// random engines for setting random payloads and packet lengths, since we want to check packets
	// on the fly both threads need separate copies of the payload engine;
	// "subtract with carry" engines are used to optimize performance, not randomness
	std::ranlux48_base m_sending_engine;
	std::ranlux48_base m_receiving_engine;
	std::ranlux24_base m_length_engine;

	// the send function for different loopback types is the same, only
	// the function modifying the packet changes
	std::function<void(void)> make_function;
	std::function<void(void)> check_packet_payload;
	std::function<WordType(void)> get_next_word;


	std::atomic<bool> m_timer_running;
	std::atomic<bool> m_receive_running;

	std::uniform_int_distribution<WordType> sending_dist;
	std::uniform_int_distribution<WordType> receiving_dist;
	std::uniform_int_distribution<uint32_t> length_dist;

	void timer();

	void progress();

	void approximate_bandwidth();

	// payload of constant length and rising nubmer
	WordType make_incremental_sequence_payload();
	void check_incremental_sequence_payload();
	// payload of constant length and decreading number
	WordType make_decremental_sequence_payload();
	void check_decremental_sequence_payload();
	// payload of random length with random values
	WordType make_random_payload();
	void check_random_payload();

	void make_decremental_sequence_packet();
	void make_incremental_sequence_packet();
	void make_random_packet();
	void make_corner_packet();
	void make_max_packet();
	void make_min_packet();


	void send_loopback();
	void receive_loopback();
};

// needed for boost program options
template <typename P = sctrltp::Parameters<>>
inline std::istream& operator>>(std::istream& in, typename LoopbackTest<P>::PacketMode& tt)
{
	std::string token;
	in >> token;
	if (token == "r")
		tt = LoopbackTest<P>::PacketMode::Random;
	else if (token == "s")
		tt = LoopbackTest<P>::PacketMode::SequenceIncremental;
	else if (token == "d")
		tt = LoopbackTest<P>::PacketMode::SequenceDecremental;
	else if (token == "c")
		tt = LoopbackTest<P>::PacketMode::Corner;
	else if (token == "m")
		tt = LoopbackTest<P>::PacketMode::Max;
	else if (token == "n")
		tt = LoopbackTest<P>::PacketMode::Min;
	else
		in.setstate(std::ios_base::failbit);
	return in;
}

// needed for boost program options
template <typename P = sctrltp::Parameters<>>
inline std::istream& operator>>(std::istream& in, typename LoopbackTest<P>::PayloadMode& tt)
{
	std::string token;
	in >> token;
	if (token == "r")
		tt = LoopbackTest<P>::PayloadMode::Random;
	else if (token == "s")
		tt = LoopbackTest<P>::PayloadMode::SequenceIncremental;
	else if (token == "d")
		tt = LoopbackTest<P>::PayloadMode::SequenceDecremental;
	else
		in.setstate(std::ios_base::failbit);
	return in;
}
