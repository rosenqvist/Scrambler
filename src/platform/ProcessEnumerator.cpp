#include "platform/ProcessEnumerator.h"

#include "core/Types.h"

#include <windows.h>

#include <array>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <tlhelp32.h>
#include <type_traits>
#include <utility>

namespace scrambler::platform
{

namespace
{

struct HandleCloser
{
    void operator()(HANDLE handle) const noexcept
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle);
        }
    }
};

using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;

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

uint64_t FileTimeToUint64(const FILETIME& time)
{
    return (static_cast<uint64_t>(time.dwHighDateTime) << 32U) | time.dwLowDateTime;
}

bool EqualsInsensitive(const std::wstring& lhs, const std::wstring& rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    return _wcsnicmp(lhs.c_str(), rhs.c_str(), lhs.size()) == 0;
}

std::string WideToUtf8(std::wstring_view value)
{
    if (value.empty())
    {
        return {};
    }

    const auto input_len = static_cast<int>(value.size());
    const wchar_t* const input = value.data();
    const int required_len = WideCharToMultiByte(CP_UTF8, 0, input, input_len, nullptr, 0, nullptr, nullptr);
    if (required_len <= 0)
    {
        return {};
    }

    std::string result;
    result.resize_and_overwrite(static_cast<size_t>(required_len),
                                [&](char* data, size_t size) noexcept
    {
        const int written =
            WideCharToMultiByte(CP_UTF8, 0, input, input_len, data, static_cast<int>(size), nullptr, nullptr);
        return written > 0 ? static_cast<size_t>(written) : 0U;
    });
    return result;
}

std::expected<ProcessIdentity, ProcessIdentityError> ReadProcessIdentity(HANDLE process, uint32_t pid)
{
    std::array<wchar_t, MAX_PATH> path_buf{};
    auto path_size = static_cast<DWORD>(path_buf.size());
    if (QueryFullProcessImageNameW(process, 0, path_buf.data(), &path_size) == 0 || path_size == 0)
    {
        return std::unexpected(ProcessIdentityError::kImagePathUnavailable);
    }

    FILETIME creation_time{};
    FILETIME exit_time{};
    FILETIME kernel_time{};
    FILETIME user_time{};
    if (GetProcessTimes(process, &creation_time, &exit_time, &kernel_time, &user_time) == 0)
    {
        return std::unexpected(ProcessIdentityError::kCreationTimeUnavailable);
    }

    return ProcessIdentity{
        .pid = pid,
        .creation_time = FileTimeToUint64(creation_time),
        .exe_path = std::wstring(path_buf.data(), path_size),
    };
}

}  // namespace

bool StartsWithInsensitive(const std::wstring& str, const std::wstring& prefix)
{
    if (str.size() < prefix.size())
    {
        return false;
    }
    return _wcsnicmp(str.c_str(), prefix.c_str(), prefix.size()) == 0;
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

std::expected<ProcessIdentity, ProcessIdentityError> GetProcessIdentity(uint32_t pid)
{
    UniqueHandle proc(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!proc)
    {
        return std::unexpected(ProcessIdentityError::kOpenFailed);
    }

    return ReadProcessIdentity(proc.get(), pid);
}

bool IsProcessIdentityCurrent(const ProcessIdentity& identity)
{
    if (!identity.IsValid())
    {
        return false;
    }

    const auto current = GetProcessIdentity(identity.pid);
    return current.has_value() && current->creation_time == identity.creation_time
           && EqualsInsensitive(current->exe_path, identity.exe_path);
}

std::vector<ProcessInfo> EnumerateProcesses()
{
    std::vector<ProcessInfo> result;

    const uint32_t self_pid = GetCurrentProcessId();

    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE)
    {
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot.get(), &entry) == 0)
    {
        return result;
    }

    while (true)
    {
        auto pid = static_cast<uint32_t>(entry.th32ProcessID);
        auto parent_pid = static_cast<uint32_t>(entry.th32ParentProcessID);

        //* Skip idle + system + self
        if (pid != 0 && pid != scrambler::core::kSystemPid && pid != self_pid)
        {
            const auto* exe = static_cast<const wchar_t*>(entry.szExeFile);
            std::string name = WideToUtf8(exe);

            auto identity = GetProcessIdentity(pid);
            if (identity.has_value() && !(IsSystemProcessPath(identity->exe_path) || IsSessionZero(pid)))
            {
                result.push_back(ProcessInfo{
                    .pid = pid,
                    .parent_pid = parent_pid,
                    .name = std::move(name),
                    .exe_path = std::move(identity->exe_path),
                    .creation_time = identity->creation_time,
                });
            }
        }

        if (Process32NextW(snapshot.get(), &entry) == 0)
        {
            break;
        }
    }

    return result;
}

}  // namespace scrambler::platform
