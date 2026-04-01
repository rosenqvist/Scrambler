#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace scrambler::platform
{

struct ProcessInfo
{
    uint32_t pid;
    uint32_t parent_pid;
    std::string name;
    std::wstring exe_path;
};

// Exposed for unit testing
bool StartsWithInsensitive(const std::wstring& str, const std::wstring& prefix);
bool IsSystemProcessPath(const std::wstring& path);

std::vector<ProcessInfo> EnumerateProcesses();

}  // namespace scrambler::platform
