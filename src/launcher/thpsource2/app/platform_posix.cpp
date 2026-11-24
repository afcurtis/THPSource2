// The Linux side of platform.hpp (the Windows side is platform.cpp): processes with posix_spawn, the desktop's file manager through xdg-open, the
// resources linked into the program (launcher_resources.cpp, made by src/linux/CMakeLists.txt from app/icon.png and app/header.png).
#include "platform.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>

extern char** environ;

// launcher_resources.cpp: the bytes of resource `id`, or none
namespace thpsource2::resources {
const unsigned char* find(int id, std::size_t& size);
}

namespace thpsource2 {

namespace fs = std::filesystem;

namespace {

std::vector<char*> argv_of(const fs::path& program, const std::vector<std::string>& arguments, std::vector<std::string>& storage) {
    storage.clear();
    storage.push_back(program.string());
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (std::string& argument : storage) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    return argv;
}

long exit_code_of(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    // a signal reads as the Windows code of the same stop, as opentony's own run summary does
    return WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS ? static_cast<long>(0xc0000005u) : static_cast<long>(0xc0000000u | WTERMSIG(status));
}

// a program started on its own (a file manager): it is never waited for, and its child is reaped by a short-lived thread
void launch_detached(const std::vector<std::string>& command) {
    std::vector<std::string> storage;
    std::vector<char*> argv;
    for (const std::string& part : command) {
        storage.push_back(part);
    }
    for (std::string& part : storage) {
        argv.push_back(part.data());
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    pid_t child = 0;
    if (posix_spawnp(&child, argv[0], &actions, nullptr, argv.data(), environ) == 0) {
        std::thread([child] {
            int status = 0;
            waitpid(child, &status, 0);
        }).detach();
    }
    posix_spawn_file_actions_destroy(&actions);
}

}  // namespace

fs::path path_from_utf8(const std::string& text) { return fs::path(text); }

std::string utf8_of(const fs::path& path) { return path.string(); }

long run_process(const fs::path& program, const std::vector<std::string>& arguments, const fs::path& directory,
                 const std::vector<std::pair<std::string, std::string>>& environment, const std::function<void(const std::string&)>& on_line,
                 const std::function<void()>& on_started, std::string& error) {
    for (const auto& [name, value] : environment) {   // the child inherits this process's environment, as on Windows
        if (value.empty()) {
            ::unsetenv(name.c_str());
        } else {
            ::setenv(name.c_str(), value.c_str(), 1);
        }
    }
    int pipe_ends[2];
    if (::pipe2(pipe_ends, O_CLOEXEC) != 0) {
        error = std::string("cannot create a pipe (") + std::strerror(errno) + ")";
        return -1;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, pipe_ends[1], 1);
    posix_spawn_file_actions_adddup2(&actions, pipe_ends[1], 2);
    if (!directory.empty()) {
        posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
    }
    std::vector<std::string> storage;
    std::vector<char*> argv = argv_of(program, arguments, storage);
    pid_t child = 0;
    // a bare name (curl) is looked for on PATH, a path is started as it is
    const int spawned = program.has_parent_path() ? posix_spawn(&child, program.c_str(), &actions, nullptr, argv.data(), environ)
                                                  : posix_spawnp(&child, program.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(pipe_ends[1]);
    if (spawned != 0) {
        ::close(pipe_ends[0]);
        error = "cannot start " + utf8_of(program) + " (" + std::strerror(spawned) + ")";
        return -1;
    }
    if (on_started) {
        on_started();
    }
    std::string pending;
    char buffer[4096];
    for (;;) {
        const ssize_t got = ::read(pipe_ends[0], buffer, sizeof buffer);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            break;
        }
        pending.append(buffer, static_cast<std::size_t>(got));
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
    ::close(pipe_ends[0]);
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return exit_code_of(status);
}

long run_isolated(const fs::path& program, const std::vector<std::string>& arguments, const fs::path& directory,
                  const std::vector<std::pair<std::string, std::string>>& environment, const fs::path& output, const std::function<bool()>& keep_going,
                  std::string& error) {
    // this process's environment without any OT_ or OPENTONY_ variable (the launcher's own settings never reach a census run), then the run's
    std::vector<std::string> entries;
    for (char** entry = environ; *entry != nullptr; ++entry) {
        const std::string text = *entry;
        if (text.rfind("OT_", 0) != 0 && text.rfind("OPENTONY_", 0) != 0) {
            entries.push_back(text);
        }
    }
    for (const auto& [name, value] : environment) {
        if (!value.empty()) {
            entries.push_back(name + "=" + value);
        }
    }
    std::vector<char*> envp;
    for (std::string& entry : entries) {
        envp.push_back(entry.data());
    }
    envp.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    if (!directory.empty()) {
        posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
    }
    std::vector<std::string> storage;
    std::vector<char*> argv = argv_of(program, arguments, storage);
    pid_t child = 0;
    const int spawned = posix_spawn(&child, program.c_str(), &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) {
        error = "cannot start " + utf8_of(program) + " (" + std::strerror(spawned) + ")";
        return -1;
    }
    int status = 0;
    for (;;) {
        const pid_t got = ::waitpid(child, &status, WNOHANG);
        if (got == child) {
            break;
        }
        if (got < 0 && errno != EINTR) {
            return -1;
        }
        if (keep_going && !keep_going()) {
            ::kill(child, SIGKILL);   // TerminateProcess
            ::waitpid(child, &status, 0);
            return 0xdead;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return exit_code_of(status);
}

void open_in_explorer(const fs::path& path) { launch_detached({"xdg-open", path.string()}); }

void reveal_in_explorer(const fs::path& file) {
    // the freedesktop file manager interface selects the file where a file manager offers it; xdg-open of its folder otherwise
    const std::string uri = "file://" + fs::absolute(file).string();
    if (std::system(nullptr) != 0) {
        const std::string command = "dbus-send --session --dest=org.freedesktop.FileManager1 --type=method_call /org/freedesktop/FileManager1 "
                                    "org.freedesktop.FileManager1.ShowItems array:string:'" + uri + "' string:'' >/dev/null 2>&1";
        if (std::system(command.c_str()) == 0) {
            return;
        }
    }
    open_in_explorer(file.parent_path());
}

fs::path system_program(const char* name) {
    std::string bare = name;
    if (bare.size() > 4 && bare.compare(bare.size() - 4, 4, ".exe") == 0) {
        bare.resize(bare.size() - 4);   // curl.exe -> curl
    }
    const char* path = std::getenv("PATH");
    std::string directories = path != nullptr ? path : "/usr/bin:/bin";
    std::size_t at = 0;
    while (at <= directories.size()) {
        std::size_t next = directories.find(':', at);
        if (next == std::string::npos) {
            next = directories.size();
        }
        const fs::path candidate = fs::path(directories.substr(at, next - at)) / bare;
        if (::access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
        at = next + 1;
    }
    return fs::path("/usr/bin") / bare;   // not found: a path that is not there, as the Windows side answers for a missing program
}

fs::path executable_directory() {
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : self.parent_path();
}

void add_dll_directory(const fs::path&) {}   // SDL3 is the system's (libSDL3.so.0), linked as usual

void set_window_icon(void*) {}   // Linux: main.cpp sets the window icon through SDL from resource 1

std::vector<unsigned char> resource_bytes(int id) {
    std::size_t size = 0;
    const unsigned char* data = resources::find(id, size);
    return data != nullptr ? std::vector<unsigned char>(data, data + size) : std::vector<unsigned char>{};
}

bool zip_folder(const fs::path& folder, const fs::path& zip_file, std::string& error) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        error = "the folder is gone: " + utf8_of(folder);
        return false;
    }
    fs::create_directories(zip_file.parent_path(), ec);
    fs::remove(zip_file, ec);
    // zip (Info-ZIP), else bsdtar (libarchive: -a picks the zip format from the extension, as Windows' tar.exe does)
    std::string output;
    long code = -1;
    if (const fs::path zip = system_program("zip"); fs::is_regular_file(zip, ec)) {
        code = run_process(zip, {"-r", "-q", utf8_of(zip_file), utf8_of(folder.filename())}, folder.parent_path(), {},
                           [&output](const std::string& line) { output += line + "\n"; }, nullptr, error);
    } else if (const fs::path tar = system_program("bsdtar"); fs::is_regular_file(tar, ec)) {
        code = run_process(tar, {"-a", "-c", "-f", utf8_of(zip_file), "-C", utf8_of(folder.parent_path()), utf8_of(folder.filename())}, folder.parent_path(), {},
                           [&output](const std::string& line) { output += line + "\n"; }, nullptr, error);
    } else {
        error = "neither zip nor bsdtar is installed, so the folder cannot be zipped; it is at " + utf8_of(folder);
        return false;
    }
    if (code != 0 || !fs::is_regular_file(zip_file, ec)) {
        if (error.empty()) {
            error = "zipping failed (exit code " + std::to_string(code) + "): " + output;
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
