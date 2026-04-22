#pragma once

#include "core/StartupError.h"
#include "core/Types.h"

#include <atomic>
#include <expected>
#include <functional>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace scrambler::core
{

class FlowTracker
{
public:
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
    uint32_t LookupPid(const FiveTuple& tuple, bool is_outbound);

    // Must be set before Start(). Not thread-safe against a running tracker.
    void SetFatalCallback(FatalCallback cb);

private:
    using FlowTable = std::unordered_map<FiveTuple, uint32_t, FiveTupleHash>;

    void TrackingLoop();
    void OnFlowEstablished(const FiveTuple& tuple, uint32_t pid);
    void OnFlowDeleted(const FiveTuple& tuple);
    void InsertFlow(const FiveTuple& tuple, uint32_t pid);
    void NotifyFatal(uint32_t gle);
    void BootstrapFromSystem();

    FlowTable flow_table_;
    std::unordered_map<uint64_t, uint32_t> endpoint_to_pid_;
    mutable std::shared_mutex mutex_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
    HANDLE handle_{INVALID_HANDLE_VALUE};
    FatalCallback fatal_cb_;
};

}  // namespace scrambler::core
