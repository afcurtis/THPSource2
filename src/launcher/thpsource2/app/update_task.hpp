// THPSource2 launcher: the update notice's one web request (docs/LAUNCHER.md, "Updates"; the rules of what the answer means are core/update_check.hpp).
#pragma once

#include <memory>
#include <mutex>
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
    static std::shared_ptr<UpdateCheck> answered(State state, const std::string& latest_tag);

    State state() const;
    std::string latest() const;   // the newest release's tag as written ("v0.8"), once the state is Newer or Current

private:
    mutable std::mutex lock_;
    State state_ = State::Checking;
    std::string latest_;
    void finish(State state, const std::string& latest_tag);
};

}  // namespace thpsource2
