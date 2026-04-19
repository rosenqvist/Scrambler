#include "core/PacketInterceptor.h"

#include "core/Diagnostics.h"

#include <array>
#include <cstddef>
#include <span>

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
// 3. If it belongs to a targeted PID we apply effects (drop or delay)
// 4. Otherwise reinject immediately so non targeted traffic is unaffected
void PacketInterceptor::CaptureLoop()
{
    // Batch config
    constexpr UINT kBatchSize = 128;  // Up to 128 packets per kernel transition
    constexpr UINT kBatchBufferLen = kBatchSize * kStandardMtuSize;

    // Receive buffers
    std::vector<uint8_t> recv_buf(kBatchBufferLen);
    std::vector<WINDIVERT_ADDRESS> recv_addrs(kBatchSize);

    // Send (Repack) buffers
    std::vector<uint8_t> send_buf(kBatchBufferLen);
    std::vector<WINDIVERT_ADDRESS> send_addrs(kBatchSize);

    uint32_t consecutive_failures = 0;

    while (running_.load())
    {
        UINT recv_len = 0;
        UINT addr_len = kBatchSize * sizeof(WINDIVERT_ADDRESS);

        // We can capture about 128 packets in a single kernel call
        if (WinDivertRecvEx(
                handle_, recv_buf.data(), kBatchBufferLen, &recv_len, 0, recv_addrs.data(), &addr_len, nullptr)
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

        UINT send_len = 0;
        UINT send_count = 0;

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

            UINT pkt_len = current_remaining - next_len;
            bool handled = false;

            // Process the individual packet
            if (ip && udp)
            {
                auto tuple = TupleFromPacket(*ip, *udp);
                auto pid = flow_tracker_.LookupPid(tuple);

                if (pid != 0 && targets_.Contains(pid))
                {
                    bool is_outbound = addr.Outbound != 0;

                    // Apply Drop Effect
                    if (effects_.MatchesDropDirection(is_outbound) && ShouldDrop(effects_.drop_rate.load()))
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
                        CountEvent(is_outbound ? Counter::kPacketsDroppedOutbound : Counter::kPacketsDroppedInbound);
                        handled = true;
                    }
                    // Apply Delay Effect
                    else
                    {
                        auto delay = effects_.Delay();
                        if (delay.count() > 0 && effects_.MatchesDelayDirection(is_outbound))
                        {
#ifndef NDEBUG
                            auto addrs = FormatAddresses(tuple.src_addr, tuple.dst_addr);
                            DEBUG_PRINT("[DELAY] PID {:>5} | {}:{} -> {}:{} ({} bytes) +{}ms",
                                        pid,
                                        addrs.src.data(),
                                        tuple.src_port,
                                        addrs.dst.data(),
                                        tuple.dst_port,
                                        pkt_len,
                                        delay.count());
#endif
                            CountEvent(is_outbound ? Counter::kPacketsDelayedOutbound
                                                   : Counter::kPacketsDelayedInbound);
                            delay_queue_->Push(std::span(current_pkt, pkt_len), addr, delay);
                            handled = true;
                        }
                    }
                }
            }

            // Repack non-targeted packets for bulk reinjection
            if (!handled)
            {
                // A fast user-space memory copy is immensely faster than multiple kernel transitions
                std::memcpy(send_buf.data() + send_len, current_pkt, pkt_len);
                send_addrs[send_count] = addr;

                send_len += pkt_len;
                send_count++;
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
        if (send_count > 0)
        {
            if (WinDivertSendEx(handle_,
                                send_buf.data(),
                                send_len,
                                nullptr,
                                0,
                                send_addrs.data(),
                                send_count * sizeof(WINDIVERT_ADDRESS),
                                nullptr)
                == 0)
            {
                static std::atomic<uint64_t> occurrences{0};
                LogRateLimited(occurrences,
                               LogLevel::kWarn,
                               "PacketInterceptor: batch WinDivertSendEx failed (GLE={}, packets={})",
                               GetLastError(),
                               send_count);
                CountEvent(Counter::kReinjectFailures, send_count);
            }
            else
            {
                CountEvent(Counter::kPacketsReinjected, send_count);
            }
        }
    }
}

}  // namespace scrambler::core
