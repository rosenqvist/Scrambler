#include "core/EffectConfig.h"
#include "core/FlowTracker.h"
#include "core/PacketInterceptor.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <print>
#include <thread>

namespace
{

std::atomic<bool> should_exit{false};

void SignalHandler(int /*signal*/)
{
    should_exit.store(true);
}

}  // namespace

// Making it CLI-based to make sure core logic is working properly before adding qt
int main()
{
    std::signal(SIGINT, SignalHandler);

    scrambler::core::TargetSet targets;
    scrambler::core::EffectConfig effects;

    scrambler::core::FlowTracker flow_tracker;
    if (!flow_tracker.Start())
    {
        return 1;
    }

    scrambler::core::PacketInterceptor interceptor(flow_tracker, targets, effects);
    if (!interceptor.Start())
    {
        return 1;
    }

    std::println("\nScrambler running. Flows will appear above.");
    std::println("Enter: <pid> <delay_ms> <drop_rate>\n");

    uint32_t target_pid = 0;
    int delay_ms = 0;
    float drop_rate = 0.0F;

    std::print("> ");
    std::cin >> target_pid >> delay_ms >> drop_rate;

    if (std::cin.fail())
    {
        std::println("Invalid input.");
        interceptor.Stop();
        flow_tracker.Stop();
        return 1;
    }

    targets.Add(target_pid);
    effects.delay_ms.store(delay_ms);
    effects.drop_rate.store(drop_rate);

    std::println("\nTargeting PID {} | {}ms delay | {:.0f}% drop\n", target_pid, delay_ms, drop_rate * 100.0F);

    while (!should_exit.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::println("\nShutting down...");
    interceptor.Stop();
    flow_tracker.Stop();
    return 0;
}
