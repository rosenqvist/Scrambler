#include "core/PacketEffectEngine.h"

#include <utility>

namespace scrambler::core
{

void PacketEffectEmission::AddImmediate(OwnedPacket packet)
{
    immediate_packets.push_back(std::move(packet));
}

void PacketEffectEmission::AddScheduled(OwnedPacket packet, std::chrono::steady_clock::time_point release_at)
{
    scheduled_packets.push_back({.packet = std::move(packet), .release_at = release_at});
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

    if (snapshot.delay.count() > 0)
    {
        emission.applied_effects |= ToMask(PacketEffectKind::kDelay);
        const auto release_at = now + snapshot.delay;
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
    return packet.metadata.is_outbound ? outbound_.Process(std::move(packet), now)
                                       : inbound_.Process(std::move(packet), now);
}

}  // namespace scrambler::core
