#include "core/PacketEffectEngine.h"

#include <utility>

namespace scrambler::core
{

bool PacketEffectEmission::AddImmediate(OwnedPacket packet)
{
    if (immediate_count >= immediate_packets.size())
    {
        return false;
    }

    immediate_packets[immediate_count++] = std::move(packet);
    return true;
}

bool PacketEffectEmission::AddScheduled(OwnedPacket packet, std::chrono::steady_clock::time_point release_at)
{
    if (scheduled_count >= scheduled_packets.size())
    {
        return false;
    }

    scheduled_packets[scheduled_count++] = {.packet = std::move(packet), .release_at = release_at};
    return true;
}

bool PacketEffectEmission::HasEffect(PacketEffectKind kind) const
{
    return (applied_effects & ToMask(kind)) != 0;
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

    if (snapshot.delay.count() > 0)
    {
        emission.applied_effects |= ToMask(PacketEffectKind::kDelay);
        emission.AddScheduled(std::move(packet), now + snapshot.delay);
        return emission;
    }

    emission.AddImmediate(std::move(packet));
    return emission;
}

PacketEffectEngine::PacketEffectEngine(const EffectConfig& effects) : outbound_(effects.outbound), inbound_(effects.inbound)
{
}

PacketEffectEmission PacketEffectEngine::Process(OwnedPacket packet, std::chrono::steady_clock::time_point now)
{
    return packet.metadata.is_outbound ? outbound_.Process(std::move(packet), now) : inbound_.Process(std::move(packet), now);
}

}  // namespace scrambler::core
