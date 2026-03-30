#include "core/PacketInterceptor.h"

#include <array>
#include <print>

namespace scrambler::core
{

PacketInterceptor::PacketInterceptor(FlowTracker& flow_tracker, const TargetSet& targets, const EffectConfig& effects)
    : flow_tracker_(flow_tracker), targets_(targets), effects_(effects)
{
}

PacketInterceptor::~PacketInterceptor()
{
    Stop();
}

bool PacketInterceptor::Start()
{
    // NETWORK layer with no flags, used to intercept packets (not just sniff)
    // so we can hold, delay or drop packets before reinjecting them
    handle_ = WinDivertOpen("udp", WINDIVERT_LAYER_NETWORK, 0, 0);

    if (handle_ == INVALID_HANDLE_VALUE)
    {
        std::println("[NET] WinDivertOpen failed: {}", GetLastError());
        return false;
    }

    delay_queue_ = std::make_unique<DelayQueue>(handle_);
    delay_queue_->Start();

    running_.store(true);
    thread_ = std::jthread([this]
    {
        CaptureLoop();
    });
    std::println("[NET] Capturing UDP packets...");
    return true;
}

void PacketInterceptor::Stop()
{
    running_.store(false);

    if (delay_queue_)
    {
        delay_queue_->Stop();
    }

    if (handle_ != INVALID_HANDLE_VALUE)
    {
        WinDivertClose(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }

    delay_queue_.reset();
}

bool PacketInterceptor::IsRunning() const
{
    return running_.load();
}

void PacketInterceptor::Reinject(const uint8_t* data, UINT len, const WINDIVERT_ADDRESS& addr)
{
    WinDivertSend(handle_, data, len, nullptr, &addr);
}

// Main packet processing pipeline:
// 1. Capture a UDP packet
// 2. Parse headers, look up which process owns it
// 3. If it belongs to a targeted PID, apply effects (drop or delay)
// 4. Otherwise reinject immediately so non-targeted traffic is unaffected
void PacketInterceptor::CaptureLoop()
{
    std::array<uint8_t, kMaxPacketSize> packet{};
    WINDIVERT_ADDRESS addr{};
    UINT len = 0;

    while (running_.load())
    {
        if (WinDivertRecv(handle_, packet.data(), static_cast<UINT>(packet.size()), &len, &addr) == 0)
        {
            continue;
        }

        WINDIVERT_IPHDR* ip = nullptr;
        WINDIVERT_UDPHDR* udp = nullptr;

        if (!ParseUdpPacket(packet.data(), len, ip, udp))
        {
            Reinject(packet.data(), len, addr);
            continue;
        }

        auto tuple = TupleFromPacket(*ip, *udp);
        auto pid = flow_tracker_.LookupPid(tuple);

        if (pid == 0 || !targets_.Contains(pid))
        {
            Reinject(packet.data(), len, addr);
            continue;
        }

        if (effects_.ShouldDrop())
        {
            auto addrs = FormatAddresses(tuple.src_addr, tuple.dst_addr);
            std::println("[DROP]  PID {:>5} | {}:{} -> {}:{} ({} bytes)",
                         pid,
                         addrs.src.data(),
                         tuple.src_port,
                         addrs.dst.data(),
                         tuple.dst_port,
                         len);
            continue;
        }

        auto delay = effects_.Delay();
        if (delay.count() > 0)
        {
            auto addrs = FormatAddresses(tuple.src_addr, tuple.dst_addr);
            std::println("[DELAY] PID {:>5} | {}:{} -> {}:{} ({} bytes) +{}ms",
                         pid,
                         addrs.src.data(),
                         tuple.src_port,
                         addrs.dst.data(),
                         tuple.dst_port,
                         len,
                         delay.count());
            delay_queue_->Push(packet.data(), len, addr, delay);
            continue;
        }

        Reinject(packet.data(), len, addr);
    }
}

}  // namespace scrambler::core
