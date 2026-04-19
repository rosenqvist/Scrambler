#pragma once

#include "core/StartupError.h"
#include "core/Types.h"

#include <atomic>
#include <expected>
#include <functional>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

// WinDivert has two layers we care about: FLOW and NETWORK. The NETWORK layer
// captures raw packets but has no idea which process sent them. The FLOW layer
// is the opposite. It fires events whenever a process opens or closes a
// network connection and tells you the PID and the 5 tuple (source/dest IP,
// source/dest port, protocol) but it never touches actual packet data.
//
// FlowTracker sits on the FLOW layer and builds a table mapping the 5 tuples to
// PIDs. When PacketInterceptor grabs a packet off the NETWORK layer it pulls
// out the 5 tuple structure from the headers and asks FlowTracker "who owns this?"
// so we can decide whether to mess with it or let it through.
// When Scrambler starts after the application we want to capture packets from,
// we fall back to calling GetExtendedUdpTable (expensive) which returns a table
// listing all UDP endpoints.
namespace scrambler::core
{

class FlowTracker
{
public:
    // Fires from the tracking thread when the capture loop exits due to a
    // terminal error. The argument is the GetLastError() value at the point
    // of failure. Callers have to marshal back to their UI thread before
    // touching widgets.
    using FatalCallback = std::function<void(uint32_t)>;

    FlowTracker() = default;
    ~FlowTracker();

    FlowTracker(const FlowTracker&) = delete;
    FlowTracker& operator=(const FlowTracker&) = delete;
    FlowTracker(FlowTracker&&) = delete;
    FlowTracker& operator=(FlowTracker&&) = delete;

    std::expected<void, StartupError> Start();
    void Stop();
    bool IsRunning() const;
    uint32_t LookupPid(const FiveTuple& tuple);

    // Must be set before Start(). Not thread-safe against a running tracker.
    void SetFatalCallback(FatalCallback cb);

private:
    using FlowTable = std::unordered_map<FiveTuple, uint32_t, FiveTupleHash>;

    void TrackingLoop();
    void OnFlowEstablished(const FiveTuple& tuple, uint32_t pid);
    void OnFlowDeleted(const FiveTuple& tuple);
    void InsertFlow(const FiveTuple& tuple, uint32_t pid);
    void EraseFlow(const FiveTuple& tuple);
    void NotifyFatal(uint32_t gle);
    // Snapshot the current UDP table and populate port_to_pid_. Called once
    // from Start() so flows that were already open when we attached show up
    // as cache hits instead of first-packet kernel scans.
    void BootstrapFromSystem();

    FlowTable flow_table_;
    // Secondary index keyed by local port only, populated from GetExtendedUdpTable
    // at Start() and kept warm by OnFlowEstablished. Used as a fallback when
    // flow_table_ misses. Covers pre-existing flows (no FLOW_ESTABLISHED event
    // ever fired for them) without paying for a kernel scan.
    std::unordered_map<uint16_t, uint32_t> port_to_pid_;
    mutable std::shared_mutex mutex_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
    HANDLE handle_{INVALID_HANDLE_VALUE};
    FatalCallback fatal_cb_;
};

}  // namespace scrambler::core
