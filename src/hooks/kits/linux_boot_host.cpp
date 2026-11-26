#include "linux_boot_host.hpp"
#include "linux_guest_memory.hpp"

#include "kit_bridge.hpp"
#include "seam_addresses_generated.hpp"
#include "standalone_thunks_generated.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace opentony::kitdll {
namespace {

template <std::size_t>
using Word = std::uint32_t;

template <std::size_t... I>
std::uint32_t invoke(const NativeEntry& native, const std::uint32_t* args, std::index_sequence<I...>) {
    if (native.convention == Convention::Thiscall) {
        using Function = std::uint32_t (*)(std::uint32_t, std::uint32_t, Word<I>...);
        return reinterpret_cast<Function>(native.function)(args[0], 0, args[I + 1]...);
    }
    using Function = std::uint32_t (*)(Word<I>...);
    return reinterpret_cast<Function>(native.function)(args[I]...);
}

template <std::size_t... N>
std::uint32_t dispatch(const NativeEntry& native, const std::uint32_t* args, std::index_sequence<N...>) {
    using Function = std::uint32_t (*)(const NativeEntry&, const std::uint32_t*);
    static constexpr Function calls[] = {
        +[](const NativeEntry& entry, const std::uint32_t* words) {
            return invoke(entry, words, std::make_index_sequence<N>{});
        }...
    };
    return calls[native.stack_args](native, args);
}

class BootHost final : public runtime::ServiceHost {
public:
    explicit BootHost(LinuxGuestMemory& arena) {
        memory_.make_live(nullptr, false, arena.base());
        services_.attach(this);
        make_kits(memory_, services_);
    }

    std::uint32_t resolve(std::uint32_t address, const std::vector<std::uint32_t>& args, bool virtual_call = false) {
        const auto* end = kNatives + kNativeCount;
        const auto* native = std::lower_bound(kNatives, end, address, [](const NativeEntry& entry, std::uint32_t value) {
            return entry.address < value;
        });
        if (native != end && native->address == address) {
            constexpr std::size_t kMaxStackArgs = 16;
            const bool has_self = native->convention == Convention::Thiscall;
            const std::size_t required = native->stack_args + (has_self ? 1u : 0u);
            const bool skip_self = !has_self && (virtual_call ||
                (native->convention == Convention::Stdcall && args.size() == required + 1));
            const auto words = skip_self && !args.empty() ? std::span(args).subspan(1) : std::span(args);
            if (native->stack_args > kMaxStackArgs || words.size() < required ||
                (native->convention != Convention::Cdecl && words.size() != required)) {
                throw runtime::ServiceFault(std::string(native->name) + ": unexpected argument count");
            }
            const auto saved = x87_control();
            x87_load(t_caller_x87);
            const auto result = dispatch(*native, words.data(), std::make_index_sequence<kMaxStackArgs + 1>{});
            x87_load(saved);
            return result;
        }

        if (address == seam::crt::CRT_atexit && args.size() == 1) {
            if (args.front() == 0) {
                return 0xffffffffu;
            }
            exit_callbacks_.push_back(args.front());
            return 0;
        }
        if (address == seam::crt::FUN_00500400 && args.size() >= 4) {  // CRT vector constructor iterator
            if (args[3] != 0) {
                for (std::uint32_t index = 0; index < args[2]; ++index) {
                    resolve(args[3], {args[0] + index * args[1]});
                }
            }
            return 0;
        }
        const auto* thunk = std::lower_bound(std::begin(kThunks), std::end(kThunks), address,
            [](const Thunk& entry, std::uint32_t value) { return entry.address < value; });
        if (thunk != std::end(kThunks) && thunk->address == address) {
            return resolve(thunk->target, args, virtual_call);
        }
        char hex[9];
        std::snprintf(hex, sizeof hex, "%08x", address);
        throw runtime::ServiceFault(std::string("Linux host has no service for 0x") + hex);
    }

    std::uint32_t call(const std::string& name, const std::vector<std::uint32_t>& args, const std::string&) override {
        const std::string_view service(name);
        const bool virtual_call = service.starts_with("vcall_");
        if (virtual_call || service.starts_with("engine_")) {
            const auto hex = service.substr(virtual_call ? 6 : 7);
            std::uint32_t address = 0;
            const auto parsed = std::from_chars(hex.data(), hex.data() + hex.size(), address, 16);
            if (parsed.ec == std::errc{} && parsed.ptr == hex.data() + hex.size()) {
                return resolve(address, args, virtual_call);
            }
        }
        for (const auto& entry : std::span(kLiveCalls, kLiveCallCount)) {
            if (name == entry.service) {
                std::vector<std::uint32_t> full(entry.lead, entry.lead_value);
                full.insert(full.end(), args.begin(), args.end());
                return resolve(entry.address, full);
            }
        }
        for (const auto& entry : std::span(kServices, kServiceCount)) {
            if (name == entry.name) {
                return resolve(entry.address, args);
            }
        }
        throw runtime::ServiceFault("Linux host has no service named " + name);
    }

private:
    RetailMemory memory_;
    RetailServices services_;
    std::vector<std::uint32_t> exit_callbacks_;
};

std::uintptr_t guest_base = 0;
BootHost* boot_host = nullptr;

}  // namespace

std::uintptr_t arena::base() noexcept { return guest_base; }

std::uint32_t call_native_or_seam(std::uint32_t address, const std::vector<std::uint32_t>& args) {
    return boot_host->resolve(address, args);
}

[[noreturn]] void fatal(const char* hook, const char* what) noexcept {
    std::fprintf(stderr, "Linux host: %s: %s\n", hook, what);
    std::exit(EXIT_FAILURE);
}

void run_linux_initializers(LinuxGuestMemory& arena) {
    guest_base = arena.base();
    BootHost host(arena);
    boot_host = &host;
    standalone_initialize();
    boot_host = nullptr;
    guest_base = 0;
}

}  // namespace opentony::kitdll
