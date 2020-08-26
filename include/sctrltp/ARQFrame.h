#pragma once
// payload is counted in quadwords (64-bit)

#include "sctrltp/packets.h"
#include "sctrltp_defines.h"

#include <type_traits>

namespace sctrltp {

/* standard packet type: with payload */
template <typename P>
struct packet
{
	// lock the used types together
	using arq_frame_t = arq_frame<P>;
	using seq_t = decltype(std::declval<arq_frame_t>().SEQ);
	// NOTE: We cannot use the following statement to derive entry_t:
	//
	// `using entry_t = std::decay_t<decltype(std::declval<arq_frame_t&>().COMMANDS[0])>;`
	//
	// Because entry_t is __u64, an alias to `long long unsigned int`,
	// which a distinct type from uint64_t which is what checks in fisch
	// expect.
	using entry_t = uint64_t;

	// -> Solution: We check the correct length
	static_assert(
	    sizeof(std::decay_t<decltype(std::declval<arq_frame_t&>().COMMANDS[0])>) == sizeof(entry_t),
	    "entry_t has wrong length");

	// NOTE: Correct alignment is ensured by test/test-alignment.cpp
	decltype(std::declval<arq_frame_t&>().ACK) ack;
	decltype(std::declval<arq_frame_t&>().SEQ) seq;
	decltype(std::declval<arq_frame_t&>().PTYPE) pid;
	decltype(std::declval<arq_frame_t&>().LEN) len;

	// Compute size of header.
	// This will be used to copy the whole packet header at once.
	constexpr static size_t size_header = sizeof(ack) + sizeof(seq) + sizeof(pid) + sizeof(len);

	union
	{
		entry_t pdu[P::MAX_PDUWORDS];
		uint8_t rawpdu[P::MAX_PDUWORDS * 8];
	};
	static_assert(sizeof(pdu) == sizeof(rawpdu), "");

	size_t size() const
	{
		size_t tmp = 0;
		tmp += sizeof(ack);
		tmp += sizeof(seq);
		tmp += sizeof(pid);
		tmp += sizeof(len);
		tmp += len * 8; // length is 64bit-wise!
		return tmp;
	}

	packet() : pid(0xDEAD), len(1) // minimum length
	{}
} __attribute__((__packed__));

} // namespace sctrltp
