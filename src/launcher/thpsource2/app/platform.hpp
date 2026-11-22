// THPSource2 launcher: the few Windows services the app needs (a child process with its output captured, the environment it inherits, Explorer).
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace thpsource2 {

std::filesystem::path path_from_utf8(const std::string& text);
std::string utf8_of(const std::filesystem::path& path);

// Starts `program` with `arguments` in `directory` with the variables of `environment` set (an empty value unsets one) and calls `on_line` for each line it
// prints. Waits for it; `on_started` is called once it runs. The exit code, or -1 with `error` when it did not start.
long run_process(const std::filesystem::path& program, const std::vector<std::string>& arguments, const std::filesystem::path& directory,
                 const std::vector<std::pair<std::string, std::string>>& environment, const std::function<void(const std::string&)>& on_line,
                 const std::function<void()>& on_started, std::string& error);

// Starts `program` with this process's environment minus every OT_ and OPENTONY_ variable, plus `environment` (nothing is set in this process, so runs can go
// side by side), its output into the file `output`, no window. Every half second `keep_going` is asked; false ends the program. The exit code, -1 with `error`.
long run_isolated(const std::filesystem::path& program, const std::vector<std::string>& arguments, const std::filesystem::path& directory,
                  const std::vector<std::pair<std::string, std::string>>& environment, const std::filesystem::path& output, const std::function<bool()>& keep_going,
                  std::string& error);

void open_in_explorer(const std::filesystem::path& path);
// Explorer on the folder of `file`, with the file selected (a zip to drag into a web page: no hunting for it)
void reveal_in_explorer(const std::filesystem::path& file);

// A program that ships in Windows' own System32 folder (curl.exe, tar.exe), by its full path; the bare name when the folder cannot be found
std::filesystem::path system_program(const char* name);

// The folder of this program (no SDL needed: it runs before SDL3.dll is found), and a folder LoadLibrary searches for the DLLs the program loads later
std::filesystem::path executable_directory();
void add_dll_directory(const std::filesystem::path& directory);

// Gives the window (an HWND) the launcher's own icon (resource 1, app/launcher.rc), at the sizes the title bar and the taskbar use
void set_window_icon(void* hwnd);
// The bytes of one of the program's RCDATA resources (app/launcher.rc), empty when there is none
std::vector<unsigned char> resource_bytes(int id);

// Zips `folder` (kept as the zip's top folder) into `zip_file`, with Windows' own tar.exe. False with `error` when it could not
bool zip_folder(const std::filesystem::path& folder, const std::filesystem::path& zip_file, std::string& error);

// The newest line of Logs\sessions.txt (opentony.exe writes one per run), empty when there is none
std::string last_session_line(const std::filesystem::path& sessions_file);

}  // namespace thpsource2
