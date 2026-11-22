#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace opentony::runtime {

// A service use the live host cannot honour (an unknown service, a scripted value no real call
// answers): the hook adapter reports it and stops the process.
class ServiceFault final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Phase 5.0: the running game (opentony_native.dll) turns each reported call into a real call of
// the retail function and answers with what it returned (EAX).
class ServiceHost {
public:
    virtual std::uint32_t call(const std::string& name, const std::vector<std::uint32_t>& args, const std::string& text) = 0;

protected:
    ~ServiceHost() = default;
};

namespace detail {

// the last live call and its result, per thread (a streaming thread may run a native too)
struct PendingServiceCall {
    std::string name;
    std::uint32_t result = 0;
    bool valid = false;
};

}  // namespace detail

// The outside world a ported retail function reaches: the assertion routine (M3dUtils_InBetween,
// which only logs), sound, on-screen messages, random numbers, the movie player. A port reports
// each reach as a named call in order, and asks for a value (a random number, "is a movie
// playing") by name. Replaying a retail capture scripts the values and compares the calls; in a
// running game the host turns calls into real effects.
//
// With a host attached (live), nothing is recorded: `note` makes the call, and the `value` of the
// same name that follows it is that call's result. A `value` with no call before it is a scripted
// answer only a replay has (a service's memory writes replayed as values): a ServiceFault.
class RetailServices final {
public:
    void attach(ServiceHost* host) noexcept { host_ = host; }
    [[nodiscard]] bool live() const noexcept { return host_ != nullptr; }

    struct Call {
        std::string name;
        std::vector<std::uint32_t> args;
        std::string text;  // the formatted string of a message call
        friend bool operator==(const Call&, const Call&) = default;
    };

    void note(std::string name, std::vector<std::uint32_t> args = {}, std::string text = {}) {
        if (host_ != nullptr) {
            const std::uint32_t result = host_->call(name, args, text);
            pending_ = {std::move(name), result, true};
            return;
        }
        calls_.push_back({std::move(name), std::move(args), std::move(text)});
    }
    // M3dUtils_InBetween(false, message): retail logs "ASSERT" and carries on; `message` is the
    // address of the format string (it names the assertion).
    void assert_failed(std::uint32_t message) { note("assert", {message}); }
    void check(bool condition, std::uint32_t message) {
        if (!condition) {
            assert_failed(message);
        }
    }

    // Live: the result of the call just made is read next under another name (a caller written for the replay's service names reads its virtual calls
    // by their slot's name, the call itself is made as `vcall_<address>`).
    void rename_pending(const std::string& name) {
        if (host_ != nullptr && pending_.valid) {
            pending_.name = name;
        }
    }
    // The next scripted value for `name`; `fallback` when the script has run out.
    [[nodiscard]] std::int32_t value(const std::string& name, std::int32_t fallback = 0) {
        if (host_ != nullptr) {
            if (!pending_.valid || pending_.name != name) {
                // OT_SERVICE_AUDIT=1 (a measurement mode, like OT_RESOLVER_LENIENT): list every such read and answer 0, so one run finds all of a screen's
                static const bool audit = std::getenv("OT_SERVICE_AUDIT") != nullptr;
                if (audit) {
                    std::fprintf(stderr, "audit: live service value '%s' has no call before it\n", name.c_str());
                    return fallback;
                }
                throw ServiceFault("live service value '" + name + "' has no call before it (a replay-only scripted value)");
            }
            // a Win32 or Bink import (`win32_<name>`) has one return value, which the code may read more than once (the message loop of
            // PCMovie_PlayStartupMedia reads PeekMessage's three scripted values); every other service answers once per call
            if (name.compare(0, 6, "win32_") != 0) {
                pending_.valid = false;
            }
            return static_cast<std::int32_t>(pending_.result);
        }
        auto it = values_.find(name);
        if (it == values_.end() || it->second.empty()) {
            return fallback;
        }
        const std::int32_t v = it->second.front();
        it->second.pop_front();
        return v;
    }
    void script(const std::string& name, std::int32_t value) { values_[name].push_back(value); }

    // A service that takes no arguments and returns a value (Video_Playing, Pack_NewFree): the
    // call is reported and the next scripted value returned.
    [[nodiscard]] std::int32_t query(const std::string& name, std::int32_t fallback = 0) {
        note(name);
        return value(name, fallback);
    }

    [[nodiscard]] const std::vector<Call>& calls() const noexcept { return calls_; }
    void clear() {
        calls_.clear();
        values_.clear();
    }

private:
    std::vector<Call> calls_;
    std::map<std::string, std::deque<std::int32_t>> values_;
    ServiceHost* host_ = nullptr;
    static inline thread_local detail::PendingServiceCall pending_;
};

}  // namespace opentony::runtime
