#include "core/PacketEffectEngine.h"

namespace scrambler::core
{

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

    PacketEffectEmission emission;
    if (ShouldDrop(snapshot.drop_rate, rng_))
    {
        emission.applied_effects |= ToMask(PacketEffectKind::kDrop);
        return emission;
    }

    const bool should_duplicate = snapshot.duplicate_count > 0 && ShouldApplyRate(snapshot.duplicate_rate, rng_);
    const size_t total_copy_count = should_duplicate ? static_cast<size_t>(snapshot.duplicate_count + 1) : 1U;
    if (should_duplicate)
    {
        emission.applied_effects |= ToMask(PacketEffectKind::kDuplicate);
    }

    const bool has_base_delay = snapshot.delay.count() > 0;
    const bool has_jitter = snapshot.delay_jitter_ms > 0;
    if (has_base_delay || has_jitter)
    {
        auto scheduled_delay = snapshot.delay;
        if (has_base_delay)
        {
            emission.applied_effects |= ToMask(PacketEffectKind::kDelay);
        }
        if (has_jitter)
        {
            const int jitter_ms = std::uniform_int_distribution<int>(0, snapshot.delay_jitter_ms)(rng_);
            scheduled_delay += std::chrono::milliseconds(jitter_ms);
            emission.applied_effects |= ToMask(PacketEffectKind::kDelayJitter);
        }

        const auto release_at = now + scheduled_delay;
        emission.scheduled_packets.reserve(total_copy_count);
        for (size_t copy_index = 0; copy_index < total_copy_count; ++copy_index)
        {
            emission.AddScheduled(packet, release_at);
        }
        return emission;
    }

    emission.immediate_packets.reserve(total_copy_count);
    for (size_t copy_index = 0; copy_index < total_copy_count; ++copy_index)
    {
        emission.AddImmediate(packet);
    }
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
