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
	// Lock the used types together
	using arq_frame_t = arq_frame<P>;
	// We can only verify the sizes because arq_frame_t::entry_t is __u64, an
	// alias to `long long unsigned int`, is a distinct type from uint64_t
	// which is what checks in fisch expect.
	using entry_t = uint64_t;
	// Same goes for for other fields to indicate different endianness
	using ack_t = uint32_t;
	using seq_t = uint32_t;
	using pid_t = uint16_t;
	using len_t = uint16_t;

	// Perform checks
	static_assert(sizeof(arq_frame_t::COMMANDS[0]) == sizeof(entry_t), "entry_t has wrong length");
	static_assert(
	    sizeof(typename arq_frame_t::ack_t) == sizeof(ack_t), "ACK-field has wrong length");
	static_assert(
	    sizeof(typename arq_frame_t::seq_t) == sizeof(seq_t), "SEQ-field has wrong length");
	static_assert(
	    sizeof(packetid_t) == sizeof(pid_t),
	    "Packet ID-field has wrong length");
	static_assert(
	    sizeof(typename arq_frame_t::len_t) == sizeof(len_t), "LEN-field has wrong length");

	ack_t ack;
	seq_t seq;
	pid_t pid;
	len_t len;

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
