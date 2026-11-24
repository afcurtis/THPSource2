// THPSource2 launcher: the update notice's one web request (update_task.hpp).
#include "update_task.hpp"

#include "game_files.hpp"
#include "platform.hpp"
#include "update_check.hpp"

#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace thpsource2 {

namespace fs = std::filesystem;

std::shared_ptr<UpdateCheck> UpdateCheck::start(const std::string& repository_url, const std::string& running_tag) {
    auto check = std::make_shared<UpdateCheck>();
    // the thread owns a share of the check, so it may outlive the window; it only ever writes through finish()
    std::thread([check, repository_url, running_tag] {
        const fs::path curl = system_program("curl.exe");   // Windows 10 1803 and later ship it, as the texture upscaler's download already relies on
        std::error_code ec;
        if (!fs::is_regular_file(curl, ec)) {
            check->finish(State::Failed, {});
            return;
        }
        // HEAD of <repository>/releases/latest, not followed: GitHub answers 302 with the release's own page as the address, and curl writes that address out.
        // A repository with no release answers with its releases list instead, a missing one with 404 (an empty address). --ssl-no-revoke: Windows' certificate
        // revocation lookup fails behind some proxies and antivirus filters, and a check that silently never works is worse than one that skips it; the answer
        // only ever decides whether a notice shows, and the notice opens an address of the launcher's own, never one from the network.
        const std::vector<std::string> arguments = {"--silent", "--head", "--ssl-no-revoke", "--proto", "=https", "--connect-timeout", "5", "--max-time", "10",
                                                    "--user-agent", "THPSource2/" + running_tag, "--output", "NUL", "--write-out", "%{redirect_url}",
                                                    repository_url + "/releases/latest"};
        std::string location, error;
        const long code = run_process(curl, arguments, curl.parent_path(), {},
                                      [&location](const std::string& line) {
                                          if (location.empty()) {
                                              location = line;
                                          }
                                      },
                                      nullptr, error);
        if (code != 0 || !error.empty()) {
            check->finish(State::Failed, {});
            return;
        }
        const std::optional<std::string> tag = update::tag_from_redirect(location, repository_url);
        if (!tag) {
            check->finish(State::NoRelease, {});
            return;
        }
        const bool newer = update::is_newer(*tag, running_tag);
        check->finish(newer ? State::Newer : State::Current, *tag);
        if (!newer) {
            return;
        }
        // the notes: the API's page for that release (unauthenticated requests are limited to 60 an hour for an address, and only a newer release asks)
        const std::string api = update::api_url(repository_url, *tag);
        std::string json;
        std::string api_error;
        const std::vector<std::string> api_arguments = {"--silent", "--fail", "--ssl-no-revoke", "--proto", "=https", "--connect-timeout", "5", "--max-time", "15",
                                                        "--user-agent", "THPSource2/" + running_tag, "--header", "Accept: application/vnd.github+json", api};
        if (api.empty() || run_process(curl, api_arguments, curl.parent_path(), {}, [&json](const std::string& line) { json += line + "\n"; }, nullptr, api_error) != 0) {
            return;
        }
        if (const std::optional<update::Release> release = update::parse_release(json, *tag)) {
            check->finish_release(*release);
        }
    }).detach();
    return check;
}

std::shared_ptr<UpdateCheck> UpdateCheck::answered(State state, const std::string& latest_tag, const std::optional<update::Release>& release) {
    auto check = std::make_shared<UpdateCheck>();
    check->finish(state, latest_tag);
    if (release) {
        check->finish_release(*release);
    }
    return check;
}

UpdateCheck::State UpdateCheck::state() const {
    const std::lock_guard<std::mutex> guard(lock_);
    return state_;
}

std::string UpdateCheck::latest() const {
    const std::lock_guard<std::mutex> guard(lock_);
    return latest_;
}

std::optional<update::Release> UpdateCheck::release() const {
    const std::lock_guard<std::mutex> guard(lock_);
    return release_;
}

void UpdateCheck::finish_release(const update::Release& release) {
    const std::lock_guard<std::mutex> guard(lock_);
    release_ = release;
}

void UpdateCheck::finish(State state, const std::string& latest_tag) {
    const std::lock_guard<std::mutex> guard(lock_);
    state_ = state;
    latest_ = latest_tag;
}

// ---- the install ----

fs::path UpdateInstall::staging(const fs::path& install_folder) {
    return install_folder / "Data" / "Update";
}

void UpdateInstall::discard(const fs::path& install_folder) {
    std::error_code ec;
    const fs::path folder = staging(install_folder);
    fs::remove_all(folder / "unpack", ec);
    fs::remove(folder / update::kPackageName, ec);
    fs::remove(folder / update::kChecksumName, ec);
    fs::remove(folder / "apply.cmd", ec);
    fs::remove(folder / "curl.txt", ec);
    fs::remove(folder, ec);   // only when it is empty (failed.txt, the updater's own note, stays until the launcher has shown it)
}

std::shared_ptr<UpdateInstall> UpdateInstall::start(const std::string& repository_url, const update::Release& release, const fs::path& install_folder) {
    auto install = std::make_shared<UpdateInstall>();
    install->tag_ = release.tag;
    install->install_ = install_folder;
    install->total_ = release.zip_size;
    std::thread([install, repository_url, release, install_folder] {
        const fs::path folder = staging(install_folder);
        const fs::path zip = folder / update::kPackageName;
        const fs::path checksum_file = folder / update::kChecksumName;
        std::error_code ec;
        discard(install_folder);
        fs::create_directories(folder, ec);
        const fs::path curl = system_program("curl.exe");
        const fs::path tar = system_program("tar.exe");
        if (!fs::is_regular_file(curl, ec) || !fs::is_regular_file(tar, ec)) {
            install->finish(State::Failed, "curl.exe or tar.exe is missing from Windows (both come with Windows 10 1803 and later).");
            return;
        }
        if (!fs::is_directory(folder, ec)) {
            install->finish(State::Failed, "Cannot write in " + utf8_of(folder) + ". Is the folder read-only?");
            return;
        }
        auto fail = [&](const std::string& why) {
            const bool cancelled = install->cancel_;
            std::error_code remove_ec;
            fs::remove(zip, remove_ec);
            fs::remove(checksum_file, remove_ec);
            fs::remove_all(folder / "unpack", remove_ec);
            install->finish(State::Failed, cancelled ? "Cancelled." : why);
        };
        // curl needs --location: a release asset is served from another host. --proto-redir keeps every hop on https
        const std::vector<std::string> common = {"--location", "--fail", "--silent", "--show-error", "--ssl-no-revoke", "--proto", "=https", "--proto-redir", "=https",
                                                 "--connect-timeout", "10", "--retry", "2", "--user-agent", "THPSource2/" + release.tag};
        // 1. the checksum file, a few dozen bytes
        {
            std::vector<std::string> arguments = common;
            arguments.insert(arguments.end(), {"--max-time", "30", "--output", utf8_of(checksum_file), update::asset_url(repository_url, release.tag, update::kChecksumName)});
            std::string said, run_error;
            const long code = run_process(curl, arguments, folder, {}, [&said](const std::string& line) { said = line; }, nullptr, run_error);
            if (code != 0) {
                fail("This release has no checksum file to check the download against" + (said.empty() ? std::string() : " (" + said + ")") + ".");
                return;
            }
        }
        std::string expected;
        {
            std::ifstream in(checksum_file, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            const std::optional<std::string> hash = update::checksum_from_file(text.str(), update::kPackageName);
            if (!hash) {
                fail(std::string("The release's ") + update::kChecksumName + " is not a checksum of " + update::kPackageName + ".");
                return;
            }
            expected = *hash;
        }
        // 2. the package, with the bar fed by the size of the file as it grows (run_isolated asks every half second whether to go on: a cancel ends curl)
        {
            std::vector<std::string> arguments = common;
            arguments.insert(arguments.end(), {"--max-time", "1800", "--output", utf8_of(zip), update::asset_url(repository_url, release.tag, update::kPackageName)});
            std::string run_error;
            const long code = run_isolated(curl, arguments, folder, {}, folder / "curl.txt",
                                           [&install, &zip] {
                                               std::error_code size_ec;
                                               const std::uintmax_t size = fs::file_size(zip, size_ec);
                                               install->done_ = size_ec ? 0 : size;
                                               return !install->cancel_;
                                           },
                                           run_error);
            std::string said;
            {
                std::ifstream log(folder / "curl.txt");
                for (std::string line; std::getline(log, line);) {
                    if (!line.empty()) {
                        said = line;
                    }
                }
            }
            if (code != 0) {
                fail("The download failed (" + (run_error.empty() ? "curl exit " + std::to_string(code) : run_error) + (said.empty() ? std::string() : ": " + said) + "). Is the PC online?");
                return;
            }
        }
        std::error_code size_ec;
        const std::uintmax_t size = fs::file_size(zip, size_ec);
        if (size_ec || size == 0 || (release.zip_size != 0 && size != release.zip_size)) {
            fail("The download is " + std::to_string(size_ec ? 0 : size) + " bytes, the release lists " + std::to_string(release.zip_size) + ".");
            return;
        }
        install->done_ = size;
        if (install->cancel_) {
            fail("Cancelled.");
            return;
        }
        if (sha256_file(zip) != expected) {
            fail("The download does not match the release's checksum, so it was thrown away. Try again, or download it from the release page.");
            return;
        }
        // 3. unpack: a package holds one folder, thpsource2-win64, with the launcher, Build and Licenses in it
        install->finish(State::Unpacking, {});
        const fs::path unpack = folder / "unpack";
        fs::create_directories(unpack, ec);
        std::string run_error;
        const long code = run_process(tar, {"-xf", utf8_of(zip), "-C", utf8_of(unpack)}, folder, {}, nullptr, nullptr, run_error);
        const fs::path release_root = unpack / "thpsource2-win64";
        if (code != 0 || !fs::is_regular_file(release_root / "THPSource2.exe", ec) || !fs::is_regular_file(release_root / "Build" / "opentony.exe", ec) ||
            !fs::is_regular_file(release_root / "Build" / "SDL3.dll", ec)) {
            fail("The download could not be unpacked (" + (run_error.empty() ? "tar exit " + std::to_string(code) : run_error) + "), or it is not a THPSource2 release.");
            return;
        }
        // 4. the script
        std::ofstream script(folder / "apply.cmd", std::ios::binary | std::ios::trunc);
        script << update::apply_script(release.tag);
        script.close();
        if (!script) {
            fail("Cannot write the updater script in " + utf8_of(folder) + ".");
            return;
        }
        install->finish(State::Ready, {});
    }).detach();
    return install;
}

UpdateInstall::State UpdateInstall::state() const {
    const std::lock_guard<std::mutex> guard(lock_);
    return state_;
}

std::string UpdateInstall::error() const {
    const std::lock_guard<std::mutex> guard(lock_);
    return error_;
}

void UpdateInstall::finish(State state, const std::string& error) {
    const std::lock_guard<std::mutex> guard(lock_);
    state_ = state;
    error_ = error;
}

bool UpdateInstall::apply(std::string& error) {
    if (state() != State::Ready) {
        error = "The update is not ready.";
        return false;
    }
    return start_batch(staging(install_) / "apply.cmd", std::to_string(current_process_id()), error);
}

}  // namespace thpsource2
