
#include <cstddef>
#include <gtest/gtest.h>

#include "sctrltp/ARQFrame.h"
#include "sctrltp/packets.h"
#include "sctrltp/sctrltp_defines.h"

using ParameterSets = ::testing::Types<
    sctrltp::ParametersFcpBss1,
    sctrltp::ParametersFcpBss2Cube,
    sctrltp::ParametersAnanasBss1>;

/**
 * Test fixture for data alignment.
 * @tparam Paramater Parameter set to test
 */
template <class Parameter>
class AlignmentTest : public testing::Test
{};

TYPED_TEST_SUITE(AlignmentTest, ParameterSets);

TYPED_TEST(AlignmentTest, General)
{
	using frame_t = sctrltp::arq_frame<TypeParam>;
	using packet_t = sctrltp::packet<TypeParam>;

	// Ensure that the first fields of are aligned in the same way
	static_assert(offsetof(frame_t, ACK) == offsetof(packet_t, ack), "ack misaligned");
	static_assert(offsetof(frame_t, SEQ) == offsetof(packet_t, seq), "seq misaligned");
	static_assert(offsetof(frame_t, PTYPE) == offsetof(packet_t, pid), "pid misaligned");
	static_assert(offsetof(frame_t, LEN) == offsetof(packet_t, len), "len misaligned");

	static_assert(
	    sizeof(std::declval<frame_t&>().COMMANDS[0]) == sizeof(typename packet_t::entry_t),
	    "entry_t has wrong length");
}
