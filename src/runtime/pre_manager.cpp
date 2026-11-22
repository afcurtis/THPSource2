#include "auto_views.hpp"
#include "pre_manager.hpp"

namespace opentony::runtime::pre {

using namespace opentony::runtime::autoviews;

namespace {
constexpr std::uint32_t kDelete = 0x00466bb0;      // operator delete(block)
constexpr const char* kMemDelete = "mem_delete";   // Mem_Delete(block)
constexpr const char* kLog = "printf_log";         // printf_log(format, ...): only the format is reported
}  // namespace

// 0x004a8f90: delete the global manager (its first virtual method is the destructor).
void PreManager::destroy_global() {
    const std::uint32_t manager = global_word(m_, kGlobalManager);
    s_.check(manager != 0, kMsgNoManager);
    if (manager != 0) {
        if (word_at(m_, manager) == kVtManager) {
            scalar_delete(manager, 1);
        } else {
            s_.note(pspool::service_name(word_at(m_, word_at(m_, manager))), {manager, 1});
        }
    }
    set_global_word(m_, kGlobalManager, 0);
}

// 0x004a9060
std::uint32_t PreManager::scalar_delete(std::uint32_t self, std::uint32_t flags) {
    destruct(self);
    if ((flags & 1) != 0) {
        s_.note(pspool::service_name(kDelete), {self});
    }
    return self;
}

// 0x004a9080: free every buffer the manager owns (the slots keep their stale pointers).
void PreManager::destruct(std::uint32_t self) {
    set_word_at(m_, self, kVtManager);
    for (std::uint32_t i = 0; i < kSlotCount; ++i) {
        if (const std::uint32_t buffer = word_at(m_, self + kSlots + 4 * i); buffer != 0) {
            s_.note(kMemDelete, {buffer});
        }
    }
    set_word_at(m_, self, 0x005184b4);
}

// 0x004a91d0: keep `buffer` in the first free slot under `name` (which must not be there already).
void PreManager::register_buffer(std::uint32_t self, std::uint32_t buffer, std::uint32_t name) {
    s_.check(buffer != 0, kMsgNoBuffer);
    s_.check(name != 0, kMsgNoFilename);
    s_.check(!find(self, name), kMsgAlreadyLoaded);
    std::uint32_t slot = 0;
    while (slot < kSlotCount && word_at(m_, self + kSlots + 4 * slot) != 0) {
        ++slot;
    }
    s_.check(slot < kSlotCount, kMsgOutOfSlots);
    set_word_at(m_, self + kSlots + 4 * slot, buffer);
    const std::string text = m_.cstring(name, 0x400);
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m_.write(self + kNames + kNameStride * slot, bytes);
}

// 0x004a9510: is a buffer registered under this name? (the scan stops at the first free slot)
bool PreManager::find(std::uint32_t self, std::uint32_t name) {
    const std::string wanted = m_.cstring(name, 0x400);
    for (std::uint32_t i = 0; i < kSlotCount; ++i) {
        if (word_at(m_, self + kSlots + 4 * i) == 0) {
            return false;
        }
        std::string stored;
        for (std::uint32_t k = 0;; ++k) {
            const char c = static_cast<char>(byte_at(m_, self + kNames + kNameStride * i + k));
            if (c == 0 || k > 0x400) {
                break;
            }
            stored.push_back(c);
        }
        if (stored == wanted) {
            return true;
        }
    }
    return false;
}

// 0x004a9580: print the manager and whether any slot is in use
void PreManager::dump(std::uint32_t self, std::uint32_t label) {
    s_.note(kLog, {kDumpBanner});
    s_.note(kLog, {kDumpChecking});
    (void)label;
    s_.note(kLog, {kDumpAddress});
    std::uint32_t slot = 0;
    while (slot < kSlotCount && word_at(m_, self + kSlots + 4 * slot) == 0) {
        ++slot;
    }
    if (slot < kSlotCount) {
        s_.note(kLog, {kDumpNotEmpty});
    }
    s_.note(kLog, {kDumpFooter});
}

// 0x004a95f0
void PreManager::dump_global(std::uint32_t label) { dump(global_word(m_, kGlobalManager), label); }

}  // namespace opentony::runtime::pre
