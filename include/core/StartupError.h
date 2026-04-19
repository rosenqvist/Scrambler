#pragma once

#include <cstdint>

namespace scrambler::core
{

// Errors that can be returned from FlowTracker::Start or PacketInterceptor::Start.
//
// The UI maps these to user facing messages via ToUserMessage. The core layer
// never constructs QString or touches UI strings directly so it stays free of
// any Qt dependency.
enum class StartupError : uint8_t
{
    kDriverAccessDenied,     // ERROR_ACCESS_DENIED: not running as admin
    kDriverMissing,          // ERROR_FILE_NOT_FOUND or ERROR_SERVICE_DOES_NOT_EXIST: .sys/.dll missing
    kDriverOpenFailedOther,  // WinDivertOpen failed for a reason we don't specifically recognize
    kAlreadyRunning,         // Start() called on an already-running tracker or interceptor
    kInvalidState,           // Internal state machine violation (reserved for future use)
};

// Maps a GetLastError() (GLE) value from a failed WinDivertOpen call to a typed
// StartupError. Anything without a specific bucket becomes kDriverOpenFailedOther.
// The numeric GLE is still logged separately so telemetry doesn't lose info.
StartupError MapWinDivertOpenError(uint32_t gle) noexcept;

// Plain ASCII thats short enough to fit a status bar.
inline const char* ToUserMessage(StartupError error) noexcept
{
    switch (error)
    {
        case StartupError::kDriverAccessDenied:
            return "Access denied. Run Scrambler as administrator.";
        case StartupError::kDriverMissing:
            return "WinDivert driver files are missing. Reinstall Scrambler.";
        case StartupError::kDriverOpenFailedOther:
            return "Failed to open WinDivert driver. Check the diagnostics log.";
        case StartupError::kAlreadyRunning:
            return "Pipeline is already running.";
        case StartupError::kInvalidState:
            return "Internal error: invalid pipeline state.";
    }
    return "Unknown startup error.";
}

}  // namespace scrambler::core
