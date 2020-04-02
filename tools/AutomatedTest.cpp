#include <chrono>
#include <iostream>
#include <boost/program_options.hpp>
#include <gtest/gtest.h>

#include "LoopbackTest.h"
#include "bpo_parser_helper.h"

namespace bpo = boost::program_options;
using namespace std::chrono_literals;
using namespace sctrltp;

std::string ip;
bpo_parser_helper::duration runtime;

extern int waf_gtest_argc;
extern char** waf_gtest_argv;
int parse_options();

// TODO make typed test for all packet and payload modes combinations
TEST(LoopbackTest, Random)
{
	if (parse_options() != 0) return;

	LoopbackTest<>::Settings settings;
	settings.runtime = std::chrono::duration_cast<std::chrono::seconds>(runtime.value);
	settings.payload_mode = LoopbackTest<>::PayloadMode::Random;
	settings.packet_mode = LoopbackTest<>::PacketMode::Random;
	LoopbackTest<> random_test(ip, settings);

	LoopbackTest<>::Stats random_stats = random_test.run();

	EXPECT_EQ(0, random_stats.error_counter);
	EXPECT_EQ(random_stats.sent_payload_counter, random_stats.received_payload_counter);

	// when using random packets the bandwidth is expected to be lower due to more communication
	// overhead (not all packets of maximum size)
	// TODO: Tweak these values to be max_pduwords, windowsize and wirespeed dependent
	double threshold = 500.0; // Mbit/s
	EXPECT_PRED_FORMAT2(::testing::DoubleLE, threshold, random_stats.approx_bandwidth)
	    << "Bandwidth below " << threshold << " Mbit/s";
}

int parse_options()
{
	bpo::options_description desc("Options");
	desc.add_options()("test-help", "produce help message")(
	    "ip", bpo::value<std::string>(&ip), "Set FPGA IP address")(
	    "runtime",
	    bpo::value<bpo_parser_helper::duration>(&runtime)->default_value(
	        bpo_parser_helper::duration{1h}),
	    "Runtime of the test in 'h'ours, 'm'inutes or 's'econds");

	bpo::variables_map vm;
	bpo::store(bpo::parse_command_line(waf_gtest_argc, waf_gtest_argv, desc), vm);
	bpo::notify(vm);

	if (vm.count("test-help")) {
		std::cout << desc << "\n";
		return 1;
	}
	return 0;
}
