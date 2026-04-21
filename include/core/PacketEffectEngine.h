#pragma once

#include "core/EffectConfig.h"
#include "core/PacketData.h"

#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

namespace scrambler::core
{

enum class PacketEffectKind : std::uint8_t
{
    kDrop = 1U << 0,
    kDelay = 1U << 1,
    kDuplicate = 1U << 2,
    kBandwidthThrottle = 1U << 3,
    kReorder = 1U << 4,
    kBurstDrop = 1U << 5,
    kDelayJitter = 1U << 6,
};

using PacketEffectMask = std::uint8_t;

[[nodiscard]] constexpr PacketEffectMask ToMask(PacketEffectKind kind)
{
    return static_cast<PacketEffectMask>(kind);
}

struct PacketEffectEmission
{
    PacketEffectMask applied_effects = 0;
    std::vector<OwnedPacket> immediate_packets;
    std::vector<ScheduledPacket> scheduled_packets;

    void AddImmediate(OwnedPacket packet);
    void AddScheduled(OwnedPacket packet, std::chrono::steady_clock::time_point release_at);

    [[nodiscard]] bool HasEffect(PacketEffectKind kind) const;
    [[nodiscard]] size_t ImmediateCount() const;
    [[nodiscard]] size_t ScheduledCount() const;
    [[nodiscard]] size_t TotalEmittedCount() const;
};

class DirectionPacketEffectEngine
{
public:
    explicit DirectionPacketEffectEngine(const DirectionEffectConfig& config);

    [[nodiscard]] PacketEffectEmission Process(OwnedPacket packet, std::chrono::steady_clock::time_point now);

private:
    // Future bandwidth throttling, reordering, and burst-loss policies live here
    // as long-lived per-direction stateful members.
    const DirectionEffectConfig* config_ = nullptr;
    std::mt19937 rng_;
};

class PacketEffectEngine
{
public:
    explicit PacketEffectEngine(const EffectConfig& effects);

    [[nodiscard]] PacketEffectEmission Process(OwnedPacket packet, std::chrono::steady_clock::time_point now);

private:
    DirectionPacketEffectEngine outbound_;
    DirectionPacketEffectEngine inbound_;
};

}  // namespace scrambler::core
