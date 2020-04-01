#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include <boost/program_options.hpp>
#include <boost/timer/timer.hpp>

#include "LoopbackTest.h"
#include "bpo_parser_helper.h"

namespace bpo = boost::program_options;
namespace chr = std::chrono;
using namespace std::chrono_literals;
using namespace sctrltp;

int main(int argc, char** argv)
{
	std::string ip;
	LoopbackTest<>::PayloadMode payload_mode = LoopbackTest<>::PayloadMode::Random;
	LoopbackTest<>::PacketMode packet_mode = LoopbackTest<>::PacketMode::Random;
	uint64_t payload_seed = static_cast<uint64_t>(std::time(nullptr));
	uint32_t length_seed = static_cast<uint32_t>(std::time(nullptr)) * payload_seed;
	size_t len;
	size_t corner;
	bpo_parser_helper::duration runtime;
	bpo_parser_helper::duration timeout;

	bpo::options_description desc("Options");
	desc.add_options()("help", "produce help message")(
	    "ip", bpo::value<std::string>(&ip), "Set remote IP address")(
	    "runtime",
	    bpo::value<bpo_parser_helper::duration>(&runtime)->default_value(
	        bpo_parser_helper::duration{10s}),
	    "Runtime of the test in 'h'ours, 'm'inutes or 's'econds")(
	    "timeout",
	    bpo::value<bpo_parser_helper::duration>(&timeout)->default_value(
	        bpo_parser_helper::duration{5s}),
	    "Time to wait for last response after sending in 'h'ours, 'm'inutes or 's'econds")(
	    "payload_mode", bpo::value<LoopbackTest<>::PayloadMode>(&payload_mode),
	    "Type of test, includes: \n 's'equencial_increase, seqiuncial_'d'ecrese, 'r'andom")(
	    "packet_mode", bpo::value<LoopbackTest<>::PacketMode>(&packet_mode),
	    "Type of test, includes: \n 's'equencial_increase, seqiuncial_'d'ecrese, 'r'andom, 'm'ax, "
	    "mi'n' and 'c'orner-case, default ")(
	    "payload_seed", bpo::value<uint64_t>(&payload_seed),
	    "Seed the generator for random packet payload generation, default is system clock")(
	    "length_seed", bpo::value<uint32_t>(&length_seed),
	    "Seed the generator for random packet length generation, default is system clock")(
	    "len", bpo::value<size_t>(&len)->default_value(Parameters<>::MAX_PDUWORDS),
	    "Set packet length for sequential loopback")(
	    "corner", bpo::value<size_t>(&corner)->default_value(1000),
	    "Set the number of length 1 packets between max length packets for "
	    "corner-case test");

	bpo::variables_map vm;
	bpo::store(bpo::parse_command_line(argc, argv, desc), vm);
	bpo::notify(vm);

	if (vm.count("help")) {
		std::cout << desc << "\n";
		return 1;
	}

	std::cout << "Connecting to " << ip << std::endl;
	std::cout << "Testing for " << runtime << " (+ " << timeout << " timeout)" << std::endl;
	std::cout << "Random payload seed " << payload_seed << std::endl;
	std::cout << "Random length seed " << length_seed << std::endl;

	LoopbackTest<>::Settings settings;
	settings.runtime = chr::duration_cast<chr::seconds>(runtime.value);
	settings.timeout = chr::duration_cast<chr::seconds>(timeout.value);
	settings.payload_mode = payload_mode;
	settings.packet_mode = packet_mode;
	settings.payload_seed = payload_seed;
	settings.length_seed = length_seed;
	settings.packet_length = len;
	settings.corner = corner;
	settings.print_progress = true;

	LoopbackTest<> loop(ip, settings);

	std::cout << "ARQStream established, start sending" << std::endl;

	LoopbackTest<>::Stats stats = loop.run();

	std::cout << "Done!" << std::endl;
	std::cout << "sent " << stats.sent_payload_counter << " payloads in "
	          << stats.sent_packet_counter << " packets" << std::endl;
	std::cout << "received " << stats.received_payload_counter << " payloads in "
	          << stats.received_packet_counter << " packets" << std::endl;
	std::cout << stats.error_counter << " payloads were wrong" << std::endl;
	std::cout << stats.sent_payload_counter - stats.received_payload_counter
	          << " payloads went missing" << std::endl;

	std::cout << "approximate bandwidth was " << std::setprecision(3) << stats.approx_bandwidth
	          << " Mbit/s" << std::endl;

	if (stats.error_counter == 0 && stats.received_payload_counter == stats.sent_payload_counter) {
		return 0;
	} else {
		return 1;
	}
}
