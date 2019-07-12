#pragma once
// payload is counted in quadwords (64-bit)

#ifndef NCSIM
extern "C" {
#include "sctrltp_defines.h" // MAX_PDUWORDS
}
#else
// just the default setting
#ifndef MAX_PDUWORDS
#define MAX_PDUWORDS 180
#endif
#endif

#ifndef STATIC_ASSERT
#define STATIC_ASSERT(x) do{switch(0){case 0:case x:;}}while(false)
#endif

namespace sctrltp {

/* standard packet type: with payload */
struct packet {
    typedef uint32_t seq_t;
    typedef uint64_t entry_t;

    seq_t ack;
    seq_t seq;
    uint16_t pid;
    uint16_t len;

    union {
        entry_t pdu[MAX_PDUWORDS];
        uint8_t rawpdu[MAX_PDUWORDS*8];
    };

	void compile_time_assert() {
		STATIC_ASSERT(sizeof(pdu) == sizeof(rawpdu));
	}

	entry_t const& operator[](std::size_t const idx) const {
		return pdu[idx];
	}

	entry_t& operator[](std::size_t const idx) {
		return pdu[idx];
	}

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
