#include "platform.hpp"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iterator>

namespace thpsource2 {

namespace fs = std::filesystem;

namespace {

std::wstring wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

// CommandLineToArgvW's quoting rules
std::wstring quoted(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        out.append(c == L'"' ? 2 * backslashes + 1 : backslashes, L'\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(2 * backslashes, L'\\');
    out.push_back(L'"');
    return out;
}

}  // namespace

fs::path path_from_utf8(const std::string& text) {
    return fs::path(wide(text));
}

std::string utf8_of(const fs::path& path) {
    return narrow(path.wstring());
}

long run_process(const fs::path& program, const std::vector<std::string>& arguments, const fs::path& directory,
                 const std::vector<std::pair<std::string, std::string>>& environment, const std::function<void(const std::string&)>& on_line,
                 const std::function<void()>& on_started, std::string& error) {
    for (const auto& [name, value] : environment) {
        SetEnvironmentVariableW(wide(name).c_str(), value.empty() ? nullptr : wide(value).c_str());   // the child inherits this process's environment
    }
    std::wstring command = quoted(program.wstring());
    for (const std::string& argument : arguments) {
        command += L" " + quoted(wide(argument));
    }
    SECURITY_ATTRIBUTES inherit = {sizeof inherit, nullptr, TRUE};
    HANDLE read_end = nullptr, write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &inherit, 0)) {
        error = "cannot create a pipe (error " + std::to_string(GetLastError()) + ")";
        return -1;
    }
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    HANDLE null_input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW startup = {};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = null_input;
    startup.hStdOutput = write_end;
    startup.hStdError = write_end;
    PROCESS_INFORMATION process = {};
    const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, directory.wstring().c_str(), &startup, &process);
    const DWORD start_error = GetLastError();
    CloseHandle(write_end);
    if (null_input != INVALID_HANDLE_VALUE) {
        CloseHandle(null_input);
    }
    if (!started) {
        CloseHandle(read_end);
        error = "cannot start " + utf8_of(program) + " (error " + std::to_string(start_error) + ")";
        return -1;
    }
    if (on_started) {
        on_started();
    }
    std::string pending;
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(read_end, buffer, sizeof buffer, &got, nullptr) && got > 0) {
        pending.append(buffer, got);
        std::size_t end;
        while ((end = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, end);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (on_line) {
                on_line(line);
            }
            pending.erase(0, end + 1);
        }
    }
    if (!pending.empty() && on_line) {
        on_line(pending);
    }
    CloseHandle(read_end);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<long>(code);
}

long run_isolated(const fs::path& program, const std::vector<std::string>& arguments, const fs::path& directory,
                  const std::vector<std::pair<std::string, std::string>>& environment, const fs::path& output, const std::function<bool()>& keep_going,
                  std::string& error) {
    // this process's environment without any OT_ or OPENTONY_ variable (the launcher's own settings never reach a census run), then the run's
    std::vector<std::wstring> entries;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* p = block; *p != L'\0'; p += std::wcslen(p) + 1) {
            const std::wstring entry = p;
            std::wstring upper = entry.substr(0, 9);
            for (wchar_t& c : upper) {
                c = static_cast<wchar_t>(std::towupper(c));
            }
            if (upper.rfind(L"OT_", 0) != 0 && upper.rfind(L"OPENTONY_", 0) != 0) {
                entries.push_back(entry);
            }
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& [name, value] : environment) {
        if (!value.empty()) {
            entries.push_back(wide(name) + L"=" + wide(value));
        }
    }
    std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::wstring block;
    for (const std::wstring& entry : entries) {
        block += entry;
        block += L'\0';
    }
    block += L'\0';
    std::wstring command = quoted(program.wstring());
    for (const std::string& argument : arguments) {
        command += L" " + quoted(wide(argument));
    }
    SECURITY_ATTRIBUTES inherit = {sizeof inherit, nullptr, TRUE};
    HANDLE log = CreateFileW(output.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE null_input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW startup = {};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = null_input;
    startup.hStdOutput = log;
    startup.hStdError = log;
    PROCESS_INFORMATION process = {};
    const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, block.data(),
                                        directory.wstring().c_str(), &startup, &process);
    const DWORD start_error = GetLastError();
    if (log != INVALID_HANDLE_VALUE) {
        CloseHandle(log);
    }
    if (null_input != INVALID_HANDLE_VALUE) {
        CloseHandle(null_input);
    }
    if (!started) {
        error = "cannot start " + utf8_of(program) + " (error " + std::to_string(start_error) + ")";
        return -1;
    }
    while (WaitForSingleObject(process.hProcess, 500) == WAIT_TIMEOUT) {
        if (keep_going && !keep_going()) {
            TerminateProcess(process.hProcess, 0xdead);
            WaitForSingleObject(process.hProcess, 10000);
            break;
        }
    }
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<long>(code);
}

void open_in_explorer(const fs::path& path) {
    ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void reveal_in_explorer(const fs::path& file) {
    fs::path preferred = file;
    preferred.make_preferred();   // /select, wants backslashes
    const std::wstring arguments = L"/select,\"" + preferred.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

fs::path system_program(const char* name) {
    wchar_t folder[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(folder, MAX_PATH);
    return length != 0 && length < MAX_PATH ? fs::path(std::wstring(folder, length)) / name : fs::path(name);
}

fs::path executable_directory() {
    wchar_t self[32768];
    const DWORD length = GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
    return length != 0 && length < std::size(self) ? fs::path(std::wstring(self, length)).parent_path() : fs::current_path();
}

void add_dll_directory(const fs::path& directory) {
    SetDllDirectoryW(directory.wstring().c_str());
}

void set_window_icon(void* hwnd) {
    if (hwnd == nullptr) {
        return;
    }
    const HINSTANCE self = GetModuleHandleW(nullptr);
    const auto load = [&](int metric_x, int metric_y) {
        return static_cast<HICON>(LoadImageW(self, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(metric_x), GetSystemMetrics(metric_y), LR_SHARED));
    };
    // shared handles: Windows owns them, nothing to free
    if (HICON big = load(SM_CXICON, SM_CYICON)) {
        SendMessageW(static_cast<HWND>(hwnd), WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
    }
    if (HICON small = load(SM_CXSMICON, SM_CYSMICON)) {
        SendMessageW(static_cast<HWND>(hwnd), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
    }
}

std::vector<unsigned char> resource_bytes(int id) {
    const HMODULE self = GetModuleHandleW(nullptr);
    HRSRC found = FindResourceW(self, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
    HGLOBAL loaded = found != nullptr ? LoadResource(self, found) : nullptr;
    const auto* data = loaded != nullptr ? static_cast<const unsigned char*>(LockResource(loaded)) : nullptr;
    return data != nullptr ? std::vector<unsigned char>(data, data + SizeofResource(self, found)) : std::vector<unsigned char>{};
}

bool zip_folder(const fs::path& folder, const fs::path& zip_file, std::string& error) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        error = "the folder is gone: " + utf8_of(folder);
        return false;
    }
    fs::create_directories(zip_file.parent_path(), ec);
    fs::remove(zip_file, ec);
    // tar.exe has been part of Windows since 10 (build 17063): -a picks the zip format from the extension
    wchar_t system[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    const fs::path tar = (length != 0 && length < MAX_PATH ? fs::path(std::wstring(system, length)) : fs::path(L"C:\\Windows\\System32")) / L"tar.exe";
    if (!fs::is_regular_file(tar, ec)) {
        error = "Windows' tar.exe was not found, so the folder cannot be zipped; it is at " + utf8_of(folder);
        return false;
    }
    std::string output;
    const long code = run_process(tar, {"-a", "-c", "-f", utf8_of(zip_file), "-C", utf8_of(folder.parent_path()), utf8_of(folder.filename())}, folder.parent_path(), {},
                                  [&output](const std::string& line) { output += line + "\n"; }, nullptr, error);
    if (code != 0 || !fs::is_regular_file(zip_file, ec)) {
        if (error.empty()) {
            error = "tar.exe failed (exit code " + std::to_string(code) + "): " + output;
        }
        return false;
    }
    return true;
}

std::string last_session_line(const fs::path& sessions_file) {
    std::ifstream in(sessions_file);
    std::string line, last;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            last = line;
        }
    }
    return last;
}

}  // namespace thpsource2
