#include "core/PacketInterceptor.h"

#include "core/DelayQueue.h"
#include "core/Diagnostics.h"
#include "core/EffectConfig.h"
#include "core/FlowTracker.h"
#include "core/PacketData.h"
#include "core/PacketEffectEngine.h"
#include "core/StartupError.h"
#include "core/Types.h"

#include <windivert.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <errhandlingapi.h>
#include <expected>
#include <handleapi.h>
#include <memory>
#include <minwindef.h>
#include <span>
#include <utility>
#include <vector>

namespace scrambler::core
{

PacketInterceptor::PacketInterceptor(FlowTracker& flow_tracker,
                                     const TargetPidSet& target_pids,
                                     const EffectConfig& effects)
    : flow_tracker_(flow_tracker), target_pids_(target_pids), effect_engine_(effects)
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

    handle_ = WinDivertOpen("udp and ip", WINDIVERT_LAYER_NETWORK, 0, 0);

    if (handle_ == INVALID_HANDLE_VALUE)
    {
        const auto gle = static_cast<std::uint32_t>(GetLastError());
        LogError("PacketInterceptor: WinDivertOpen failed (GLE={})", gle);
        CountEvent(Counter::kDriverErrors);
        return std::unexpected(MapWinDivertOpenError(gle));
    }

    constexpr std::uint64_t kQueueLength = 8192;
    constexpr auto kQueueSizeBytes = static_cast<std::uint64_t>(8 * 1024 * 1024);

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
    const bool was_running = running_.exchange(false);

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

void PacketInterceptor::CaptureLoop()
{
    constexpr unsigned int kBatchSize = 128;  // Up to 128 packets per kernel transition
    constexpr unsigned int kRecvBufferLen = kBatchSize * kStandardMtuSize;

    std::vector<uint8_t> recv_buf(kRecvBufferLen);
    std::vector<WINDIVERT_ADDRESS> recv_addrs(kBatchSize);

    std::vector<uint8_t> send_buf;
    send_buf.reserve(kRecvBufferLen);
    std::vector<WINDIVERT_ADDRESS> send_addrs;
    send_addrs.reserve(kBatchSize);

    uint32_t consecutive_failures = 0;

    while (running_.load())
    {
        unsigned int recv_len = 0;
        unsigned int addr_len = kBatchSize * sizeof(WINDIVERT_ADDRESS);

        if (WinDivertRecvEx(
                handle_, recv_buf.data(), kRecvBufferLen, &recv_len, 0, recv_addrs.data(), &addr_len, nullptr)
            == 0)
        {
            const auto gle = static_cast<std::uint32_t>(GetLastError());
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

        const unsigned int num_packets = addr_len / sizeof(WINDIVERT_ADDRESS);

        unsigned int outbound_count = 0;
        for (unsigned int i = 0; i < num_packets; ++i)
        {
            if (recv_addrs.at(static_cast<size_t>(i)).Outbound != 0)
            {
                ++outbound_count;
            }
        }
        CountEvent(Counter::kPacketsCapturedOutbound, outbound_count);
        CountEvent(Counter::kPacketsCapturedInbound, num_packets - outbound_count);

        uint8_t* current_pkt = recv_buf.data();
        unsigned int current_remaining = recv_len;

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

        for (unsigned int i = 0; i < num_packets; ++i)
        {
            const WINDIVERT_ADDRESS& addr = recv_addrs.at(static_cast<size_t>(i));

            WINDIVERT_IPHDR* ip = nullptr;
            WINDIVERT_UDPHDR* udp = nullptr;
            void* next_pkt = nullptr;
            unsigned int next_len = 0;

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
                static std::atomic<uint64_t> occurrences{0};
                LogRateLimited(occurrences,
                               LogLevel::kWarn,
                               "PacketInterceptor: WinDivertHelperParsePacket failed, reinjecting the rest of the "
                               "batch unchanged ({} of {})",
                               i,
                               num_packets);
                CountEvent(Counter::kParseFailures);

                const auto previous_size = send_buf.size();
                send_buf.resize(previous_size + current_remaining);
                std::memcpy(send_buf.data() + previous_size, current_pkt, current_remaining);

                for (unsigned int j = i; j < num_packets; ++j)
                {
                    send_addrs.push_back(recv_addrs.at(static_cast<size_t>(j)));
                }

                break;
            }

            const unsigned int pkt_len = current_remaining - next_len;
            bool handled = false;

            if (ip && udp)
            {
                auto tuple = TupleFromPacket(*ip, *udp);
                auto pid = flow_tracker_.LookupPid(tuple, addr.Outbound != 0);

                if (pid != 0 && target_pids_.Contains(pid))
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
                            DEBUG_PRINT("[DROP]  PID {:>5} | {}:{} -> {}:{} ({} bytes){}",
                                        pid,
                                        addrs.src.data(),
                                        tuple.src_port,
                                        addrs.dst.data(),
                                        tuple.dst_port,
                                        pkt_len,
                                        emission.HasEffect(PacketEffectKind::kBurstDrop) ? " [burst]" : "");
#endif
                            CountEvent(is_outbound ? Counter::kPacketsDroppedOutbound
                                                   : Counter::kPacketsDroppedInbound);
                        }

                        if (emission.ScheduledCount() > 0)
                        {
#ifndef NDEBUG
                            const auto release_delay = std::chrono::duration_cast<std::chrono::milliseconds>(
                                emission.scheduled_packets.front().release_at - now);
                            const auto scheduled_delay = release_delay > std::chrono::milliseconds::zero()
                                                             ? release_delay
                                                             : std::chrono::milliseconds::zero();
                            auto addrs = FormatAddresses(tuple.src_addr, tuple.dst_addr);
                            const char* schedule_label = emission.HasEffect(PacketEffectKind::kBandwidthThrottle)
                                                                 && !emission.HasEffect(PacketEffectKind::kDelay)
                                                                 && !emission.HasEffect(PacketEffectKind::kDelayJitter)
                                                             ? "THRTL"
                                                             : "DELAY";
                            DEBUG_PRINT("[{}] PID {:>5} | {}:{} -> {}:{} ({} bytes) +{}ms{}{}",
                                        schedule_label,
                                        pid,
                                        addrs.src.data(),
                                        tuple.src_port,
                                        addrs.dst.data(),
                                        tuple.dst_port,
                                        pkt_len,
                                        scheduled_delay.count(),
                                        emission.HasEffect(PacketEffectKind::kDelayJitter) ? " [jitter]" : "",
                                        emission.HasEffect(PacketEffectKind::kBandwidthThrottle) ? " [throttle]" : "");
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

            if (!handled)
            {
                append_immediate_packet(std::span(current_pkt, pkt_len), addr);
            }

            current_pkt = static_cast<uint8_t*>(next_pkt);
            current_remaining = next_len;

            if (current_remaining == 0)
            {
                break;
            }
        }

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
