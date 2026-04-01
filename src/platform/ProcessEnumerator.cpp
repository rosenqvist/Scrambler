#include "platform/ProcessEnumerator.h"

#include <windows.h>

#include <array>
#include <tlhelp32.h>

namespace scrambler::platform
{

namespace
{

const std::wstring& GetWindowsDir()
{
    static std::wstring dir = []
    {
        std::array<wchar_t, MAX_PATH> buffer{};
        UINT len = GetWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
        return std::wstring(buffer.data(), len);
    }();
    return dir;
}

bool IsSessionZero(uint32_t pid)
{
    DWORD session_id = 0;
    if (ProcessIdToSessionId(pid, &session_id) == 0)
    {
        return false;
    }

    return session_id == 0;
}

}  // namespace

bool StartsWithInsensitive(const std::wstring& str, const std::wstring& prefix)
{
    if (str.size() < prefix.size())
    {
        return false;
    }

    for (size_t i = 0; i < prefix.size(); ++i)
    {
        if (towlower(str[i]) != towlower(prefix[i]))
        {
            return false;
        }
    }
    return true;
}

bool IsSystemProcessPath(const std::wstring& path)
{
    if (path.empty())
    {
        return true;
    }

    const auto& win = GetWindowsDir();

    return StartsWithInsensitive(path, win + L"\\System32") || StartsWithInsensitive(path, win + L"\\SysWOW64")
           || StartsWithInsensitive(path, win + L"\\SystemApps") || StartsWithInsensitive(path, win + L"\\WinSxS");
}

std::vector<ProcessInfo> EnumerateProcesses()
{
    std::vector<ProcessInfo> result;

    const uint32_t self_pid = GetCurrentProcessId();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry) == 0)
    {
        CloseHandle(snapshot);
        return result;
    }

    while (true)
    {
        auto pid = static_cast<uint32_t>(entry.th32ProcessID);
        auto parent_pid = static_cast<uint32_t>(entry.th32ParentProcessID);

        // Skip idle + system + self
        if (pid != 0 && pid != 4 && pid != self_pid)
        {
            const auto* exe = static_cast<const wchar_t*>(entry.szExeFile);

            int len = WideCharToMultiByte(CP_UTF8, 0, exe, -1, nullptr, 0, nullptr, nullptr);

            std::string name;

            if (len > 1)
            {
                name.resize(static_cast<size_t>(len - 1));
                WideCharToMultiByte(CP_UTF8, 0, exe, -1, name.data(), len, nullptr, nullptr);
            }

            std::wstring exe_path;

            HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (proc != nullptr)
            {
                std::array<wchar_t, MAX_PATH> path_buf{};
                auto path_size = static_cast<DWORD>(path_buf.size());

                if (QueryFullProcessImageNameW(proc, 0, path_buf.data(), &path_size) != 0)
                {
                    exe_path.assign(path_buf.data(), path_size);
                }

                CloseHandle(proc);
            }

            if (!(exe_path.empty() || IsSystemProcessPath(exe_path) || IsSessionZero(pid)))
            {
                result.push_back(ProcessInfo{
                    .pid = pid,
                    .parent_pid = parent_pid,
                    .name = std::move(name),
                    .exe_path = std::move(exe_path),
                });
            }
        }

        if (Process32NextW(snapshot, &entry) == 0)
        {
            break;
        }
    }

    CloseHandle(snapshot);
    return result;
}

}  // namespace scrambler::platform
