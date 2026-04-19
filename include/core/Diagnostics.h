#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace scrambler::core
{

enum class LogLevel : uint8_t
{
    kInfo,
    kWarn,
    kError,
    kFatal,
};

enum class Counter : uint8_t
{
    kPacketsCapturedOutbound,
    kPacketsCapturedInbound,
    kPacketsReinjected,
    kPacketsDroppedOutbound,
    kPacketsDroppedInbound,
    kPacketsDelayedOutbound,
    kPacketsDelayedInbound,
    kPoolExhausted,
    kReinjectFailures,
    kParseFailures,
    // Flow lookup resolved from the in-memory maps (tuple map or port index).
    kFlowCacheHits,
    // Flow lookup had to call GetExtendedUdpTable. Should sit at zero once warm.
    // A few right after startup is fine
    kFlowCacheKernelScans,
    kDriverErrors,
    kCount,
};

struct LogEntry
{
    uint64_t seq;
    std::chrono::system_clock::time_point when;
    LogLevel level;
    std::string message;
};

class Diagnostics
{
public:
    static Diagnostics& Instance();

    ~Diagnostics() = default;
    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;
    Diagnostics(Diagnostics&&) = delete;
    Diagnostics& operator=(Diagnostics&&) = delete;

    void Log(LogLevel level, std::string message);

    template <typename... Args>
    void Logf(LogLevel level, std::format_string<Args...> fmt, Args&&... args)
    {
        Log(level, std::format(fmt, std::forward<Args>(args)...));
    }

    void Increment(Counter c, uint64_t n = 1) noexcept
    {
        Slot(c).fetch_add(n, std::memory_order_relaxed);
    }

    uint64_t Get(Counter c) const noexcept
    {
        return Slot(c).load(std::memory_order_relaxed);
    }

    void ResetCounters() noexcept;

    std::vector<LogEntry> Snapshot(size_t max_entries = 256) const;

    // Returns entries whose seq is strictly greater than `since_seq`,
    // up to `max_entries` most recent. The UI polls this to stream new logs
    // without re-displaying what it has already shown.
    std::vector<LogEntry> SnapshotSince(uint64_t since_seq, size_t max_entries = 1024) const;

    // Optional sink for the UI, invoked on every log entry (from any thread).
    using Sink = std::function<void(const LogEntry&)>;
    void SetSink(Sink sink);

private:
    Diagnostics() = default;

    std::atomic<uint64_t>& Slot(Counter c) noexcept
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index): index bounded by enum
        return counters_[static_cast<size_t>(c)];
    }

    const std::atomic<uint64_t>& Slot(Counter c) const noexcept
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index): index bounded by enum
        return counters_[static_cast<size_t>(c)];
    }

    static constexpr size_t kRingCapacity = 1024;

    std::array<std::atomic<uint64_t>, static_cast<size_t>(Counter::kCount)> counters_{};
    std::atomic<uint64_t> next_seq_{1};
    mutable std::mutex mutex_;
    std::deque<LogEntry> ring_;
    Sink sink_;
};

template <typename... Args>
inline void LogInfo(std::format_string<Args...> fmt, Args&&... args)
{
    Diagnostics::Instance().Logf(LogLevel::kInfo, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void LogWarn(std::format_string<Args...> fmt, Args&&... args)
{
    Diagnostics::Instance().Logf(LogLevel::kWarn, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void LogError(std::format_string<Args...> fmt, Args&&... args)
{
    Diagnostics::Instance().Logf(LogLevel::kError, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void LogFatal(std::format_string<Args...> fmt, Args&&... args)
{
    Diagnostics::Instance().Logf(LogLevel::kFatal, fmt, std::forward<Args>(args)...);
}

inline void CountEvent(Counter c, uint64_t n = 1) noexcept
{
    Diagnostics::Instance().Increment(c, n);
}

// --- Rate-limited logging ---
//
// Counters tell us something went wrong N times. They don't tell us what.
// For alert-worthy sites like pool exhaustion or reinject and parse failures
// we want the first few occurrences logged with full context so the root
// cause is visible. After that a periodic pulse keeps ongoing failure visible
// without flooding the ring buffer.
//
// Use with a function-local `static std::atomic<uint64_t> occurrences{0};`
// so each callsite has its own budget.

constexpr uint64_t kLogRateLimitFirst = 5;      // First N always log (root cause)
constexpr uint64_t kLogRateLimitEveryN = 1000;  // After that, every Nth (pulse)

template <typename... Args>
inline void LogRateLimited(std::atomic<uint64_t>& site_counter,
                           LogLevel level,
                           std::format_string<Args...> fmt,
                           Args&&... args)
{
    const uint64_t n = site_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= kLogRateLimitFirst || (n % kLogRateLimitEveryN) == 0)
    {
        Diagnostics::Instance().Logf(level, fmt, std::forward<Args>(args)...);
    }
}

// Decision returned by ClassifyRecvFailure.
enum class RecvFailureAction : uint8_t
{
    kContinue,   // Transient, keep polling
    kExitClean,  // ERROR_NO_DATA from WinDivertShutdown, exit silently
    kExitFatal,  // Terminal error or hit the failure cap, caller should NotifyFatal and exit
};

// Classifies a WinDivertRecv*() failure into one of three loop actions.
// Bumps kDriverErrors and logs as needed. The caller just has to honor the
// returned action and call its own fatal callback when the result is kExitFatal.
//
// `component` is logged verbatim ("FlowTracker", "PacketInterceptor") so
// failures are attributable. `consecutive_failures` is incremented on
// transient errors and the caller resets it on a successful recv.
RecvFailureAction ClassifyRecvFailure(uint32_t gle, uint32_t& consecutive_failures, std::string_view component);

}  // namespace scrambler::core
