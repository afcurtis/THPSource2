// THPSource2 launcher: the update notice's one web request (docs/LAUNCHER.md, "Updates"; the rules of what the answer means are core/update_check.hpp).
#pragma once

#include "update_check.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace thpsource2 {

// One look at GitHub's newest release, made on a thread of its own so the window never waits for the network. The launcher asks where
// `<repository>/releases/latest` redirects to (Windows' curl.exe, a HEAD request: nothing is downloaded, nothing about the player is sent beyond what any web request
// shows) and reads the version out of the tag in the answer. Read it from the window's thread with state() and latest(); both are safe while the thread runs.
class UpdateCheck {
public:
    enum class State {
        Checking,   // the request is out
        Newer,      // the newest release is newer than the running launcher
        Current,    // the newest release is the running one (or older)
        NoRelease,  // GitHub answered, but there is no release with a version for a tag (none published yet, a private or renamed repository)
        Failed,     // no answer: offline, no curl.exe, a timeout
    };

    // Starts the request and returns at once. `running_tag` is the launcher's own version as a release tag ("v0.7"). The thread keeps the check alive on its own, so a
    // window that closes first never waits for it (curl gives up after about ten seconds by itself)
    static std::shared_ptr<UpdateCheck> start(const std::string& repository_url, const std::string& running_tag);
    // An answer that is already in (the screenshot mode of the tests: no network)
    static std::shared_ptr<UpdateCheck> answered(State state, const std::string& latest_tag, const std::optional<update::Release>& release = std::nullopt);

    State state() const;
    std::string latest() const;   // the newest release's tag as written ("v0.8"), once the state is Newer or Current
    // The newer release's notes and package size, read from the GitHub API once the state is Newer (a second request, only then). Empty until it is in, and when the API
    // could not be reached: the update still installs, the notice only has no notes
    std::optional<update::Release> release() const;

private:
    mutable std::mutex lock_;
    State state_ = State::Checking;
    std::string latest_;
    std::optional<update::Release> release_;
    void finish(State state, const std::string& latest_tag);
    void finish_release(const update::Release& release);
};

// Downloading a release and laying it out for the updater script (update::apply_script), on a thread of its own. Everything is checked before the player is asked to
// restart: the package's SHA-256 against the release's own checksum file, its size against the API's, and that the unpacked folder holds a launcher and a game.
// Nothing of the install is touched until apply() runs. The addresses are built from the repository's address and the tag, never taken from an answer
// (update::asset_url). Staging is `<install>\Data\Update\`.
class UpdateInstall {
public:
    enum class State {
        Downloading,   // the checksum file and the package
        Unpacking,     // checked, being unpacked
        Ready,         // unpacked and the script written: apply() can run
        Failed,        // error() says why
    };

    static std::shared_ptr<UpdateInstall> start(const std::string& repository_url, const update::Release& release, const std::filesystem::path& install_folder);

    State state() const;
    std::string error() const;
    // Bytes of the package so far and in all (0 when the API gave no size), for a bar
    std::uint64_t done() const { return done_; }
    std::uint64_t total() const { return total_; }
    void cancel() { cancel_ = true; }
    const std::string& tag() const { return tag_; }

    // Ready only: starts the script, which waits for this process to end. True when it started; the launcher then closes at once
    bool apply(std::string& error);

    // The staged folder, for a launcher that starts and finds a leftover: removes it (a cancelled or an old update)
    static void discard(const std::filesystem::path& install_folder);
    static std::filesystem::path staging(const std::filesystem::path& install_folder);

private:
    mutable std::mutex lock_;
    State state_ = State::Downloading;
    std::string error_;
    std::string tag_;
    std::filesystem::path install_;
    std::atomic<std::uint64_t> done_{0}, total_{0};
    std::atomic<bool> cancel_{false};
    void finish(State state, const std::string& error);
};

}  // namespace thpsource2
