#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "Protocol.hpp"

namespace py = pybind11;

static CANyonero::Bytes bytes_from_buffer(const py::buffer& buffer) {
    py::buffer_info info = buffer.request();
    if (info.ndim != 1) {
        throw std::runtime_error("Expected a 1-D buffer");
    }
    const auto* ptr = static_cast<const uint8_t*>(info.ptr);
    return CANyonero::Bytes(ptr, ptr + info.size * info.itemsize);
}

static py::bytes bytes_to_py(const CANyonero::Bytes& bytes) {
    return py::bytes(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

PYBIND11_MODULE(canyonero_py, m) {
    m.doc() = "Python bindings for libCANyonero PDUs";

    py::enum_<CANyonero::ChannelProtocol>(m, "ChannelProtocol")
        .value("raw", CANyonero::ChannelProtocol::raw)
        .value("isotp", CANyonero::ChannelProtocol::isotp)
        .value("kline", CANyonero::ChannelProtocol::kline)
        .value("can_fd", CANyonero::ChannelProtocol::can_fd)
        .value("isotp_fd", CANyonero::ChannelProtocol::isotp_fd)
        .value("raw_with_fc", CANyonero::ChannelProtocol::raw_with_fc)
        .value("enet", CANyonero::ChannelProtocol::enet)
        .export_values();

    py::enum_<CANyonero::PDUType>(m, "PDUType")
        .value("ping", CANyonero::PDUType::ping)
        .value("request_info", CANyonero::PDUType::requestInfo)
        .value("read_voltage", CANyonero::PDUType::readVoltage)
        .value("open_channel", CANyonero::PDUType::openChannel)
        .value("close_channel", CANyonero::PDUType::closeChannel)
        .value("send", CANyonero::PDUType::send)
        .value("set_arbitration", CANyonero::PDUType::setArbitration)
        .value("start_periodic_message", CANyonero::PDUType::startPeriodicMessage)
        .value("end_periodic_message", CANyonero::PDUType::endPeriodicMessage)
        .value("send_compressed", CANyonero::PDUType::sendCompressed)
        .value("prepare_for_update", CANyonero::PDUType::prepareForUpdate)
        .value("send_update_data", CANyonero::PDUType::sendUpdateData)
        .value("commit_update", CANyonero::PDUType::commitUpdate)
        .value("reset", CANyonero::PDUType::reset)
        .value("rpc_call", CANyonero::PDUType::rpcCall)
        .value("rpc_send_binary", CANyonero::PDUType::rpcSendBinary)
        .value("ok", CANyonero::PDUType::ok)
        .value("pong", CANyonero::PDUType::pong)
        .value("info", CANyonero::PDUType::info)
        .value("voltage", CANyonero::PDUType::voltage)
        .value("channel_opened", CANyonero::PDUType::channelOpened)
        .value("channel_closed", CANyonero::PDUType::channelClosed)
        .value("received", CANyonero::PDUType::received)
        .value("received_compressed", CANyonero::PDUType::receivedCompressed)
        .value("periodic_message_started", CANyonero::PDUType::periodicMessageStarted)
        .value("periodic_message_ended", CANyonero::PDUType::periodicMessageEnded)
        .value("rpc_response", CANyonero::PDUType::rpcResponse)
        .value("rpc_binary_response", CANyonero::PDUType::rpcBinaryResponse)
        .value("error_unspecified", CANyonero::PDUType::errorUnspecified)
        .value("error_hardware", CANyonero::PDUType::errorHardware)
        .value("error_invalid_channel", CANyonero::PDUType::errorInvalidChannel)
        .value("error_invalid_periodic", CANyonero::PDUType::errorInvalidPeriodic)
        .value("error_no_response", CANyonero::PDUType::errorNoResponse)
        .value("error_invalid_rpc", CANyonero::PDUType::errorInvalidRPC)
        .value("error_invalid_command", CANyonero::PDUType::errorInvalidCommand)
        .export_values();

    py::class_<CANyonero::Info>(m, "Info")
        .def(py::init<>())
        .def_readonly("vendor", &CANyonero::Info::vendor)
        .def_readonly("model", &CANyonero::Info::model)
        .def_readonly("hardware", &CANyonero::Info::hardware)
        .def_readonly("serial", &CANyonero::Info::serial)
        .def_readonly("firmware", &CANyonero::Info::firmware);

    py::class_<CANyonero::Arbitration>(m, "Arbitration")
        .def(py::init<uint32_t, uint32_t, uint32_t, uint8_t, uint8_t>(),
             py::arg("request") = 0,
             py::arg("reply_pattern") = 0,
             py::arg("reply_mask") = 0xFFFFFFFF,
             py::arg("request_extension") = 0,
             py::arg("reply_extension") = 0)
        .def_readwrite("request", &CANyonero::Arbitration::request)
        .def_readwrite("reply_pattern", &CANyonero::Arbitration::replyPattern)
        .def_readwrite("reply_mask", &CANyonero::Arbitration::replyMask)
        .def_readwrite("request_extension", &CANyonero::Arbitration::requestExtension)
        .def_readwrite("reply_extension", &CANyonero::Arbitration::replyExtension)
        .def("to_bytes", [](const CANyonero::Arbitration& arbitration) {
            CANyonero::Bytes payload;
            arbitration.to_vector(payload);
            return bytes_to_py(payload);
        });

    py::class_<CANyonero::PDU>(m, "PDU")
        .def(py::init<CANyonero::PDUType>())
        .def_static("from_frame", [](const py::buffer& frame) {
            return CANyonero::PDU(bytes_from_buffer(frame));
        })
        .def("frame", [](const CANyonero::PDU& pdu) {
            return bytes_to_py(pdu.frame());
        })
        .def_property_readonly("type", &CANyonero::PDU::type)
        .def("payload", [](const CANyonero::PDU& pdu) {
            return bytes_to_py(pdu.payload());
        })
        .def("data", [](const CANyonero::PDU& pdu) {
            return bytes_to_py(pdu.data());
        })
        .def("uncompressed_data", [](const CANyonero::PDU& pdu) {
            return bytes_to_py(pdu.uncompressedData());
        })
        .def("uncompressed_length", &CANyonero::PDU::uncompressedLength)
        .def("information", &CANyonero::PDU::information)
        .def("arbitration", &CANyonero::PDU::arbitration)
        .def("channel", &CANyonero::PDU::channel)
        .def("periodic_message", &CANyonero::PDU::periodicMessage)
        .def("protocol", &CANyonero::PDU::protocol)
        .def("bitrate", &CANyonero::PDU::bitrate)
        .def("separation_times", &CANyonero::PDU::separationTimes)
        .def("milliseconds", &CANyonero::PDU::milliseconds)
        .def("filename", &CANyonero::PDU::filename)
        .def_static("ping", [](const py::buffer& payload) {
            return CANyonero::PDU::ping(bytes_from_buffer(payload));
        }, py::arg("payload") = py::bytes())
        .def_static("request_info", &CANyonero::PDU::requestInfo)
        .def_static("read_voltage", &CANyonero::PDU::readVoltage)
        .def_static("open_channel", &CANyonero::PDU::openChannel)
        .def_static("close_channel", &CANyonero::PDU::closeChannel)
        .def_static("send", [](CANyonero::ChannelHandle handle, const py::buffer& data) {
            return CANyonero::PDU::send(handle, bytes_from_buffer(data));
        })
        .def_static("send_compressed", [](CANyonero::ChannelHandle handle, const py::buffer& data) {
            return CANyonero::PDU::sendCompressed(handle, bytes_from_buffer(data));
        })
        .def_static("set_arbitration", &CANyonero::PDU::setArbitration)
        .def_static("start_periodic_message", [](uint8_t interval, const CANyonero::Arbitration& arbitration, const py::buffer& data) {
            return CANyonero::PDU::startPeriodicMessage(interval, arbitration, bytes_from_buffer(data));
        })
        .def_static("end_periodic_message", &CANyonero::PDU::endPeriodicMessage)
        .def_static("prepare_for_update", &CANyonero::PDU::prepareForUpdate)
        .def_static("send_update_data", [](const py::buffer& data) {
            return CANyonero::PDU::sendUpdateData(bytes_from_buffer(data));
        })
        .def_static("commit_update", &CANyonero::PDU::commitUpdate)
        .def_static("reset", &CANyonero::PDU::reset)
        .def_static("rpc_call", &CANyonero::PDU::rpcCall)
        .def_static("rpc_send_binary", &CANyonero::PDU::rpcSendBinary)
        .def_static("ok", &CANyonero::PDU::ok)
        .def_static("pong", [](const py::buffer& payload) {
            return CANyonero::PDU::pong(bytes_from_buffer(payload));
        }, py::arg("payload") = py::bytes())
        .def_static("info", &CANyonero::PDU::info)
        .def_static("voltage", &CANyonero::PDU::voltage)
        .def_static("channel_opened", &CANyonero::PDU::channelOpened)
        .def_static("channel_closed", &CANyonero::PDU::channelClosed)
        .def_static("received", [](CANyonero::ChannelHandle handle, uint32_t id, uint8_t extension, const py::buffer& data) {
            return CANyonero::PDU::received(handle, id, extension, bytes_from_buffer(data));
        })
        .def_static("received_compressed", [](CANyonero::ChannelHandle handle, uint32_t id, uint8_t extension, const py::buffer& data) {
            return CANyonero::PDU::receivedCompressed(handle, id, extension, bytes_from_buffer(data));
        })
        .def_static("periodic_message_started", &CANyonero::PDU::periodicMessageStarted)
        .def_static("periodic_message_ended", &CANyonero::PDU::periodicMessageEnded)
        .def_static("rpc_response", &CANyonero::PDU::rpcResponse)
        .def_static("rpc_binary_response", [](const py::buffer& data) {
            return CANyonero::PDU::rpcBinaryResponse(bytes_from_buffer(data));
        })
        .def_static("error_unspecified", &CANyonero::PDU::errorUnspecified)
        .def_static("error_hardware", &CANyonero::PDU::errorHardware)
        .def_static("error_invalid_channel", &CANyonero::PDU::errorInvalidChannel)
        .def_static("error_invalid_periodic", &CANyonero::PDU::errorInvalidPeriodic)
        .def_static("error_no_response", &CANyonero::PDU::errorNoResponse)
        .def_static("error_invalid_rpc", &CANyonero::PDU::errorInvalidRPC)
        .def_static("error_invalid_command", &CANyonero::PDU::errorInvalidCommand)
        .def_static("scan_buffer", [](const py::buffer& data) {
            return CANyonero::PDU::scanBuffer(bytes_from_buffer(data));
        })
        .def_static("exceeds_max_pdu_size", [](const py::buffer& data, size_t max_size) {
            return CANyonero::PDU::exceedsMaxPduSize(bytes_from_buffer(data), max_size);
        })
        .def_static("try_rewrite_ping_to_pong", [](const py::buffer& data) {
            CANyonero::Bytes bytes = bytes_from_buffer(data);
            bool changed = CANyonero::PDU::tryRewritePingToPong(bytes);
            return py::make_tuple(changed, bytes_to_py(bytes));
        })
        .def_static("separation_time_code_from_microseconds", &CANyonero::PDU::separationTimeCodeFromMicroseconds)
        .def_static("microseconds_from_separation_time_code", &CANyonero::PDU::microsecondsFromSeparationTimeCode);
}
