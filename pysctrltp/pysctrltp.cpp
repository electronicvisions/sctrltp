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

	py::class_<sctrltp::ARQStream> arqstream(m, "ARQStream");

	py::enum_<sctrltp::ARQStream::Mode>(arqstream, "Mode")
		.value("NOTHING", sctrltp::ARQStream::NOTHING)
		.value("NONBLOCK", sctrltp::ARQStream::NONBLOCK)
		.value("FLUSH", sctrltp::ARQStream::FLUSH)
		.export_values();

	arqstream
	    .def(py::init<bool const>(), "reset"_a = true)
		.def(py::init<std::string const, bool const>(), "name"_a, "reset"_a = true)
		.def(py::init<std::string const,
		              std::string const,
			      ARQStream::udpport_t const,
			      std::string const,
			      ARQStream::udpport_t const,
			      bool const>(), "name"_a, "source_ip"_a, "source_port"_a,
			      "target_ip"_a, "target_port"_a, "reset"_a = true)
		.def("send", &ARQStream::send, "packet"_a, "mode"_a = sctrltp::ARQStream::Mode::FLUSH)
		.def("receive", &ARQStream::receive, "packet"_a, "mode"_a = sctrltp::ARQStream::Mode::NONBLOCK)
		.def("flush", &ARQStream::flush)
		.def("received_packet_available", &ARQStream::received_packet_available)
		.def("all_packets_sent", &ARQStream::all_packets_sent)
		.def("send_buffer_full", &ARQStream::send_buffer_full)
	;

	py::class_<sctrltp::packet> packet(m, "packet");
	packet
		.def(py::init<>())
		.def_readwrite("pid", &packet::pid)
		.def_readwrite("len", &packet::len)
		.def("__getitem__", [](sctrltp::packet const& p, size_t const idx) {
				if (idx >= p.len) {
					throw py::index_error();
				}
				return p[idx];
			}
		)
		.def("__setitem__", [](sctrltp::packet& p, size_t const idx, sctrltp::packet::entry_t const e) {
				if (idx >= p.len) {
					throw py::index_error();
				}
				p[idx] = e;
			}
		)
	;

	return m.ptr();
}
