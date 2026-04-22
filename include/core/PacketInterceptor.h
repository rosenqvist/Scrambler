#pragma once

#include "core/DelayQueue.h"
#include "core/EffectConfig.h"
#include "core/FlowTracker.h"
#include "core/PacketEffectEngine.h"
#include "core/StartupError.h"

#include <atomic>
#include <expected>
#include <functional>
#include <memory>
#include <thread>

namespace scrambler::core
{

class PacketInterceptor
{
public:
    // See FlowTracker::FatalCallback. Fires from the capture
    // thread when the loop exits due to a terminal error or repeated failures.
    using FatalCallback = std::function<void(uint32_t)>;

    PacketInterceptor(FlowTracker& flow_tracker, const TargetSet& targets, const EffectConfig& effects);
    ~PacketInterceptor();

    PacketInterceptor(const PacketInterceptor&) = delete;
    PacketInterceptor& operator=(const PacketInterceptor&) = delete;
    PacketInterceptor(PacketInterceptor&&) = delete;
    PacketInterceptor& operator=(PacketInterceptor&&) = delete;

    std::expected<void, StartupError> Start();
    void Stop();
    bool IsRunning() const;

    // Must be set before Start(). Not thread-safe against a running interceptor.
    void SetFatalCallback(FatalCallback cb);

private:
    void CaptureLoop();
    void NotifyFatal(uint32_t gle);

    FlowTracker& flow_tracker_;
    const TargetSet& targets_;
    PacketEffectEngine effect_engine_;
    std::unique_ptr<DelayQueue> delay_queue_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
    HANDLE handle_{INVALID_HANDLE_VALUE};
    FatalCallback fatal_cb_;
};

}  // namespace scrambler::core
