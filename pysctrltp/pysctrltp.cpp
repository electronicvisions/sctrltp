#include <pybind11/chrono.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "sctrltp/libhostarq.h"
#include "sctrltp/ARQStream.h"
#include "sctrltp/ARQFrame.h"

namespace py = pybind11;


PYBIND11_PLUGIN(pysctrltp) {
	py::module m("pysctrltp", "pysctrltp plugin");

	using namespace sctrltp;
	using namespace pybind11::literals;

	// opaque handle => use create to fill it
	py::class_<hostarq_handle> hostarq_handle(m, "hostarq_handle");
	hostarq_handle
		.def(py::init<>())
	;

	// handling functions
	m.def("create_handle", &hostarq_create_handle);
	m.def("free_handle", &hostarq_free_handle);
	m.def("open", &hostarq_open);
	m.def("close", &hostarq_close);

	py::class_<sctrltp::ARQStreamSettings> settings(m, "ARQStreamSettings");
	settings.def(py::init<>())
	    .def_readwrite("ip", &ARQStreamSettings::ip)
	    .def_readwrite("reset", &ARQStreamSettings::reset)
	    .def_readwrite("init_flush_lb_packet", &ARQStreamSettings::init_flush_lb_packet)
	    .def_readwrite("init_flush_timeout", &ARQStreamSettings::init_flush_timeout)
	    .def_readwrite("destruction_timeout", &ARQStreamSettings::destruction_timeout);

	py::class_<sctrltp::ARQStream<sctrltp::ParametersFcpBss1>> arqstream(m, "ARQStream");

	py::enum_<typename sctrltp::ARQStream<ParametersFcpBss1>::Mode>(arqstream, "Mode")
		.value("NOTHING", sctrltp::ARQStream<ParametersFcpBss1>::NOTHING)
		.value("NONBLOCK", sctrltp::ARQStream<ParametersFcpBss1>::NONBLOCK)
		.value("FLUSH", sctrltp::ARQStream<ParametersFcpBss1>::FLUSH)
		.export_values();

	arqstream.def(py::init<std::string const, bool const>(), "name"_a, "reset"_a = true)
	    .def(
	        py::init<
	            std::string const, std::string const, ARQStream<ParametersFcpBss1>::udpport_t const, std::string const,
	            ARQStream<ParametersFcpBss1>::udpport_t const, bool const>(),
	        "name"_a, "source_ip"_a, "source_port"_a, "target_ip"_a, "target_port"_a,
	        "reset"_a = true)
	    .def(py::init<sctrltp::ARQStreamSettings const>(), "settings"_a)
	    .def("send", &ARQStream<ParametersFcpBss1>::send, "packet"_a, "mode"_a = sctrltp::ARQStream<ParametersFcpBss1>::Mode::FLUSH)
	    .def(
	        "receive", &ARQStream<ParametersFcpBss1>::receive, "packet"_a,
	        "mode"_a = sctrltp::ARQStream<ParametersFcpBss1>::Mode::NONBLOCK)
	    .def("flush", &ARQStream<ParametersFcpBss1>::flush)
	    .def("received_packet_available", &ARQStream<ParametersFcpBss1>::received_packet_available)
	    .def("all_packets_sent", &ARQStream<ParametersFcpBss1>::all_packets_sent)
	    .def("send_buffer_full", &ARQStream<ParametersFcpBss1>::send_buffer_full);

	py::class_<sctrltp::packet<ParametersFcpBss1>> packet(m, "packet");
	packet
		.def(py::init<>())
		.def_readwrite("pid", &sctrltp::packet<ParametersFcpBss1>::pid)
		.def_readwrite("len", &sctrltp::packet<ParametersFcpBss1>::len)
		.def("__getitem__", [](sctrltp::packet<ParametersFcpBss1> const& p, size_t const idx) {
				if (idx >= p.len) {
					throw py::index_error();
				}
				return p.pdu[idx];
			}
		)
		.def("__setitem__", [](sctrltp::packet<ParametersFcpBss1>& p, size_t const idx, sctrltp::packet<ParametersFcpBss1>::entry_t const e) {
				if (idx >= p.len) {
					throw py::index_error();
				}
				p.pdu[idx] = e;
			}
		)
	;

	return m.ptr();
}
