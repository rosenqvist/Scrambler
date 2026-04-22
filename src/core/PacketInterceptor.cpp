#include "core/PacketInterceptor.h"

#include "core/Diagnostics.h"

#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace scrambler::core
{

PacketInterceptor::PacketInterceptor(FlowTracker& flow_tracker, const TargetSet& targets, const EffectConfig& effects)
    : flow_tracker_(flow_tracker), targets_(targets), effect_engine_(effects)
{
}

PacketInterceptor::~PacketInterceptor()
{
    Stop();
}

std::expected<void, StartupError> PacketInterceptor::Start()
{
    if (running_.load())
    {
        return std::unexpected(StartupError::kAlreadyRunning);
    }

    // NETWORK layer with no flags, used to intercept packets (not just sniff)
    // so we can hold, delay or drop packets before reinjecting them
    handle_ = WinDivertOpen("udp", WINDIVERT_LAYER_NETWORK, 0, 0);

    if (handle_ == INVALID_HANDLE_VALUE)
    {
        const DWORD gle = GetLastError();
        LogError("PacketInterceptor: WinDivertOpen failed (GLE={})", gle);
        CountEvent(Counter::kDriverErrors);
        return std::unexpected(MapWinDivertOpenError(gle));
    }

    // Driver side queue constants that are safe and tested.
    constexpr UINT64 kQueueLength = 8192;
    constexpr auto kQueueSizeBytes = static_cast<const UINT64>(8 * 1024 * 1024);

    if (WinDivertSetParam(handle_, WINDIVERT_PARAM_QUEUE_LENGTH, kQueueLength) == 0)
    {
        LogWarn("PacketInterceptor: WinDivertSetParam(QUEUE_LENGTH) failed (GLE={})", GetLastError());
    }
    if (WinDivertSetParam(handle_, WINDIVERT_PARAM_QUEUE_SIZE, kQueueSizeBytes) == 0)
    {
        LogWarn("PacketInterceptor: WinDivertSetParam(QUEUE_SIZE) failed (GLE={})", GetLastError());
    }

    delay_queue_ = std::make_unique<DelayQueue>(handle_);
    delay_queue_->Start();

    running_.store(true);
    thread_ = std::jthread([this]
    {
        CaptureLoop();
    });
    LogInfo("PacketInterceptor running");
    return {};
}

void PacketInterceptor::SetFatalCallback(FatalCallback cb)
{
    fatal_cb_ = std::move(cb);
}

void PacketInterceptor::NotifyFatal(uint32_t gle)
{
    if (fatal_cb_)
    {
        fatal_cb_(gle);
    }
}

void PacketInterceptor::Stop()
{
    // exchange() so a destructor-after-never-started doesn't log a phantom "stopped" event.
    const bool was_running = running_.exchange(false);

    // Stop the capture thread first so nothing new is pushed to delay_queue_
    if (handle_ != INVALID_HANDLE_VALUE)
    {
        WinDivertShutdown(handle_, WINDIVERT_SHUTDOWN_RECV);
    }

    if (thread_.joinable())
    {
        thread_.join();
    }

    if (delay_queue_)
    {
        delay_queue_->Stop();
        delay_queue_.reset();
    }

    // Now fully shut down and close.
    if (handle_ != INVALID_HANDLE_VALUE)
    {
        WinDivertShutdown(handle_, WINDIVERT_SHUTDOWN_SEND);
        WinDivertClose(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }

    if (was_running)
    {
        LogInfo("PacketInterceptor stopped");
    }
}

bool PacketInterceptor::IsRunning() const
{
    return running_.load();
}

// Main packet processing pipeline:
// 1. Capture a UDP packet
// 2. Parse headers and look up which process owns it
// 3. If it belongs to a targeted PID we run it through the effect engine
// 4. Otherwise reinject immediately so non targeted traffic is unaffected
void PacketInterceptor::CaptureLoop()
{
    // Batch config
    constexpr UINT kBatchSize = 128;  // Up to 128 packets per kernel transition
    constexpr UINT kRecvBufferLen = kBatchSize * kStandardMtuSize;

    // Receive buffers
    std::vector<uint8_t> recv_buf(kRecvBufferLen);
    std::vector<WINDIVERT_ADDRESS> recv_addrs(kBatchSize);

    // Send buffers
    std::vector<uint8_t> send_buf;
    send_buf.reserve(kRecvBufferLen);
    std::vector<WINDIVERT_ADDRESS> send_addrs;
    send_addrs.reserve(kBatchSize);

    uint32_t consecutive_failures = 0;

    while (running_.load())
    {
        UINT recv_len = 0;
        UINT addr_len = kBatchSize * sizeof(WINDIVERT_ADDRESS);

        // We can capture about 128 packets in a single kernel call
        if (WinDivertRecvEx(
                handle_, recv_buf.data(), kRecvBufferLen, &recv_len, 0, recv_addrs.data(), &addr_len, nullptr)
            == 0)
        {
            const DWORD gle = GetLastError();
            switch (ClassifyRecvFailure(gle, consecutive_failures, "PacketInterceptor"))
            {
                case RecvFailureAction::kContinue:
                    continue;
                case RecvFailureAction::kExitClean:
                    return;
                case RecvFailureAction::kExitFatal:
                    NotifyFatal(gle);
                    return;
            }
        }

        consecutive_failures = 0;

        UINT num_packets = addr_len / sizeof(WINDIVERT_ADDRESS);

        // Count direction up front. The per-packet loop only reads addr.Outbound
        // when a packet matches a tracked PID, so we'd miss the majority otherwise.
        // recv_addrs is gonna be hot in cache here. Two atomic fetch_adds per batch keep
        // the hot path cost unchanged.
        UINT outbound_count = 0;
        for (UINT i = 0; i < num_packets; ++i)
        {
            if (recv_addrs[i].Outbound != 0)
            {
                ++outbound_count;
            }
        }
        CountEvent(Counter::kPacketsCapturedOutbound, outbound_count);
        CountEvent(Counter::kPacketsCapturedInbound, num_packets - outbound_count);

        uint8_t* current_pkt = recv_buf.data();
        UINT current_remaining = recv_len;

        send_buf.clear();
        send_addrs.clear();

        auto append_immediate_packet = [&](std::span<const uint8_t> packet_data, const WINDIVERT_ADDRESS& packet_addr)
        {
            if (packet_data.size() > kStandardMtuSize)
            {
                static std::atomic<uint64_t> occurrences{0};
                LogRateLimited(occurrences,
                               LogLevel::kWarn,
                               "PacketInterceptor: dropping oversized packet ({} bytes > {} MTU)",
                               packet_data.size(),
                               kStandardMtuSize);
                CountEvent(Counter::kPacketsOversized);
                return false;
            }

            const auto previous_size = send_buf.size();
            send_buf.resize(previous_size + packet_data.size());
            std::memcpy(send_buf.data() + previous_size, packet_data.data(), packet_data.size());
            send_addrs.push_back(packet_addr);
            return true;
        };

        // Walk through the batched buffer
        for (UINT i = 0; i < num_packets; ++i)
        {
            const WINDIVERT_ADDRESS& addr = recv_addrs[i];

            WINDIVERT_IPHDR* ip = nullptr;
            WINDIVERT_UDPHDR* udp = nullptr;
            PVOID next_pkt = nullptr;
            UINT next_len = 0;

            // This helper is needed to find the length of the current packet and get the pointer to the next one
            if (WinDivertHelperParsePacket(current_pkt,
                                           current_remaining,
                                           &ip,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           &udp,
                                           nullptr,
                                           nullptr,
                                           &next_pkt,
                                           &next_len)
                == 0)
            {
                // If a packet is malformed or corrupted we abandon the rest of this batch
                static std::atomic<uint64_t> occurrences{0};
                LogRateLimited(
                    occurrences,
                    LogLevel::kWarn,
                    "PacketInterceptor: WinDivertHelperParsePacket failed, abandoning rest of batch ({} of {})",
                    i,
                    num_packets);
                CountEvent(Counter::kParseFailures);
                break;
            }

            const UINT pkt_len = current_remaining - next_len;
            bool handled = false;

            // Process the individual packet
            if (ip && udp)
            {
                auto tuple = TupleFromPacket(*ip, *udp);
                auto pid = flow_tracker_.LookupPid(tuple, addr.Outbound != 0);

                if (pid != 0 && targets_.Contains(pid))
                {
                    const bool is_outbound = addr.Outbound != 0;
                    const auto now = std::chrono::steady_clock::now();
                    auto owned_packet = OwnedPacket::CopyFrom(std::span(current_pkt, pkt_len),
                                                              addr,
                                                              {.tuple = tuple, .pid = pid, .is_outbound = is_outbound});

                    if (owned_packet)
                    {
                        auto emission = effect_engine_.Process(*owned_packet, now);

                        if (emission.HasEffect(PacketEffectKind::kDrop))
                        {
#ifndef NDEBUG
                            auto addrs = FormatAddresses(tuple.src_addr, tuple.dst_addr);
                            DEBUG_PRINT("[DROP]  PID {:>5} | {}:{} -> {}:{} ({} bytes)",
                                        pid,
                                        addrs.src.data(),
                                        tuple.src_port,
                                        addrs.dst.data(),
                                        tuple.dst_port,
                                        pkt_len);
#endif
                            CountEvent(is_outbound ? Counter::kPacketsDroppedOutbound
                                                   : Counter::kPacketsDroppedInbound);
                        }

                        if (emission.HasEffect(PacketEffectKind::kDelay) && emission.ScheduledCount() > 0)
                        {
#ifndef NDEBUG
                            const auto scheduled_delay =
                                (std::max)(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               emission.scheduled_packets.front().release_at - now),
                                           std::chrono::milliseconds::zero());
                            auto addrs = FormatAddresses(tuple.src_addr, tuple.dst_addr);
                            DEBUG_PRINT("[DELAY] PID {:>5} | {}:{} -> {}:{} ({} bytes) +{}ms{}",
                                        pid,
                                        addrs.src.data(),
                                        tuple.src_port,
                                        addrs.dst.data(),
                                        tuple.dst_port,
                                        pkt_len,
                                        scheduled_delay.count(),
                                        emission.HasEffect(PacketEffectKind::kDelayJitter) ? " [jitter]" : "");
#endif
                            CountEvent(is_outbound ? Counter::kPacketsDelayedOutbound
                                                   : Counter::kPacketsDelayedInbound);
                        }

                        if (emission.HasEffect(PacketEffectKind::kDuplicate))
                        {
#ifndef NDEBUG
                            auto addrs = FormatAddresses(tuple.src_addr, tuple.dst_addr);
                            DEBUG_PRINT("[DUPL]  PID {:>5} | {}:{} -> {}:{} ({} bytes) x{}",
                                        pid,
                                        addrs.src.data(),
                                        tuple.src_port,
                                        addrs.dst.data(),
                                        tuple.dst_port,
                                        pkt_len,
                                        emission.TotalEmittedCount());
#endif
                            const auto duplicate_count = emission.TotalEmittedCount() - 1U;
                            CountEvent(is_outbound ? Counter::kPacketsDuplicatedOutbound
                                                   : Counter::kPacketsDuplicatedInbound,
                                       duplicate_count);
                        }

                        for (const auto& immediate_packet : emission.immediate_packets)
                        {
                            append_immediate_packet(immediate_packet.Bytes(), immediate_packet.addr);
                        }

                        for (auto& scheduled_packet : emission.scheduled_packets)
                        {
                            delay_queue_->Push(scheduled_packet);
                        }

                        handled = true;
                    }
                }
            }

            // Repack packets that should continue through the fast path immediately.
            if (!handled)
            {
                append_immediate_packet(std::span(current_pkt, pkt_len), addr);
            }

            // Advance pointers to the next packet in the batch
            current_pkt = static_cast<uint8_t*>(next_pkt);
            current_remaining = next_len;

            if (current_remaining == 0)
            {
                break;
            }
        }

        // Reinject all untouched packets in a single kernel call
        if (!send_addrs.empty())
        {
            if (WinDivertSendEx(handle_,
                                send_buf.data(),
                                static_cast<UINT>(send_buf.size()),
                                nullptr,
                                0,
                                send_addrs.data(),
                                static_cast<UINT>(send_addrs.size() * sizeof(WINDIVERT_ADDRESS)),
                                nullptr)
                == 0)
            {
                static std::atomic<uint64_t> occurrences{0};
                LogRateLimited(occurrences,
                               LogLevel::kWarn,
                               "PacketInterceptor: batch WinDivertSendEx failed (GLE={}, packets={})",
                               GetLastError(),
                               send_addrs.size());
                CountEvent(Counter::kReinjectFailures, static_cast<uint64_t>(send_addrs.size()));
            }
            else
            {
                CountEvent(Counter::kPacketsReinjected, static_cast<uint64_t>(send_addrs.size()));
            }
        }
    }
}

}  // namespace scrambler::core
