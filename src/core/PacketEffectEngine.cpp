#include "core/PacketEffectEngine.h"

#include "core/EffectConfig.h"
#include "core/PacketData.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>

namespace scrambler::core
{

std::chrono::steady_clock::duration DirectionPacketEffectEngine::TransmissionTimeForPacket(
    size_t packet_length,
    std::uint64_t throttle_bytes_per_second)
{
    if (packet_length == 0 || throttle_bytes_per_second == 0)
    {
        return std::chrono::steady_clock::duration::zero();
    }

    const auto packet_bytes = static_cast<std::uint64_t>(packet_length);
    const auto transmission_nanos =
        ((packet_bytes * 1'000'000'000ULL) + throttle_bytes_per_second - 1ULL) / throttle_bytes_per_second;
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::nanoseconds((std::max)(transmission_nanos, 1ULL)));
}

void DirectionPacketEffectEngine::ResetThrottleStateIfNeeded(const DirectionEffectSnapshot& snapshot)
{
    if (snapshot.throttle_revision != last_throttle_revision_)
    {
        next_throttle_release_at_ = {};
        last_throttle_revision_ = snapshot.throttle_revision;
    }
}

bool DirectionPacketEffectEngine::ShouldDropPacket(const DirectionEffectSnapshot& snapshot,
                                                   PacketEffectEmission& emission)
{
    if (!snapshot.burst_drop_enabled)
    {
        burst_packets_remaining_ = 0;
    }
    else
    {
        if (burst_packets_remaining_ > 0)
        {
            --burst_packets_remaining_;
            emission.applied_effects |= ToMask(PacketEffectKind::kDrop);
            emission.applied_effects |= ToMask(PacketEffectKind::kBurstDrop);
            return true;
        }

        if (ShouldApplyRate(snapshot.burst_drop_rate, rng_))
        {
            burst_packets_remaining_ = snapshot.burst_drop_length - 1;
            emission.applied_effects |= ToMask(PacketEffectKind::kDrop);
            emission.applied_effects |= ToMask(PacketEffectKind::kBurstDrop);
            return true;
        }
    }

    if (!snapshot.burst_drop_enabled && ShouldDrop(snapshot.drop_rate, rng_))
    {
        emission.applied_effects |= ToMask(PacketEffectKind::kDrop);
        return true;
    }

    return false;
}

std::chrono::milliseconds DirectionPacketEffectEngine::ResolveScheduledDelay(const DirectionEffectSnapshot& snapshot,
                                                                             PacketEffectEmission& emission)
{
    auto scheduled_delay = snapshot.delay;
    if (snapshot.delay.count() > 0)
    {
        emission.applied_effects |= ToMask(PacketEffectKind::kDelay);
    }

    if (snapshot.delay_jitter_ms > 0)
    {
        const int jitter_ms = std::uniform_int_distribution<int>(0, snapshot.delay_jitter_ms)(rng_);
        scheduled_delay += std::chrono::milliseconds(jitter_ms);
        emission.applied_effects |= ToMask(PacketEffectKind::kDelayJitter);
    }

    return scheduled_delay;
}

void DirectionPacketEffectEngine::EmitPacketCopies(const OwnedPacket& packet,
                                                   size_t total_copy_count,
                                                   std::chrono::steady_clock::time_point now,
                                                   std::chrono::milliseconds scheduled_delay,
                                                   std::uint64_t throttle_bytes_per_second,
                                                   PacketEffectEmission& emission)
{
    const bool has_throttle = throttle_bytes_per_second > 0;
    const auto base_release_at = now + scheduled_delay;
    auto throttle_release_at = next_throttle_release_at_ < now ? now : next_throttle_release_at_;
    const auto transmission_time = TransmissionTimeForPacket(packet.length, throttle_bytes_per_second);
    bool applied_throttle = false;

    emission.immediate_packets.reserve(total_copy_count);
    emission.scheduled_packets.reserve(total_copy_count);

    for (size_t copy_index = 0; copy_index < total_copy_count; ++copy_index)
    {
        auto release_at = base_release_at;
        if (has_throttle)
        {
            const auto throttled_release_at = (std::max)(release_at, throttle_release_at);
            if (throttled_release_at > release_at)
            {
                applied_throttle = true;
            }
            release_at = throttled_release_at;
            throttle_release_at = throttled_release_at + transmission_time;
        }

        if (release_at <= now)
        {
            emission.AddImmediate(packet);
        }
        else
        {
            emission.AddScheduled(packet, release_at);
        }
    }

    if (has_throttle)
    {
        next_throttle_release_at_ = throttle_release_at;
        if (applied_throttle)
        {
            emission.applied_effects |= ToMask(PacketEffectKind::kBandwidthThrottle);
        }
    }
}

void PacketEffectEmission::AddImmediate(OwnedPacket packet)
{
    immediate_packets.push_back(packet);
}

void PacketEffectEmission::AddScheduled(OwnedPacket packet, std::chrono::steady_clock::time_point release_at)
{
    scheduled_packets.push_back({.packet = packet, .release_at = release_at});
}

bool PacketEffectEmission::HasEffect(PacketEffectKind kind) const
{
    return (applied_effects & ToMask(kind)) != 0;
}

size_t PacketEffectEmission::ImmediateCount() const
{
    return immediate_packets.size();
}

size_t PacketEffectEmission::ScheduledCount() const
{
    return scheduled_packets.size();
}

size_t PacketEffectEmission::TotalEmittedCount() const
{
    return ImmediateCount() + ScheduledCount();
}

DirectionPacketEffectEngine::DirectionPacketEffectEngine(const DirectionEffectConfig& config)
    : config_(&config), rng_(std::random_device{}())
{
}

PacketEffectEmission DirectionPacketEffectEngine::Process(OwnedPacket packet, std::chrono::steady_clock::time_point now)
{
    const DirectionEffectSnapshot snapshot = config_->Snapshot();
    ResetThrottleStateIfNeeded(snapshot);

    PacketEffectEmission emission;
    if (ShouldDropPacket(snapshot, emission))
    {
        return emission;
    }

    size_t total_copy_count = 1U;
    if (snapshot.duplicate_count > 0 && ShouldApplyRate(snapshot.duplicate_rate, rng_))
    {
        total_copy_count += static_cast<size_t>(snapshot.duplicate_count);
        emission.applied_effects |= ToMask(PacketEffectKind::kDuplicate);
    }

    const auto scheduled_delay = ResolveScheduledDelay(snapshot, emission);
    EmitPacketCopies(packet, total_copy_count, now, scheduled_delay, snapshot.throttle_bytes_per_second, emission);
    return emission;
}

PacketEffectEngine::PacketEffectEngine(const EffectConfig& effects)
    : outbound_(effects.outbound), inbound_(effects.inbound)
{
}

PacketEffectEmission PacketEffectEngine::Process(OwnedPacket packet, std::chrono::steady_clock::time_point now)
{
    return packet.metadata.is_outbound ? outbound_.Process(packet, now) : inbound_.Process(packet, now);
}

}  // namespace scrambler::core
