#include "core/Diagnostics.h"

#include "core/Types.h"

#include <algorithm>

namespace scrambler::core
{

Diagnostics& Diagnostics::Instance()
{
    static Diagnostics d;
    return d;
}

void Diagnostics::Log(LogLevel level, std::string message)
{
    LogEntry entry{.seq = next_seq_.fetch_add(1, std::memory_order_relaxed),
                   .when = std::chrono::system_clock::now(),
                   .level = level,
                   .message = std::move(message)};

    Sink sink_copy;
    {
        std::lock_guard lock(mutex_);
        if (ring_.size() >= kRingCapacity)
        {
            ring_.pop_front();
        }
        ring_.push_back(entry);
        sink_copy = sink_;
    }

    if (sink_copy)
    {
        sink_copy(entry);
    }
}

void Diagnostics::ResetCounters() noexcept
{
    for (auto& c : counters_)
    {
        c.store(0, std::memory_order_relaxed);
    }
}

std::vector<LogEntry> Diagnostics::Snapshot(size_t max_entries) const
{
    std::lock_guard lock(mutex_);
    // Explicit template argument dodges the Windows `min` macro (windows.h defines `min(a,b)`).
    const size_t n = std::min<size_t>(max_entries, ring_.size());
    std::vector<LogEntry> out;
    out.reserve(n);
    auto start = ring_.end() - static_cast<ptrdiff_t>(n);
    for (auto it = start; it != ring_.end(); ++it)
    {
        out.push_back(*it);
    }
    return out;
}

std::vector<LogEntry> Diagnostics::SnapshotSince(uint64_t since_seq, size_t max_entries) const
{
    std::lock_guard lock(mutex_);
    std::vector<LogEntry> out;

    // The ring stores entries in insertion order (oldest at front, newest at back).
    // We walk from newest backwards to bound the scan when most entries are old.
    // Reverse at the end to hand back chronological order for the caller.
    for (auto it = ring_.rbegin(); it != ring_.rend(); ++it)
    {
        if (it->seq <= since_seq)
        {
            break;
        }
        out.push_back(*it);
        if (out.size() >= max_entries)
        {
            break;
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

void Diagnostics::SetSink(Sink sink)
{
    std::lock_guard lock(mutex_);
    sink_ = std::move(sink);
}

RecvFailureAction ClassifyRecvFailure(uint32_t gle, uint32_t& consecutive_failures, std::string_view component)
{
    // WinDivertShutdown drains the queue and then causes Recv to return 0 with
    // ERROR_NO_DATA. That's the normal clean-shutdown signal.
    if (gle == ERROR_NO_DATA)
    {
        return RecvFailureAction::kExitClean;
    }

    if (gle == ERROR_INVALID_HANDLE || gle == ERROR_OPERATION_ABORTED)
    {
        Diagnostics::Instance().Logf(LogLevel::kError,
                                     "{}: terminal recv error (GLE={}), exiting capture loop",
                                     component,
                                     gle);
        CountEvent(Counter::kDriverErrors);
        return RecvFailureAction::kExitFatal;
    }

    CountEvent(Counter::kDriverErrors);
    if (++consecutive_failures > kMaxConsecutiveRecvFailures)
    {
        Diagnostics::Instance().Logf(LogLevel::kFatal,
                                     "{}: {} consecutive recv failures (last GLE={}), exiting capture loop",
                                     component,
                                     consecutive_failures,
                                     gle);
        return RecvFailureAction::kExitFatal;
    }
    return RecvFailureAction::kContinue;
}

}  // namespace scrambler::core
