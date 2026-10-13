#pragma once
#include "DongleMessages.hpp"
#include <iomanip>
#include <ostream>

namespace dongle_recovered {
namespace rnet_stream_detail {
class StreamState {
public:
    explicit StreamState(std::ostream& os)
        : os_(os), flags_(os.flags()), fill_(os.fill()), precision_(os.precision()) {}
    ~StreamState() { os_.flags(flags_); os_.fill(fill_); os_.precision(precision_); }
private:
    std::ostream& os_;
    std::ios::fmtflags flags_;
    char fill_;
    std::streamsize precision_;
};
inline void HexByte(std::ostream& os, std::uint8_t v) {
    os << std::hex << std::uppercase << std::setfill('0')
       << std::setw(2) << static_cast<unsigned>(v);
}
inline void HexData(std::ostream& os, const CCANMsg& m) {
    for (std::uint32_t i=0; i<m.GetDLC() && i<CCANMsg::DATA_LENGTH; ++i) {
        if (i) os << ' ';
        HexByte(os, m[static_cast<int>(i)]);
    }
}
inline void Node(std::ostream& os, std::uint8_t n) {
    if (n == 0xff) os << '?'; else os << static_cast<unsigned>(n);
}
} // namespace rnet_stream_detail

inline std::ostream& operator<<(std::ostream& os, const CRawMsg& m) {
    rnet_stream_detail::StreamState s(os);
    os << "CRawMsg{type=" << (m.IsUSBMsg() ? "USB" : m.IsCANMsg() ? "CAN" : "UNKNOWN")
       << ", bytes=" << m.GetNumberBytes() << ", raw=";
    for (int i=0; i<m.GetNumberBytes(); ++i) {
        if (i) os << ' ';
        rnet_stream_detail::HexByte(os, m[i]);
    }
    return os << '}';
}
inline std::ostream& operator<<(std::ostream& os, const CUSBMsg& m) {
    rnet_stream_detail::StreamState s(os);
    os << "CUSBMsg{cmd=0x"; rnet_stream_detail::HexByte(os, m.GetCommand());
    os << ", sub=0x"; rnet_stream_detail::HexByte(os, m.GetSubCommand());
    os << ", data=";
    for (std::size_t i=0; i<CUSBMsg::DATA_LENGTH; ++i) {
        if (i) os << ' ';
        rnet_stream_detail::HexByte(os, m[static_cast<int>(i)]);
    }
    return os << '}';
}
inline std::ostream& operator<<(std::ostream& os, const CCANMsg& m) {
    rnet_stream_detail::StreamState s(os);
    os << "CCANMsg{id=0x" << std::hex << std::uppercase << std::setfill('0')
       << std::setw(m.IsExtended()?8:3) << m.GetID()
       << ", " << (m.IsExtended()?"EXT":"STD")
       << ", " << (m.IsRemote()?"RTR":"DATA")
       << ", dlc=" << std::dec << m.GetDLC() << ", data=";
    rnet_stream_detail::HexData(os, m);
    return os << '}';
}
inline std::ostream& operator<<(std::ostream& os, const CPOPMsg& m) {
    rnet_stream_detail::StreamState s(os);
    os << "CPOPMsg{id=0x" << std::hex << std::uppercase << std::setfill('0')
       << std::setw(m.IsExtended()?8:3) << m.GetID()
       << ", " << (m.IsExtended()?"EXT":"STD")
       << ", " << (m.IsRemote()?"RTR":"DATA")
       << ", dlc=" << std::dec << m.GetDLC()
       << ", tc=" << static_cast<unsigned>(m.GetTransferCode())
       << ", role=" << (m.IsServerMsg()?"server":"client")
       << ", client=";
    rnet_stream_detail::Node(os, m.GetClientFromMsg());
    os << ", server="; rnet_stream_detail::Node(os, m.GetServerFromMsg());
    if (m.IsPOPSegMsg()) {
        os << ", segment=" << m.GetSegmentNumber()
           << ", last=" << (m.IsLastSegment()?1:0);
    } else {
        os << ", odi=0x" << std::hex << std::uppercase << std::setfill('0')
           << std::setw(6) << m.GetODI()
           << ", size=" << std::dec << m.GetSize()
           << ", block=" << m.GetBlock()
           << ", quick=" << (m.GetQuickFlag()?1:0)
           << ", crcFlag=" << (m.GetCRCFlag()?1:0);
        if (m.IsAbortMsg())
            os << ", abort=0x" << std::hex << std::uppercase
               << std::setfill('0') << std::setw(8) << m.GetAbortCode();
    }
    os << ", data=";
    rnet_stream_detail::HexData(os, m);
    return os << '}';
}
} // namespace dongle_recovered
