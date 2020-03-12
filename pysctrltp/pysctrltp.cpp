#include <typeindex>
#include <unordered_map>

#include <pybind11/chrono.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "sctrltp/ARQFrame.h"
#include "sctrltp/ARQStream.h"
#include "sctrltp/libhostarq.h"

namespace py = pybind11;

template <typename P>
void add_parameterization(py::module& m)
{
	using namespace sctrltp;
	using namespace py::literals;

	py::class_<ARQStream<P>> arqstream(m, "ARQStream");

	py::enum_<typename ARQStream<P>::Mode>(arqstream, "Mode")
	    .value("NOTHING", ARQStream<P>::NOTHING)
	    .value("NONBLOCK", ARQStream<P>::NONBLOCK)
	    .value("FLUSH", ARQStream<P>::FLUSH)
	    .export_values();

	arqstream.def(py::init<std::string const, bool const>(), "name"_a, "reset"_a = true)
	    .def(
	        py::init<
	            std::string const, std::string const, udpport_t const, std::string const,
	            udpport_t const, bool const>(),
	        "name"_a, "source_ip"_a, "source_port"_a, "target_ip"_a, "target_port"_a,
	        "reset"_a = true)
	    .def(py::init<ARQStreamSettings const>(), "settings"_a)
	    .def(
	        "send",
	        py::overload_cast<packet<P>, typename ARQStream<P>::Mode>(
	            (bool (ARQStream<P>::*)(packet<P>, typename ARQStream<P>::Mode)) &
	            ARQStream<P>::send),
	        "packet"_a, "mode"_a = ARQStream<P>::Mode::FLUSH)
	    .def(
	        "send",
	        [](ARQStream<P>& self, packetid_t pid,
	           std::vector<typename packet<P>::entry_t> const& payload,
	           typename ARQStream<P>::Mode mode) {
		        self.send(pid, payload.begin(), payload.end(), mode);
	        },
	        "pid"_a, "payload"_a, "mode"_a = ARQStream<P>::Mode::FLUSH)
	    .def(
	        "receive",
	        py::overload_cast<typename sctrltp::packet<P>&, typename ARQStream<P>::Mode>(
	            &ARQStream<P>::receive),
	        "packet"_a, "mode"_a = ARQStream<P>::Mode::NONBLOCK)
	    .def(
	        "receive",
	        py::overload_cast<
	            typename sctrltp::packet<P>&, packetid_t, typename ARQStream<P>::Mode>(
	            &ARQStream<P>::receive),
	        "packet"_a, "pid"_a, "mode"_a = ARQStream<P>::Mode::NONBLOCK)
	    .def("flush", &ARQStream<P>::flush)
	    .def(
	        "received_packet_available",
	        (bool (sctrltp::ARQStream<P>::*)() const) & ARQStream<P>::received_packet_available)
	    .def(
	        "received_packet_available",
	        (bool (sctrltp::ARQStream<P>::*)(packetid_t) const) &
	            ARQStream<P>::received_packet_available,
	        "pid"_a)
	    .def("all_packets_sent", &ARQStream<P>::all_packets_sent)
	    .def("send_buffer_full", &ARQStream<P>::send_buffer_full);

	py::class_<packet<P>> pypacket(m, "packet");
	pypacket.def(py::init<>())
	    .def_readwrite("pid", &packet<P>::pid)
	    .def_readwrite("len", &packet<P>::len)
	    .def(
	        "__getitem__",
	        [](packet<P> const& p, size_t const idx) {
		        if (idx >= p.len) {
			        throw py::index_error();
		        }
		        return p.pdu[idx];
	        })
	    .def(
	        "__setitem__", [](packet<P>& p, size_t const idx, typename packet<P>::entry_t const e) {
		        if (idx >= p.len) {
			        throw py::index_error();
		        }
		        p.pdu[idx] = e;
	        });
}

void add_all_parameterizations(py::module& m)
{
	using namespace sctrltp;

#define PARAMETERISATION(Name, name)                                                               \
	{                                                                                              \
		auto lm = m.def_submodule(#name);                                                          \
		add_parameterization<Name>(lm);                                                            \
	}
#include "sctrltp/parameters.def"
}

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
	m.def("open", &hostarq_open<ParametersFcpBss1>);
	m.def("close", &hostarq_close);

	py::class_<sctrltp::ARQStreamSettings> settings(m, "ARQStreamSettings");
	settings.def(py::init<>())
	    .def_readwrite("ip", &ARQStreamSettings::ip)
	    .def_readwrite("reset", &ARQStreamSettings::reset)
	    .def_readwrite("port_data", &ARQStreamSettings::port_data)
	    .def_readwrite("port_reset", &ARQStreamSettings::port_reset)
	    .def_readwrite("local_port_data", &ARQStreamSettings::local_port_data)
	    .def_readwrite("unique_queues", &ARQStreamSettings::unique_queues)
	    .def_readwrite("init_flush_lb_packet", &ARQStreamSettings::init_flush_lb_packet)
	    .def_readwrite("init_flush_timeout", &ARQStreamSettings::init_flush_timeout)
	    .def_readwrite("destruction_timeout", &ARQStreamSettings::destruction_timeout);

	add_all_parameterizations(m);
	m.attr("ARQStream") = m.attr("fcp_bss1").attr("ARQStream");
	m.attr("packet") = m.attr("fcp_bss1").attr("packet");

	return m.ptr();
}
