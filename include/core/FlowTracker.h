#pragma once

#include "core/Types.h"

#include <atomic>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

// WinDivert has two layers we care about: FLOW and NETWORK. The NETWORK layer
// captures raw packets but has no idea which process sent them. The FLOW layer
// is the opposite, it fires events whenever a process opens or closes a
// network connection and tells you the PID and the 5 tuple (source/dest IP,
// source/dest port, protocol) but it never touches actual packet data.
//
// FlowTracker sits on the FLOW layer and builds a table mapping the 5 tuples to
// PIDs. When PacketInterceptor grabs a packet off the NETWORK layer it pulls
// out the 5 tuple structure from the headers and asks FlowTracker "who owns this?" so
// we can decide whether to mess with it or let it through.
// in those cases where Scrambler is started after the application we want to capture packets
// from, we fallback to calling GetExtendedUdpTable function (expensive), which returns
// a table that contains a list of UDP endpoints.
namespace scrambler::core
{

class FlowTracker
{
public:
    FlowTracker() = default;
    ~FlowTracker();

    FlowTracker(const FlowTracker&) = delete;
    FlowTracker& operator=(const FlowTracker&) = delete;
    FlowTracker(FlowTracker&&) = delete;
    FlowTracker& operator=(FlowTracker&&) = delete;

    bool Start();
    void Stop();
    bool IsRunning() const;
    uint32_t LookupPid(const FiveTuple& tuple);

private:
    using FlowTable = std::unordered_map<FiveTuple, uint32_t, FiveTupleHash>;

    void TrackingLoop();
    void OnFlowEstablished(const FiveTuple& tuple, uint32_t pid);
    void OnFlowDeleted(const FiveTuple& tuple);
    void InsertFlow(const FiveTuple& tuple, uint32_t pid);
    void EraseFlow(const FiveTuple& tuple);

    FlowTable flow_table_;
    mutable std::shared_mutex mutex_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

}  // namespace scrambler::core
