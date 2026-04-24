#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace scrambler::platform
{

struct ProcessIdentity
{
    uint32_t pid = 0;
    uint64_t creation_time = 0;
    std::wstring exe_path;

    [[nodiscard]] bool IsValid() const
    {
        return pid != 0 && creation_time != 0 && !exe_path.empty();
    }
};

struct ProcessInfo
{
    uint32_t pid;
    uint32_t parent_pid;
    std::string name;
    std::wstring exe_path;
    uint64_t creation_time;
};

enum class ProcessIdentityError : uint8_t
{
    kOpenFailed,
    kImagePathUnavailable,
    kCreationTimeUnavailable,
};

// Exposed for unit testing
bool StartsWithInsensitive(const std::wstring& str, const std::wstring& prefix);
bool IsSystemProcessPath(const std::wstring& path);

std::expected<ProcessIdentity, ProcessIdentityError> GetProcessIdentity(uint32_t pid);
bool IsProcessIdentityCurrent(const ProcessIdentity& identity);

std::vector<ProcessInfo> EnumerateProcesses();

}  // namespace scrambler::platform
