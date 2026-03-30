#pragma once

#include "core/DelayQueue.h"
#include "core/EffectConfig.h"
#include "core/FlowTracker.h"

#include <atomic>
#include <memory>
#include <thread>

namespace scrambler::core
{

class PacketInterceptor
{
public:
    PacketInterceptor(FlowTracker& flow_tracker, const TargetSet& targets, const EffectConfig& effects);
    ~PacketInterceptor();

    PacketInterceptor(const PacketInterceptor&) = delete;
    PacketInterceptor& operator=(const PacketInterceptor&) = delete;
    PacketInterceptor(PacketInterceptor&&) = delete;
    PacketInterceptor& operator=(PacketInterceptor&&) = delete;

    bool Start();
    void Stop();
    bool IsRunning() const;

private:
    void CaptureLoop();
    void Reinject(const uint8_t* data, UINT len, const WINDIVERT_ADDRESS& addr);

    FlowTracker& flow_tracker_;
    const TargetSet& targets_;
    const EffectConfig& effects_;
    std::unique_ptr<DelayQueue> delay_queue_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

}  // namespace scrambler::core
