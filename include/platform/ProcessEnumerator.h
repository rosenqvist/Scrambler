#pragma once

#include <cstdint>
#include <string>
#include <vector>

// windows.h must come before tlhelp32.h or you get missing type errors
#include <windows.h>

#include <tlhelp32.h>

namespace scrambler::platform
{

struct ProcessInfo
{
    uint32_t pid;
    uint32_t parent_pid;
    std::string name;
};

// Grabs a list of every running process on the system.
// Uses the Win32 Toolhelp32 API which takes a frozen snapshot of the
// process table at the time of the call. We refresh this periodically
// from the UI so the user sees an up-to-date list.
//
// PID 0 (idle) and PID 4 (system) are skipped because they show up
// in every snapshot and are never something you want target anyways.
inline std::vector<ProcessInfo> EnumerateProcesses()
{
    std::vector<ProcessInfo> result;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return result;
    }

    // dwSize must be set before calling Process32First or the call
    // silently fails and returns nothing.
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);

    // Toolhelp uses a first/next pattern instead of a normal iterator.
    // Process32First loads the first entry, then Process32Next advances.
    if (Process32First(snapshot, &entry))
    {
        while (true)
        {
            auto pid = static_cast<uint32_t>(entry.th32ProcessID);
            auto parent_pid = static_cast<uint32_t>(entry.th32ParentProcessID);
            if (pid != 0 && pid != 4)
            {
                // PROCESSENTRY32 gives us szExeFile as wchar_t because
                // Windows builds with Unicode enabled by default. We need
                // std::string for the rest of our code so we convert here.
                //
                // First call to WideCharToMultiByte with a null output
                // buffer returns the required length. Second call does the
                // actual conversion. We use &szExeFile[0] instead of just
                // szExeFile, to avoid an array decay.
                int len = WideCharToMultiByte(CP_UTF8, 0, &entry.szExeFile[0], -1, nullptr, 0, nullptr, nullptr);
                std::string name(static_cast<std::size_t>(len - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, &entry.szExeFile[0], -1, name.data(), len, nullptr, nullptr);
                result.push_back({.pid = pid, .parent_pid = parent_pid, .name = std::move(name)});
            }

            if (!Process32Next(snapshot, &entry))
            {
                break;
            }
        }
    }

    CloseHandle(snapshot);
    return result;
}

}  // namespace scrambler::platform
