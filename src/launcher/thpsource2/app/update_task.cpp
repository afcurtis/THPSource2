// THPSource2 launcher: the update notice's one web request (update_task.hpp).
#include "update_task.hpp"

#include "platform.hpp"
#include "update_check.hpp"

#include <filesystem>
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
        } else {
            check->finish(update::is_newer(*tag, running_tag) ? State::Newer : State::Current, *tag);
        }
    }).detach();
    return check;
}

std::shared_ptr<UpdateCheck> UpdateCheck::answered(State state, const std::string& latest_tag) {
    auto check = std::make_shared<UpdateCheck>();
    check->finish(state, latest_tag);
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

void UpdateCheck::finish(State state, const std::string& latest_tag) {
    const std::lock_guard<std::mutex> guard(lock_);
    state_ = state;
    latest_ = latest_tag;
}

}  // namespace thpsource2
