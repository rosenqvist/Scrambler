#include "core/FlowTracker.h"

#include "core/Diagnostics.h"
#include "core/Types.h"

#include <iphlpapi.h>
#include <vector>

namespace scrambler::core
{

namespace
{

bool IsNoisePid(uint32_t pid)
{
    return pid == 0 || pid == kSystemPid;
}

uint64_t MakeEndpointKey(uint32_t local_addr, uint16_t local_port)
{
    return (static_cast<uint64_t>(local_addr) << 16) | local_port;
}

uint64_t LocalEndpointKey(const FiveTuple& tuple, bool is_outbound)
{
    return is_outbound ? MakeEndpointKey(tuple.src_addr, tuple.src_port)
                       : MakeEndpointKey(tuple.dst_addr, tuple.dst_port);
}

// Walks the current Windows UDP table and calls
// `visitor(local_addr, local_port, pid)`
// for each entry. Centralizes the GetExtendedUdpTable retry and error
// boilerplate so both the miss-path lookup and the startup bootstrap share it.
//
// The retry loop follows Microsoft's IP Helper guidance. Between the size
// query and the data fetch another process may open a UDP socket and grow
// the table, so we re-ask with a bigger buffer until the call succeeds.
template <typename Visitor>
bool ForEachUdpEntry(Visitor&& visitor)
{
    ULONG size = 0;
    DWORD ret = GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);

    std::vector<uint8_t> buffer;
    while (ret == ERROR_INSUFFICIENT_BUFFER)
    {
        buffer.resize(size);
        ret = GetExtendedUdpTable(buffer.data(), &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    }

    if (ret != NO_ERROR)
    {
        LogWarn("GetExtendedUdpTable failed (code={})", ret);
        CountEvent(Counter::kDriverErrors);
        return false;
    }

    auto* table = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(buffer.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i)
    {
        auto& entry = table->table[i];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)

        std::forward<Visitor>(
            visitor)(ntohl(entry.dwLocalAddr), ntohs(static_cast<uint16_t>(entry.dwLocalPort)), entry.dwOwningPid);
    }
    return true;
}

// Fallback PID lookup via the Windows UDP table.
// Used when the FLOW layer hasn't seen this connection yet and the endpoint index
// has no entry for it either. Happens for a flow that established after we
// started but whose FLOW_ESTABLISHED event we haven't drained yet.
uint32_t LookupPidFromSystem(uint32_t local_addr, uint16_t local_port)
{
    uint32_t result = 0;
    ForEachUdpEntry([&](uint32_t addr, uint16_t port, uint32_t pid)
    {
        if (port == local_port && (addr == local_addr || addr == 0) && !IsNoisePid(pid))
        {
            result = pid;
        }
    });
    return result;
}

}  // namespace

FlowTracker::~FlowTracker()
{
    Stop();
}

std::expected<void, StartupError> FlowTracker::Start()
{
    if (running_.load())
    {
        return std::unexpected(StartupError::kAlreadyRunning);
    }

    // SNIFF + RECV_ONLY to observe flow without intercepting any traffic.
    // This gives us the PID = connection mappings without touching any packets.
    handle_ = WinDivertOpen("udp", WINDIVERT_LAYER_FLOW, 0, WINDIVERT_FLAG_SNIFF | WINDIVERT_FLAG_RECV_ONLY);

    if (handle_ == INVALID_HANDLE_VALUE)
    {
        const DWORD gle = GetLastError();
        LogError("FlowTracker: WinDivertOpen failed (GLE={})", gle);
        CountEvent(Counter::kDriverErrors);
        return std::unexpected(MapWinDivertOpenError(gle));
    }

    // Seed the port index before the tracking thread starts. Any flow already
    // open at this point has no FLOW_ESTABLISHED event coming our way, so
    // without this bootstrap its first packet would cost a kernel scan.
    BootstrapFromSystem();

    running_.store(true);
    thread_ = std::jthread([this]
    {
        TrackingLoop();
    });
    LogInfo("FlowTracker running");
    return {};
}

void FlowTracker::BootstrapFromSystem()
{
    std::unordered_map<uint64_t, uint32_t> snapshot;
    ForEachUdpEntry([&](uint32_t local_addr, uint16_t port, uint32_t pid)
    {
        if (!IsNoisePid(pid))
        {
            snapshot[MakeEndpointKey(local_addr, port)] = pid;
        }
    });

    const size_t count = snapshot.size();
    {
        std::unique_lock lock(mutex_);
        endpoint_to_pid_ = std::move(snapshot);
    }
    LogInfo("FlowTracker: bootstrapped {} UDP endpoints from system table", count);
}

void FlowTracker::SetFatalCallback(FatalCallback cb)
{
    fatal_cb_ = std::move(cb);
}

void FlowTracker::NotifyFatal(uint32_t gle)
{
    if (fatal_cb_)
    {
        fatal_cb_(gle);
    }
}

void FlowTracker::Stop()
{
    const bool was_running = running_.exchange(false);

    if (handle_ != INVALID_HANDLE_VALUE)
    {
        WinDivertShutdown(handle_, WINDIVERT_SHUTDOWN_BOTH);
    }

    if (thread_.joinable())
    {
        thread_.join();
    }

    if (handle_ != INVALID_HANDLE_VALUE)
    {
        WinDivertClose(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }

    if (was_running)
    {
        LogInfo("FlowTracker stopped");
    }
}

bool FlowTracker::IsRunning() const
{
    return running_.load();
}

uint32_t FlowTracker::LookupPid(const FiveTuple& tuple, bool is_outbound)
{
    {
        std::shared_lock lock(mutex_);

        // Primary: full 5-tuple map, populated by FLOW_ESTABLISHED events.
        if (auto it = flow_table_.find(tuple); it != flow_table_.end())
        {
            CountEvent(Counter::kFlowCacheHits);
            return it->second;
        }

        // Secondary: local-endpoint index, seeded from GetExtendedUdpTable at
        // Start() and refreshed on every FLOW_ESTABLISHED. Covers flows that
        // existed before our handle opened without guessing on the remote port.
        if (auto it = endpoint_to_pid_.find(LocalEndpointKey(tuple, is_outbound)); it != endpoint_to_pid_.end())
        {
            CountEvent(Counter::kFlowCacheHits);
            return it->second;
        }

        // INADDR_ANY listeners show up in the UDP table with local_addr = 0.
        const uint16_t local_port = is_outbound ? tuple.src_port : tuple.dst_port;
        if (auto it = endpoint_to_pid_.find(MakeEndpointKey(0, local_port)); it != endpoint_to_pid_.end())
        {
            CountEvent(Counter::kFlowCacheHits);
            return it->second;
        }
    }

    // Neither cache had it. This costs a full UDP-table kernel scan and should
    // be rare once warm. Only the FLOW vs NETWORK race window triggers it.
    CountEvent(Counter::kFlowCacheKernelScans);
    const uint32_t local_addr = is_outbound ? tuple.src_addr : tuple.dst_addr;
    const uint16_t local_port = is_outbound ? tuple.src_port : tuple.dst_port;
    auto pid = LookupPidFromSystem(local_addr, local_port);
    if (pid != 0)
    {
        InsertFlow(tuple, pid);
    }

    return pid;
}

// Inserts or overwrites both directions of a flow mapping.
void FlowTracker::InsertFlow(const FiveTuple& tuple, uint32_t pid)
{
    // Store both directions so packet lookups match regardless of direction
    std::unique_lock lock(mutex_);
    flow_table_[tuple] = pid;
    flow_table_[tuple.Reversed()] = pid;
}

void FlowTracker::OnFlowEstablished(const FiveTuple& tuple, uint32_t pid)
{
    if (IsNoisePid(pid))
    {
        return;
    }

    {
        std::unique_lock lock(mutex_);
        flow_table_[tuple] = pid;
        flow_table_[tuple.Reversed()] = pid;
        // TupleFromFlow puts the local address in src_*, so src_port is the
        // local endpoint here, which is exactly what the fallback index is keyed on.
        endpoint_to_pid_[MakeEndpointKey(tuple.src_addr, tuple.src_port)] = pid;
    }
#ifndef NDEBUG
    if (!IsNoisePid(pid))
    {
        auto addrs = FormatAddresses(tuple.src_addr, tuple.dst_addr);
        DEBUG_PRINT("[FLOW+] PID {:>5} | {}:{} -> {}:{}",
                    pid,
                    addrs.src.data(),
                    tuple.src_port,
                    addrs.dst.data(),
                    tuple.dst_port);
    }
#endif
}

void FlowTracker::OnFlowDeleted(const FiveTuple& tuple)
{
    uint32_t pid = 0;
    {
        std::shared_lock lock(mutex_);
        auto it = flow_table_.find(tuple);
        if (it != flow_table_.end())
        {
            pid = it->second;
        }
    }

    {
        std::unique_lock lock(mutex_);
        flow_table_.erase(tuple);
        flow_table_.erase(tuple.Reversed());
        endpoint_to_pid_.erase(MakeEndpointKey(tuple.src_addr, tuple.src_port));
        endpoint_to_pid_.erase(MakeEndpointKey(0, tuple.src_port));
    }
    if (!IsNoisePid(pid))
    {
#ifndef NDEBUG
        auto addrs = FormatAddresses(tuple.src_addr, tuple.dst_addr);
        DEBUG_PRINT("[FLOW-] PID {:>5} | {}:{} -> {}:{}",
                    pid,
                    addrs.src.data(),
                    tuple.src_port,
                    addrs.dst.data(),
                    tuple.dst_port);
#endif
    }
}

void FlowTracker::TrackingLoop()
{
    WINDIVERT_ADDRESS addr{};
    uint32_t consecutive_failures = 0;

    while (running_.load())
    {
        if (WinDivertRecv(handle_, nullptr, 0, nullptr, &addr) == 0)
        {
            const DWORD gle = GetLastError();
            switch (ClassifyRecvFailure(gle, consecutive_failures, "FlowTracker"))
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

        auto tuple = TupleFromFlow(addr);

        if (addr.Event == WINDIVERT_EVENT_FLOW_ESTABLISHED)
        {
            OnFlowEstablished(tuple, addr.Flow.ProcessId);
        }
        else if (addr.Event == WINDIVERT_EVENT_FLOW_DELETED)
        {
            OnFlowDeleted(tuple);
        }
    }
}

}  // namespace scrambler::core
