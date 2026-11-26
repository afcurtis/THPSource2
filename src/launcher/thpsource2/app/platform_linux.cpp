#include "platform.hpp"

#include <SDL3/SDL.h>

#include <spawn.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string_view>

extern char** environ;

namespace fs = std::filesystem;

namespace thpsource2 {

fs::path path_from_utf8(const std::string& text) { return fs::path(text); }
std::string utf8_of(const fs::path& path) { return path.string(); }

long run_process(const fs::path& program, const std::vector<std::string>& arguments, const fs::path& directory,
                 const std::vector<std::pair<std::string, std::string>>& environment,
                 const std::function<void(const std::string&)>& on_line, const std::function<void()>& on_started, std::string& error) {
    std::map<std::string, std::string> variables;
    for (auto entry = environ; *entry != nullptr; ++entry) {
        const std::string_view line(*entry);
        const auto separator = line.find('=');
        if (separator != std::string_view::npos) {
            variables.emplace(line.substr(0, separator), line.substr(separator + 1));
        }
    }
    for (const auto& [name, value] : environment) {
        if (value.empty()) {
            variables.erase(name);
        } else {
            variables[name] = value;
        }
    }
    std::vector<std::string> env_storage;
    for (const auto& [name, value] : variables) {
        env_storage.push_back(name + "=" + value);
    }
    auto pointers = [](std::vector<std::string>& strings) {
        std::vector<char*> result;
        for (auto& item : strings) {
            result.push_back(item.data());
        }
        result.push_back(nullptr);
        return result;
    };
    std::vector<std::string> command{program.string()};
    command.insert(command.end(), arguments.begin(), arguments.end());
    auto argv = pointers(command);
    auto envp = pointers(env_storage);
    int descriptors[2];
    if (pipe2(descriptors, O_CLOEXEC) != 0) {
        error = std::strerror(errno);
        return -1;
    }

    posix_spawn_file_actions_t actions;
    int status = posix_spawn_file_actions_init(&actions);
    const bool actions_ready = status == 0;
    if (status == 0) {
        if (!directory.empty()) {
            status = posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
        }
        if (status == 0) {
            status = posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO);
        }
        if (status == 0) {
            status = posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDERR_FILENO);
        }
    }
    pid_t child = 0;
    if (status == 0) {
        status = posix_spawnp(&child, program.c_str(), &actions, nullptr, argv.data(), envp.data());
    }
    if (actions_ready) {
        posix_spawn_file_actions_destroy(&actions);
    }
    close(descriptors[1]);
    if (status != 0) {
        close(descriptors[0]);
        error = program.string() + ": " + std::strerror(status);
        return -1;
    }
    if (on_started) {
        on_started();
    }
    std::string pending;
    char buffer[4096];
    for (;;) {
        const auto count = read(descriptors[0], buffer, sizeof buffer);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        pending.append(buffer, static_cast<std::size_t>(count));
        for (auto end = pending.find('\n'); end != std::string::npos; end = pending.find('\n')) {
            if (on_line) {
                on_line(pending.substr(0, end));
            }
            pending.erase(0, end + 1);
        }
    }
    close(descriptors[0]);
    if (on_line && !pending.empty()) {
        on_line(pending);
    }
    int result = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &result, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        error = std::strerror(errno);
        return -1;
    }
    return WIFEXITED(result) ? WEXITSTATUS(result) : 128 + WTERMSIG(result);
}

long run_isolated(const fs::path&, const std::vector<std::string>&, const fs::path&,
                  const std::vector<std::pair<std::string, std::string>>&, const fs::path&, const std::function<bool()>&,
                  std::string& error) {
    error = "The Windows texture census is unavailable in the Linux launcher.";
    return -1;
}

bool start_batch(const fs::path&, const std::string&, std::string& error) {
    error = "Automatic installation of Windows updates is unavailable on Linux.";
    return false;
}

unsigned long current_process_id() { return static_cast<unsigned long>(getpid()); }

void open_in_explorer(const fs::path& path) {
    // SDL opens URLs on the desktop; percent-encode path bytes that can be interpreted as URL syntax.
    const auto value = fs::absolute(path).string();
    std::string url = "file://";
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '/' || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            url.push_back(static_cast<char>(ch));
        } else {
            url.push_back('%');
            url.push_back(hex[ch >> 4]);
            url.push_back(hex[ch & 15]);
        }
    }
    SDL_OpenURL(url.c_str());
}

void reveal_in_explorer(const fs::path& file) { open_in_explorer(file.parent_path()); }

fs::path system_program(const char* name) {
    const fs::path path(name);
    return path.extension() == ".exe" ? path.stem() : path;
}

fs::path executable_directory() { return fs::read_symlink("/proc/self/exe").parent_path(); }
void add_dll_directory(const fs::path&) {}
void set_window_icon(void*) {}

std::vector<unsigned char> resource_bytes(int id) {
    if (id != 2) {
        return {};
    }
    std::ifstream file(executable_directory() / "thpsource2-header.png", std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

bool zip_folder(const fs::path& folder, const fs::path& zip_file, std::string& error) {
    return run_process("zip", {"-q", "-r", fs::absolute(zip_file).string(), folder.filename().string()},
                       folder.parent_path(), {}, {}, {}, error) == 0;
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
