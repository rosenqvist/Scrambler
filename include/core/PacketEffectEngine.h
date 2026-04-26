#pragma once

#include "core/EffectConfig.h"
#include "core/PacketData.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace scrambler::core
{

enum class PacketEffectKind : std::uint8_t
{
    kDrop = 1U << 0,
    kDelay = 1U << 1,
    kDuplicate = 1U << 2,
    kBandwidthThrottle = 1U << 3,
    kBurstDrop = 1U << 4,
    kDelayJitter = 1U << 5,
};

using PacketEffectMask = std::uint8_t;

[[nodiscard]] constexpr PacketEffectMask ToMask(PacketEffectKind kind)
{
    return std::to_underlying(kind);
}

struct PacketEffectEmission
{
    PacketEffectMask applied_effects = 0;
    std::vector<OwnedPacket> immediate_packets;
    std::vector<ScheduledPacket> scheduled_packets;

    void AddImmediate(const OwnedPacket& packet);
    void AddScheduled(const OwnedPacket& packet, std::chrono::steady_clock::time_point release_at);

    [[nodiscard]] bool HasEffect(PacketEffectKind kind) const;
    [[nodiscard]] size_t ImmediateCount() const;
    [[nodiscard]] size_t ScheduledCount() const;
    [[nodiscard]] size_t TotalEmittedCount() const;
};

class DirectionPacketEffectEngine
{
public:
    explicit DirectionPacketEffectEngine(const DirectionEffectConfig& config);

    [[nodiscard]] PacketEffectEmission Process(const OwnedPacket& packet, std::chrono::steady_clock::time_point now);

private:
    [[nodiscard]] static std::chrono::steady_clock::duration TransmissionTimeForPacket(
        size_t packet_length,
        std::uint64_t throttle_bytes_per_second);
    void ResetThrottleStateIfNeeded(const DirectionEffectSnapshot& snapshot);
    [[nodiscard]] bool ShouldDropPacket(const DirectionEffectSnapshot& snapshot, PacketEffectEmission& emission);
    [[nodiscard]] std::chrono::milliseconds ResolveScheduledDelay(const DirectionEffectSnapshot& snapshot,
                                                                  PacketEffectEmission& emission);
    void EmitPacketCopies(const OwnedPacket& packet,
                          size_t total_copy_count,
                          std::chrono::steady_clock::time_point now,
                          std::chrono::milliseconds scheduled_delay,
                          std::uint64_t throttle_bytes_per_second,
                          PacketEffectEmission& emission);

    const DirectionEffectConfig* config_ = nullptr;
    std::mt19937 rng_;
    int burst_packets_remaining_ = 0;
    std::chrono::steady_clock::time_point next_throttle_release_at_;
    std::uint64_t last_throttle_revision_ = 0;
};

class PacketEffectEngine
{
public:
    explicit PacketEffectEngine(const EffectConfig& effects);

    [[nodiscard]] PacketEffectEmission Process(const OwnedPacket& packet, std::chrono::steady_clock::time_point now);

private:
    DirectionPacketEffectEngine outbound_;
    DirectionPacketEffectEngine inbound_;
};

}  // namespace scrambler::core
