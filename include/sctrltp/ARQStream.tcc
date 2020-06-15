#pragma once

#include <iterator>
#include <stdexcept>
#include <type_traits>

#if __cplusplus < 201703L
// AG: this is especially required for compatibility with the xcelium-included
// gcc version(s) that do not (yet) speak c++17
namespace std {
template <class Base, class Derived>
constexpr bool is_base_of_v = is_base_of<Base, Derived>::value;
}
#endif

namespace sctrltp {

template <typename P>
template <typename InputIterator>
void ARQStream<P>::send(
    packetid_t const pid, InputIterator const begin, InputIterator const end, Mode const mode)
{
	if (mode == Mode::NONBLOCK) {
		throw std::runtime_error("NONBLOCK mode is unsupported for iterator send.");
	}

	typedef std::iterator_traits<InputIterator> iterator_traits;

	// Expect iterator value type to be the same as entry_t
	static_assert(
	    std::is_same<typename iterator_traits::value_type, typename packet<P>::entry_t>::value);
	// Expect the iterator category to satisfy input iterator category
	static_assert(
	    std::is_base_of_v<std::input_iterator_tag, typename iterator_traits::iterator_category>);

	auto iterator = begin;

	packet<P> t;
	t.pid = pid;

	if
#ifdef __cpp_if_constexpr
	    // cf. AG's comment regarding C++17 and xcelium's gcc
	    constexpr
#endif
	    (std::is_base_of_v<
	         std::random_access_iterator_tag, typename iterator_traits::iterator_category>) {
		size_t const num_full_packets = std::distance(begin, end) / P::MAX_PDUWORDS;
		size_t const num_rest_words = std::distance(begin, end) % P::MAX_PDUWORDS;

		// first fill and send all full packets
		t.len = P::MAX_PDUWORDS;
		for (size_t full_packet = 0; full_packet < num_full_packets; ++full_packet) {
			for (size_t i = 0; i < P::MAX_PDUWORDS; ++i) {
				t.pdu[i] = htobe64(*iterator);
				iterator++;
			}
			send_direct(t, Mode::NOTHING);
		}

		// send last packet if payloads are left over
		if (num_rest_words > 0) {
			t.len = num_rest_words;
			for (size_t i = 0; i < num_rest_words; ++i) {
				t.pdu[i] = htobe64(*iterator);
				iterator++;
			}
			// use specified mode for last packet
			send_direct(t, mode);
		} else if (mode == Mode::FLUSH) {
			// no words left to send but flush was requested
			flush();
		}
	} else { // distance only calculatable by traversing -> traverse and send
		size_t packed_index = 0;

		// first fill and send all full packets
		for (; iterator != end; ++iterator) {
			if (packed_index == P::MAX_PDUWORDS) {
				t.len = P::MAX_PDUWORDS;
				send_direct(t, Mode::NOTHING);
				packed_index = 0;
			}
			t.pdu[packed_index] = htobe64(*iterator);
		}

		// send last packet if payloads are left over
		if (packed_index > 0) {
			t.len = packed_index;
			// use specified flush mode for last packet
			send_direct(t, mode);
		} else if (mode == Mode::FLUSH) {
			// no words left to send but flush was requested
			flush();
		}
	}
}

} // namespace sctrltp