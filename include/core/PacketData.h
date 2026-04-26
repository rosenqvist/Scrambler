#pragma once

#include "core/Types.h"

#include <chrono>
#include <cstring>
#include <optional>
#include <span>

namespace scrambler::core
{

struct PacketMetadata
{
    FiveTuple tuple{};
    uint32_t pid = 0;
    bool is_outbound = false;
};

struct OwnedPacket
{
    std::array<uint8_t, kStandardMtuSize> data{};
    UINT length = 0;
    WINDIVERT_ADDRESS addr{};
    PacketMetadata metadata{};

    [[nodiscard]] std::span<const uint8_t> Bytes() const
    {
        return std::span(data.data(), static_cast<size_t>(length));
    }

    [[nodiscard]] static std::optional<OwnedPacket> CopyFrom(std::span<const uint8_t> packet_data,
                                                             const WINDIVERT_ADDRESS& addr,
                                                             const PacketMetadata& metadata)
    {
        if (packet_data.size() > kStandardMtuSize)
        {
            return std::nullopt;
        }

        OwnedPacket packet;
        packet.length = static_cast<UINT>(packet_data.size());
        packet.addr = addr;
        packet.metadata = metadata;
        std::memcpy(packet.data.data(), packet_data.data(), packet_data.size());
        return packet;
    }
};

struct ScheduledPacket
{
    OwnedPacket packet{};
    std::chrono::steady_clock::time_point release_at;
};

}  // namespace scrambler::core
