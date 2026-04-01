#pragma once

#include <windivert.h>

#include <array>
#include <cstdint>
#include <winsock.h>

namespace scrambler::core
{

[[maybe_unused]] constexpr uint32_t kMaxPacketSize = 65535;
constexpr uint32_t kIpStringLength = 64;
// WinDivert stores addresses as 4-element arrays for IPv6 compatibility.
// For IPv4 the address sits in element 0 only.
constexpr int kIPv4AddressIndex = 0;
// bit mixer, constant from boost::hash_combine that helps spread hash bits evenly
// https://www.boost.org/doc/libs/latest/libs/container_hash/doc/html/hash.html#notes_hash_combine
constexpr std::size_t kGoldenRatio = 0x9e3779b9;
// the pid of the windows System process: "System".
// We try to exclude network activity from this process since its mostly noise.
[[maybe_unused]] constexpr uint32_t kSystemPid = 4;

struct FiveTuple
{
    uint32_t src_addr;
    uint32_t dst_addr;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol;

    bool operator==(const FiveTuple&) const = default;

    [[nodiscard]] FiveTuple Reversed() const
    {
        return {.src_addr = dst_addr,
                .dst_addr = src_addr,
                .src_port = dst_port,
                .dst_port = src_port,
                .protocol = protocol};
    }
};

// The goal is to combine all (src_addr, dst_addr, scr_port, dst_port, protocol) into one hash value.
// the reason we do this is to reduce collisions where different tuples might get the same hash value.
// this might also be overkill since chance of a collision happening is negligible at best
// any collision that would occur would also be handled by unordered_map internally
struct FiveTupleHash
{
    std::size_t operator()(const FiveTuple& t) const noexcept
    {
        // Fold each field into the running hash using XOR + bit shifts
        // to reduce collisions across similar tuples
        auto combine = [](std::size_t& s, auto v)
        {
            // Mix the hash of v into s using a variant of boost::hash_combine.
            // The shifts and golden ratio constant help spread the bits around and reduce collisions.
            s ^= std::hash<decltype(v)>{}(v) + kGoldenRatio + (s << 6) + (s >> 2);
        };

        std::size_t h = 0;
        combine(h, t.src_addr);
        combine(h, t.dst_addr);
        combine(h, t.src_port);
        combine(h, t.dst_port);
        combine(h, t.protocol);
        return h;
    }
};

struct IpPair
{
    std::array<char, kIpStringLength> src{};
    std::array<char, kIpStringLength> dst{};
};

inline IpPair FormatAddresses(uint32_t src, uint32_t dst)
{
    IpPair result;
    WinDivertHelperFormatIPv4Address(src, result.src.data(), kIpStringLength);
    WinDivertHelperFormatIPv4Address(dst, result.dst.data(), kIpStringLength);
    return result;
}

inline FiveTuple TupleFromFlow(const WINDIVERT_ADDRESS& addr)
{
    return {
        .src_addr = addr.Flow.LocalAddr[kIPv4AddressIndex],
        .dst_addr = addr.Flow.RemoteAddr[kIPv4AddressIndex],
        .src_port = addr.Flow.LocalPort,
        .dst_port = addr.Flow.RemotePort,
        .protocol = addr.Flow.Protocol,
    };
}

// Build a tuple from captured packet headers.
// Packet headers are in network byte order so we convert to host order
// to match the format used by the FLOW layer.
inline FiveTuple TupleFromPacket(const WINDIVERT_IPHDR& ip, const WINDIVERT_UDPHDR& udp)
{
    return {
        .src_addr = ntohl(ip.SrcAddr),
        .dst_addr = ntohl(ip.DstAddr),
        .src_port = ntohs(udp.SrcPort),
        .dst_port = ntohs(udp.DstPort),
        .protocol = ip.Protocol,
    };
}

// Extract IPv4 and UDP headers from a raw packet buffer.
// Returns false if the packet isn't a valid IPv4/UDP packet.
inline bool ParseUdpPacket(void* data, UINT len, WINDIVERT_IPHDR*& ip, WINDIVERT_UDPHDR*& udp)
{
    ip = nullptr;
    udp = nullptr;
    WinDivertHelperParsePacket(
        data, len, &ip, nullptr, nullptr, nullptr, nullptr, nullptr, &udp, nullptr, nullptr, nullptr, nullptr);
    return ip != nullptr && udp != nullptr;
}

}  // namespace scrambler::core
