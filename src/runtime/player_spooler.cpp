#include "auto_views.hpp"
#include "player_spooler.hpp"

namespace opentony::runtime::pspool {

using namespace opentony::runtime::autoviews;

namespace {

// the engine entry points the manager reaches (reported by address, as the other ported screens do)
constexpr std::uint32_t kNew = 0x00466b70;             // operator new(size)
constexpr std::uint32_t kDelete = 0x00466bb0;          // operator delete(block)
constexpr std::uint32_t kFileOpenResource = 0x00449030;  // FileIO_OpenResource(name): the file's size (0 or less: missing)
constexpr std::uint32_t kFileLoadResource = 0x00449230;  // FileIO_LoadResource(buffer): start the read
constexpr std::uint32_t kFileSyncResource = 0x00449660;  // FileIO_SyncResource(): wait for it
constexpr std::uint32_t kFileContinue = 0x004494b0;      // FileIO_Continue(): step the read
constexpr std::uint32_t kPsxQueue = 0x004b37a0;          // PSX_QueueResource(name, kind, a, b): the region slot
constexpr std::uint32_t kPsxSpooler = 0x004b39b0;        // PSX_Spooler()
constexpr std::uint32_t kPsxDrain = 0x004b3df0;          // PSX_DrainSpooler()
constexpr std::uint32_t kPsxParse = 0x004b2450;          // PSX_ParseEnvironment(slot)
constexpr std::uint32_t kPsxClearNamed = 0x004b3270;     // PSX_ClearNamedRegion(name)
constexpr const char* kAlloc = "alloc";                  // FileIO_AllocLoadedBuffer(size, heap, 1, 0)
constexpr const char* kMemDelete = "mem_delete";         // Mem_Delete(block)

}  // namespace

// ---- the object -------------------------------------------------------------------------------------------------------------------------

// 0x004b50e0: allocate the manager and construct it (the pointer is kept at 0x0056db4c).
std::uint32_t PlayerSpooler::create() {
    s_.check(global_word(m_, kGlobalManager) == 0, kMsgAlreadyCreated);
    service(kNew, {kManagerSize});
    const std::uint32_t block = service_value(kNew);
    set_global_word(m_, kGlobalManager, block != 0 ? construct(block) : 0);
    return global_word(m_, kGlobalManager);
}

// 0x004b5190: the destructor is the object's first virtual method.
void PlayerSpooler::destroy() {
    const std::uint32_t manager = global_word(m_, kGlobalManager);
    if (manager == 0) {
        return;
    }
    if (word_at(m_, manager) == kVtManager) {
        scalar_delete(manager, 1);
    } else {
        virtual_call(manager, 0, {1});
    }
    set_global_word(m_, kGlobalManager, 0);
}

void PlayerSpooler::virtual_call(std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args) {
    const std::uint32_t target = word_at(m_, word_at(m_, object) + 4 * slot);
    args.insert(args.begin(), object);
    service(target, std::move(args));
}

// 0x004b51b0: the numbers 0..count-1 ordered by their distance round a ring from `start`: start, start + 1, start - 1, start + 2, ...
std::vector<std::int32_t> PlayerSpooler::ring_values(std::int32_t count, std::int32_t start) {
    std::vector<std::int32_t> out;
    std::int32_t down = start;
    std::int32_t up = start + 1;
    if (up >= count) {
        up = 0;
    }
    for (;;) {
        out.push_back(down);
        if (static_cast<std::int32_t>(out.size()) == count) {
            return out;
        }
        out.push_back(up);
        if (static_cast<std::int32_t>(out.size()) == count) {
            return out;
        }
        if (--down < 0) {
            down = count - 1;
        }
        if (++up >= count) {
            up = 0;
        }
    }
}

void PlayerSpooler::ring_order(std::uint32_t out, std::int32_t count, std::int32_t start) {
    const auto values = ring_values(count, start);
    for (std::size_t i = 0; i < values.size(); ++i) {
        set_word_at(m_, out + 4 * static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(values[i]));
    }
}

// 0x004b5200
std::uint32_t PlayerSpooler::construct(std::uint32_t self) {
    set_word_at(m_, self, kVtManager);
    set_word_at(m_, self + kCount, 0);
    set_word_at(m_, self + kCurrent, 0);
    set_word_at(m_, self + kMode, 0);
    set_global_byte(m_, kStepping, 0);
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(kEntryCount); ++i) {
        const std::uint32_t at = entry(self, i);
        set_byte_at(m_, at + kEntryProcessed, 0);
        set_word_at(m_, at + kEntryBuffer, 0);
        set_word_at(m_, at + kEntryHandle, 0xffffffffu);
    }
    return self;
}

// 0x004b5240: the virtual destructor (bit 0 of the flags frees the object)
std::uint32_t PlayerSpooler::scalar_delete(std::uint32_t self, std::uint32_t flags) {
    destruct(self);
    if ((flags & 1) != 0) {
        service(kDelete, {self});
    }
    return self;
}

// 0x004b5260
void PlayerSpooler::destruct(std::uint32_t self) {
    set_word_at(m_, self, kVtManager);
    reset(self);
    set_word_at(m_, self, kVtBase);
}

// 0x004b52b0: finish what is loading, release every entry and start again.
void PlayerSpooler::reset(std::uint32_t self) {
    if (word_at(m_, self + kMode) == 1) {
        process_pending(self);
    }
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(kEntryCount); ++i) {
        release_entry(self, i);
    }
    set_word_at(m_, self + kCount, 0);
    set_word_at(m_, self + kCurrent, 0);
    set_word_at(m_, self + kMode, 0);
    set_global_byte(m_, kStepping, 0);
}

// 0x004b5300: wait for the entry being loaded (a PSX region through its spooler, a file through the file layer) and move on.
void PlayerSpooler::process_pending(std::uint32_t self) {
    if (word_at(m_, self + kMode) == 0) {
        return;
    }
    set_global_byte(m_, kStepping, 0);
    const std::uint32_t at = entry(self, static_cast<std::int32_t>(word_at(m_, self + kCurrent)));
    if (word_at(m_, at + kEntryKind) == 1) {
        service(kPsxDrain);
    } else {
        service(kFileSyncResource);
    }
    advance_entry(self);
    if (word_at(m_, self + kMode) != 0) {
        set_word_at(m_, self + kMode, 2);
    }
}

// 0x004b5350
void PlayerSpooler::finalize(std::uint32_t self) {
    if (word_at(m_, self + kMode) != 2) {
        return;
    }
    set_global_byte(m_, kStepping, 1);
    set_word_at(m_, self + kMode, 1);
    load_queued_item(self);
}

// 0x004b5370: queue a file by name. `force_raw` makes it a raw file whatever its extension; otherwise a `.psx` file is a PSX region (its stored name loses the extension).
std::int32_t PlayerSpooler::queue(std::uint32_t self, std::uint32_t name, std::uint32_t force_raw, std::uint32_t heap, std::uint32_t pad) {
    return queue_text(self, m_.cstring(name, 0x400), force_raw, heap, pad);
}

std::int32_t PlayerSpooler::queue_text(std::uint32_t self, const std::string& name, std::uint32_t force_raw, std::uint32_t heap, std::uint32_t pad) {
    const std::uint32_t count = word_at(m_, self + kCount);
    s_.check(static_cast<std::int32_t>(count) < static_cast<std::int32_t>(kEntryCount), kMsgTooManyItems);
    const std::uint32_t at = entry(self, static_cast<std::int32_t>(count));
    // sprintf(entry name, "%s", name)
    {
        std::vector<std::uint8_t> bytes(name.begin(), name.end());
        bytes.push_back(0);
        m_.write(at + kEntryName, bytes);
    }
    // the extension starts after the first dot (a name without one is scanned past its end: the assertion fires at the terminator)
    std::uint32_t dot = 0;
    while (dot < name.size() ? name[dot] != '.' : true) {
        s_.check(dot < name.size(), kMsgIrregularName);
        ++dot;
        if (dot > name.size() + 0x40) {
            break;      // (retail would run on through memory: not reproduced)
        }
    }
    if (force_raw != 0) {
        set_word_at(m_, at + kEntryKind, 0);
    } else {
        const std::string extension = dot + 1 <= name.size() ? name.substr(dot + 1) : std::string();
        if (extension == "psx" || extension == "PSX") {
            set_word_at(m_, at + kEntryKind, 1);
            set_byte_at(m_, at + kEntryName + dot, 0);
        } else {
            set_word_at(m_, at + kEntryKind, 0);
        }
    }
    set_word_at(m_, at + kEntryHeap, heap);
    s_.check(heap == 0 || heap == 1, kMsgUnrecognizedHeap);
    set_word_at(m_, at + kEntryPad, pad);
    set_word_at(m_, self + kCount, count + 1);
    return static_cast<std::int32_t>(count);
}

// 0x004b5580: start loading the first entry.
void PlayerSpooler::force_finalize(std::uint32_t self) {
    set_word_at(m_, self + kMode, 1);
    set_global_byte(m_, kStepping, 1);
    load_queued_item(self);
}

// 0x004b55a0: every queued entry has been loaded
bool PlayerSpooler::idle(std::uint32_t self) { return word_at(m_, self + kCurrent) == word_at(m_, self + kCount); }

// 0x004b55c0: one step of the loader: let the file layer read, and when the read is done move to the next entry.
void PlayerSpooler::pump(std::uint32_t self) {
    if (word_at(m_, self + kMode) != 1) {
        return;
    }
    set_global_byte(m_, kStepping, 0);
    const std::uint32_t at = entry(self, static_cast<std::int32_t>(word_at(m_, self + kCurrent)));
    service(kFileContinue);
    set_global_word(m_, kLoading, service_value(kFileContinue));       // (the file layer's read progress: the probe scripts what it leaves in the word)
    if (word_at(m_, at + kEntryKind) == 1) {
        service(kPsxSpooler);
    }
    const std::uint32_t loading = global_word(m_, kLoading);
    set_global_byte(m_, kStepping, 1);
    if (loading != 0) {
        return;
    }
    advance_entry(self);
    if (word_at(m_, self + kMode) != 0) {
        load_queued_item(self);
    }
}

// 0x004b5620: start loading the current entry: a PSX region is queued with the PSX spooler, a file is opened, a buffer reserved and the read begun.
void PlayerSpooler::load_queued_item(std::uint32_t self) {
    set_global_byte(m_, kStepping, 0);
    const std::uint32_t at = entry(self, static_cast<std::int32_t>(word_at(m_, self + kCurrent)));
    const std::uint32_t name = at + kEntryName;
    if (word_at(m_, at + kEntryKind) == 1) {
        service(kPsxQueue, {name, 2, 1, 1});
        set_word_at(m_, at + kEntryHandle, service_value(kPsxQueue));
        set_global_byte(m_, kStepping, 1);
        return;
    }
    service(kFileOpenResource, {name});
    std::int32_t size = static_cast<std::int32_t>(service_value(kFileOpenResource));
    s_.check(size > 0, kMsgFileNotFound);
    std::int32_t reserve = size;
    if (word_at(m_, at + kEntryPad) != 0xffffffffu) {
        s_.check(size < static_cast<std::int32_t>(word_at(m_, at + kEntryPad)), kMsgPadTooSmall);
        reserve = static_cast<std::int32_t>(word_at(m_, at + kEntryPad));
    }
    s_.note(kAlloc, {static_cast<std::uint32_t>(reserve), word_at(m_, at + kEntryHeap), 1, 0});
    const std::uint32_t buffer = static_cast<std::uint32_t>(s_.value(kAlloc));
    set_word_at(m_, at + kEntryBuffer, buffer);
    s_.check(buffer != 0, kMsgNotEnoughMemory);
    service(kFileLoadResource, {buffer});
    set_global_byte(m_, kStepping, 1);
}

// 0x004b57d0: the current entry is done.
void PlayerSpooler::advance_entry(std::uint32_t self) {
    const std::uint32_t current = word_at(m_, self + kCurrent);
    set_byte_at(m_, entry(self, static_cast<std::int32_t>(current)) + kEntryProcessed, 1);
    const std::uint32_t next = current + 1;
    set_word_at(m_, self + kCurrent, next);
    if (next == word_at(m_, self + kCount)) {
        set_word_at(m_, self + kMode, 0);
        set_global_byte(m_, kStepping, 0);
    }
}

// 0x004b5810
bool PlayerSpooler::has_resource(std::uint32_t self, std::uint32_t name) { return find_entry(self, name, false) != -1; }

// 0x004b5830: is a file that was queued ready? (asserts it was queued)
bool PlayerSpooler::acquire(std::uint32_t self, std::uint32_t name) {
    const std::int32_t index = find_entry(self, name, true);
    s_.check(index != -1, kMsgNotInQueue);
    return entry_ready(self, index);
}

// 0x004b58a0
bool PlayerSpooler::entry_ready(std::uint32_t self, std::int32_t index) {
    const std::uint32_t at = entry(self, index);
    if (byte_at(m_, at + kEntryProcessed) == 0) {
        return false;
    }
    s_.check(word_at(m_, at + kEntryKind) != 0 || word_at(m_, at + kEntryBuffer) != 0, kMsgNoDataForLoaded);
    return true;
}

// 0x004b5920
std::uint32_t PlayerSpooler::take_buffer_named(std::uint32_t self, std::uint32_t name) {
    const std::int32_t index = find_entry(self, name, true);
    s_.check(index != -1, kMsgNotAvailable);
    return take_buffer(self, index);
}

// 0x004b5990: hand the loaded file's buffer to the caller (the entry forgets it and becomes reusable).
std::uint32_t PlayerSpooler::take_buffer(std::uint32_t self, std::int32_t index) {
    const std::uint32_t at = entry(self, index);
    s_.check(word_at(m_, at + kEntryKind) == 0, kMsgWrongFileType);
    const std::uint32_t buffer = word_at(m_, at + kEntryBuffer);
    set_word_at(m_, at + kEntryBuffer, 0);
    set_byte_at(m_, at + kEntryProcessed, 0);
    return buffer;
}

// 0x004b5a00: drop a processed entry: a file's buffer is freed; a PSX region is parsed once more (to settle the loader), cleared by name and the PSX spooler drained.
void PlayerSpooler::release_entry(std::uint32_t self, std::int32_t index) {
    const std::uint32_t at = entry(self, index);
    if (byte_at(m_, at + kEntryProcessed) == 0) {
        return;
    }
    if (word_at(m_, at + kEntryKind) != 1) {
        s_.check(word_at(m_, at + kEntryBuffer) != 0, kMsgNoBufferToKill);
        s_.note(kMemDelete, {word_at(m_, at + kEntryBuffer)});
        set_word_at(m_, at + kEntryBuffer, 0);
        set_byte_at(m_, at + kEntryProcessed, 0);
        return;
    }
    // (the assertion tests that no buffer is waiting unprocessed: the entry is processed, so it always holds)
    s_.check(!(word_at(m_, at + kEntryBuffer) != 0 && byte_at(m_, at + kEntryProcessed) == 0), kMsgStillLoading);
    service(kPsxParse, {word_at(m_, at + kEntryHandle)});
    service(kPsxClearNamed, {at + kEntryName});
    service(kPsxDrain);
    set_word_at(m_, at + kEntryHandle, 0xffffffffu);
}

// 0x004b5b00: the entry named `name` (an exact match of the stored copy); with `loaded_only` it must also be processed.
std::int32_t PlayerSpooler::find_entry(std::uint32_t self, std::uint32_t name, bool loaded_only) {
    const std::int32_t count = static_cast<std::int32_t>(word_at(m_, self + kCount));
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t at = entry(self, i);
        std::uint32_t a = at + kEntryName;
        std::uint32_t b = name;
        bool equal = true;
        for (;;) {
            const std::uint8_t x = byte_at(m_, a++);
            const std::uint8_t y = byte_at(m_, b++);
            if (x != y) {
                equal = false;
                break;
            }
            if (x == 0) {
                break;
            }
        }
        if (equal && (!loaded_only || byte_at(m_, at + kEntryProcessed) != 0)) {
            return i;
        }
    }
    return -1;
}

}  // namespace opentony::runtime::pspool
