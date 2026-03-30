#include "core/FlowTracker.h"

#include "core/Types.h"

#include <iphlpapi.h>  // for access to GetExtendedUdpTable function
#include <print>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")

namespace scrambler::core
{

namespace
{

bool IsNoisePid(uint32_t pid)
{
    return pid == 0 || pid == kSystemPid;
}

// Fallback PID lookup via the Windows UDP table.
// Used when the FLOW layer hasn't seen this connection yet,
// which happens for flows that were already open before Scrambler was started.
uint32_t LookupPidFromSystem(uint16_t local_port)
{
    ULONG size = 0;
    GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);

    std::vector<uint8_t> buffer(size);
    auto* table = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(buffer.data());

    if (GetExtendedUdpTable(table, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) != NO_ERROR)
    {
        return 0;
    }

    for (DWORD i = 0; i < table->dwNumEntries; ++i)
    {
        auto& entry = table->table[i];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        auto pid = entry.dwOwningPid;
        if (ntohs(static_cast<uint16_t>(entry.dwLocalPort)) == local_port && !IsNoisePid(pid))
        {
            return pid;
        }
    }

    return 0;
}

}  // namespace

FlowTracker::~FlowTracker()
{
    Stop();
}

bool FlowTracker::Start()
{
    // SNIFF + RECV_ONLY to observe flow without intercepting any traffic.
    // This gives us the PID = connection mappings without touching any packets.
    handle_ = WinDivertOpen("udp", WINDIVERT_LAYER_FLOW, 0, WINDIVERT_FLAG_SNIFF | WINDIVERT_FLAG_RECV_ONLY);

    if (handle_ == INVALID_HANDLE_VALUE)
    {
        std::println("[FLOW] WinDivertOpen failed: {}", GetLastError());
        return false;
    }

    running_.store(true);
    thread_ = std::jthread([this]
    {
        TrackingLoop();
    });
    std::println("[FLOW] Tracking UDP flows...");
    return true;
}

void FlowTracker::Stop()
{
    running_.store(false);

    if (handle_ != INVALID_HANDLE_VALUE)
    {
        WinDivertClose(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

bool FlowTracker::IsRunning() const
{
    return running_.load();
}

uint32_t FlowTracker::LookupPid(const FiveTuple& tuple)
{
    {
        std::shared_lock lock(mutex_);
        auto it = flow_table_.find(tuple);
        if (it != flow_table_.end())
        {
            return it->second;
        }
    }
    // This is a Windows kernel call to query the Windows UDP table and will always be expensive.
    // Try src_port first (outbound), then dst_port (inbound).
    auto pid = LookupPidFromSystem(tuple.src_port);
    if (pid == 0)
    {
        pid = LookupPidFromSystem(tuple.dst_port);
    }

    if (pid != 0)
    {
        InsertFlow(tuple, pid);
    }

    return pid;
}

void FlowTracker::InsertFlow(const FiveTuple& tuple, uint32_t pid)
{
    // Store both directions so packet lookups match regardless of direction
    std::unique_lock lock(mutex_);
    flow_table_[tuple] = pid;
    flow_table_[tuple.Reversed()] = pid;
}

void FlowTracker::EraseFlow(const FiveTuple& tuple)
{
    std::unique_lock lock(mutex_);
    flow_table_.erase(tuple);
    flow_table_.erase(tuple.Reversed());
}

void FlowTracker::OnFlowEstablished(const FiveTuple& tuple, uint32_t pid)
{
    if (IsNoisePid(pid))
    {
        return;
    }

    InsertFlow(tuple, pid);

    auto addrs = FormatAddresses(tuple.src_addr, tuple.dst_addr);
    std::println(
        "[FLOW+] PID {:>5} | {}:{} -> {}:{}", pid, addrs.src.data(), tuple.src_port, addrs.dst.data(), tuple.dst_port);
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

    EraseFlow(tuple);

    if (!IsNoisePid(pid))
    {
        auto addrs = FormatAddresses(tuple.src_addr, tuple.dst_addr);
        std::println("[FLOW-] PID {:>5} | {}:{} -> {}:{}",
                     pid,
                     addrs.src.data(),
                     tuple.src_port,
                     addrs.dst.data(),
                     tuple.dst_port);
    }
}

void FlowTracker::TrackingLoop()
{
    WINDIVERT_ADDRESS addr{};

    while (running_.load())
    {
        if (WinDivertRecv(handle_, nullptr, 0, nullptr, &addr) == 0)
        {
            continue;
        }

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
