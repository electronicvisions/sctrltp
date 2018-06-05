#include "LoopbackTest.h"

#include <iostream>
#include <stdexcept>

#include <boost/progress.hpp>
#include <boost/timer.hpp>

using namespace std::chrono_literals;

LoopbackTest::Settings::Settings() :
    runtime(1min),
    timeout(5s),
    packet_mode(LoopbackTest::PacketMode::Random),
    payload_mode(LoopbackTest::PayloadMode::Random),
    payload_seed(42),
    length_seed(52),
    packet_length(MAX_PDUWORDS),
    corner(1000),
    print_progress(false),
    max_retries(1000)
{}

LoopbackTest::LoopbackTest(std::string const ip, Settings const& settings) :
    m_arq_stream(sctrltp::ARQStream(ip, "192.168.0.1", 1234, ip, 1234)),
    m_expected_next_word(0),
    m_next_word_to_send(0),
    m_sending_engine(std::ranlux48_base(settings.payload_seed)),
    m_receiving_engine(std::ranlux48_base(settings.payload_seed)),
    m_length_engine(std::ranlux24_base(settings.length_seed))
{
	test_packet.pid = PTYPE_LOOPBACK;
	test_packet.len = MAX_PDUWORDS;

	set_settings(settings);
	stats_reset();

	sending_dist = std::uniform_int_distribution<WordType>(0, std::numeric_limits<WordType>::max());
	receiving_dist = std::uniform_int_distribution<WordType>(0, std::numeric_limits<WordType>::max());
}

void LoopbackTest::set_settings(Settings const& settings)
{
	m_settings = settings;
	switch (m_settings.payload_mode) {
		case PayloadMode::Random:
			get_next_word = std::bind(&LoopbackTest::make_random_payload, this);
			check_packet_payload = std::bind(&LoopbackTest::check_random_payload, this);
			break;
		case PayloadMode::SequenceIncremental:
			get_next_word = std::bind(&LoopbackTest::make_incremental_sequence_payload, this);
			check_packet_payload =
			    std::bind(&LoopbackTest::check_incremental_sequence_payload, this);
			m_expected_next_word = 0;
			m_next_word_to_send = 0;
			break;
		case PayloadMode::SequenceDecremental:
			get_next_word = std::bind(&LoopbackTest::make_decremental_sequence_payload, this);
			check_packet_payload =
			    std::bind(&LoopbackTest::check_decremental_sequence_payload, this);
			m_expected_next_word = std::numeric_limits<WordType>::max();
			m_next_word_to_send = std::numeric_limits<WordType>::max();
			break;
		default:
			std::cerr << "Entered payload mode " << static_cast<size_t>(settings.payload_mode)
			          << " not supported" << std::endl;
			std::terminate();
	};

	if(settings.packet_length > MAX_PDUWORDS) {
		throw std::overflow_error("Provided packet length to long");
	}
	length_dist = std::uniform_int_distribution<uint32_t>(1, settings.packet_length);

	switch (m_settings.packet_mode) {
		case PacketMode::Random:
			make_function = std::bind(&LoopbackTest::make_random_packet, this);
			break;
		case PacketMode::SequenceIncremental:
			make_function = std::bind(&LoopbackTest::make_incremental_sequence_packet, this);
			break;
		case PacketMode::SequenceDecremental:
			make_function = std::bind(&LoopbackTest::make_decremental_sequence_packet, this);
			break;
		case PacketMode::Corner:
			make_function = std::bind(&LoopbackTest::make_corner_packet, this);
			break;
		case PacketMode::Max:
			make_function = std::bind(&LoopbackTest::make_max_packet, this);
			break;
		case PacketMode::Min:
			make_function = std::bind(&LoopbackTest::make_min_packet, this);
			break;
		default:
			std::cerr << "Entered packet mode " << static_cast<size_t>(settings.packet_mode)
			          << " not supported" << std::endl;
			std::terminate();
	};
}

LoopbackTest::Settings LoopbackTest::get_settings() const
{
	return m_settings;
}

void LoopbackTest::timer()
{
	std::this_thread::sleep_for(m_settings.runtime);
	m_timer_running = false;
	std::this_thread::sleep_for(m_settings.timeout);
	m_receive_running = false;
	approximate_bandwidth();
}

void LoopbackTest::progress()
{
	// convert runtime to seconds, then update the bar every second
	size_t const int_runtime =
	    std::chrono::duration_cast<std::chrono::seconds>(m_settings.runtime + m_settings.timeout)
	        .count();
	boost::progress_display prog(int_runtime);
	for (size_t elapsed = 0; elapsed < int_runtime; elapsed++) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		++prog;
	}
}

LoopbackTest::WordType LoopbackTest::make_incremental_sequence_payload()
{
	// yes we want to return the not yet increased value
	return m_next_word_to_send++;
}

LoopbackTest::WordType LoopbackTest::make_decremental_sequence_payload()
{
	// yes we want to return the not yet decreased value
	return m_next_word_to_send--;
}

LoopbackTest::WordType LoopbackTest::make_random_payload()
{
	return sending_dist(m_sending_engine);
}

void LoopbackTest::make_incremental_sequence_packet()
{
	test_packet.len = (m_stats.sent_packet_counter % (m_settings.packet_length - 1 )) + 1;
	for (size_t i = 0; i < test_packet.len; i++) {
		test_packet.pdu[i] = get_next_word();
		m_stats.sent_payload_counter++;
	}
}

void LoopbackTest::make_decremental_sequence_packet()
{
	test_packet.len =
	    m_settings.packet_length - (m_stats.sent_packet_counter % (m_settings.packet_length - 1)) + 1;
	for (size_t i = 0; i < test_packet.len; i++) {
		test_packet.pdu[i] = get_next_word();
		m_stats.sent_payload_counter++;
	}
}

void LoopbackTest::make_random_packet()
{
	test_packet.len = length_dist(m_length_engine);
	for (size_t i = 0; i < test_packet.len; i++) {
		test_packet.pdu[i] = get_next_word();
		m_stats.sent_payload_counter++;
	}
}

void LoopbackTest::make_corner_packet()
{
	if (m_stats.sent_payload_counter % m_settings.corner) {
		test_packet.len = MAX_PDUWORDS;
	} else {
		test_packet.len = 1;
	}
	for (size_t i = 0; i < test_packet.len; i++) {
		test_packet.pdu[i] = get_next_word();
		m_stats.sent_payload_counter++;
	}
}

void LoopbackTest::make_max_packet()
{
	test_packet.len = m_settings.packet_length;
	for (size_t i = 0; i < test_packet.len; i++) {
		test_packet.pdu[i] = get_next_word();
		m_stats.sent_payload_counter++;
	}
}

void LoopbackTest::make_min_packet()
{
	test_packet.len = 1;
	for (size_t i = 0; i < test_packet.len; i++) {
		test_packet.pdu[i] = get_next_word();
		m_stats.sent_payload_counter++;
	}
}

void LoopbackTest::check_random_payload()
{
	size_t tries = 0;
	for (size_t i = 0; i < received_packet.len; i++) {
		// try to catch up by keep generating numbers and fail if this does not succeed
		while (received_packet.pdu[i] != receiving_dist(m_receiving_engine)) {
			tries++;
			if (tries > m_settings.max_retries) {
				// if we skipped more than 1000 packets something went wrong kill the program
				std::cerr << "[ERROR] receiving random packets failed lost more than "
				          << m_settings.max_retries << " packets" << std::endl;
				std::cerr << "lost track at payload " << m_stats.received_payload_counter
				          << std::endl;
				std::cerr << "which was in packet " << m_stats.received_payload_counter
				          << std::endl;
				std::terminate();
			}
		}
		if (tries > 0) {
			m_stats.error_counter++;
		}
		m_stats.received_payload_counter++;
	}
}

void LoopbackTest::check_incremental_sequence_payload()
{
	for (size_t i = 0; i < received_packet.len; i++) {
		if (received_packet.pdu[i] != m_expected_next_word) {
			m_stats.error_counter++;
			std::cerr << "[ERROR] received ascending word: " << received_packet.pdu[i]
			          << " did not match expected word:" << m_expected_next_word << std::endl;
		}
		m_expected_next_word = received_packet.pdu[i] + 1;
		m_stats.received_payload_counter++;
	}
}

void LoopbackTest::check_decremental_sequence_payload()
{
	for (size_t i = 0; i < received_packet.len; i++) {
		if (received_packet.pdu[i] != m_expected_next_word) {
			m_stats.error_counter++;
			std::cerr << "[ERROR] received descending word: " << received_packet.pdu[i]
			          << " did not match expected word:" << m_expected_next_word << std::endl;
		}
		m_expected_next_word = received_packet.pdu[i] - 1;
		m_stats.received_payload_counter++;
	}
}

void LoopbackTest::send_loopback()
{
	while (m_timer_running) {
		make_function();
		m_arq_stream.send(test_packet, sctrltp::ARQStream::NOTHING);
		m_stats.sent_packet_counter++;
	}
	m_arq_stream.flush();
}

void LoopbackTest::receive_loopback()
{
	while (m_receive_running) {
		if (m_arq_stream.received_packet_available()) {
			m_arq_stream.receive(received_packet, sctrltp::ARQStream::NOTHING);
			check_packet_payload();
			m_stats.received_packet_counter++;
		}
	}
}

void LoopbackTest::approximate_bandwidth()
{
	// TODO packets sent during "runtime" are used for this calculation
	// for a more exact measurement the actual time of the last _received_ data would be required
	double const total_data = m_stats.sent_payload_counter * std::numeric_limits<WordType>::digits; // total sent data in bits
	// total sending time in seconds
	double const total_time = std::chrono::duration_cast<std::chrono::seconds>(m_settings.runtime).count();
	m_stats.approx_bandwidth = (total_data / total_time) / 1e6; // bandwidth in Mbit/s
}

void LoopbackTest::stats_reset()
{
	m_stats.sent_payload_counter = 0;
	m_stats.received_payload_counter = 0;
	m_stats.sent_packet_counter = 0;
	m_stats.received_packet_counter = 0;
	m_stats.error_counter = 0;
	m_stats.approx_bandwidth = -1.0;

	m_timer_running = true;
	m_receive_running = true;
}

LoopbackTest::Stats LoopbackTest::run()
{
	stats_reset();
	std::thread timer = get_timer_thread();
	std::thread sending = get_sending_thread();
	std::thread receiving = get_receiving_thread();
	std::thread progress;
	if (m_settings.print_progress) {
		progress = get_progress_thread();
	}

	timer.join();
	sending.join();
	receiving.join();
	if (m_settings.print_progress) {
		progress.join();
	}

	return m_stats;
}

std::thread LoopbackTest::get_sending_thread()
{
	return std::thread([&] { send_loopback(); });
}

std::thread LoopbackTest::get_receiving_thread()
{
	return std::thread([&] { receive_loopback(); });
}

std::thread LoopbackTest::get_timer_thread()
{
	return std::thread([&] { timer(); });
}

std::thread LoopbackTest::get_progress_thread()
{
	return std::thread([&] { progress(); });
}
