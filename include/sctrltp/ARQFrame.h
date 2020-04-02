#pragma once
// payload is counted in quadwords (64-bit)

#include "sctrltp_defines.h"

namespace sctrltp {

/* standard packet type: with payload */
template<typename P>
struct packet {
    typedef uint32_t seq_t;
    typedef uint64_t entry_t;

    seq_t ack;
    seq_t seq;
    uint16_t pid;
    uint16_t len;

    union {
        entry_t pdu[P::MAX_PDUWORDS];
        uint8_t rawpdu[P::MAX_PDUWORDS*8];
    };
    static_assert(sizeof(pdu) == sizeof(rawpdu), "");

    size_t size() const {
        size_t tmp = 0;
        tmp += sizeof(ack);
        tmp += sizeof(seq);
        tmp += sizeof(pid);
        tmp += sizeof(len);
        tmp += len * 8; // length is 64bit-wise!
        return tmp;
    }

    packet() :
        pid(0xDEAD),
        len(1) // minimum length
    {}
} __attribute__ ((__packed__));

} // namespace sctrltp
