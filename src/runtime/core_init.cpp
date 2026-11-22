#include "core_views.hpp"
#include "seam_addresses_generated.hpp"
#include "host_options.hpp"

// Phase 4.14c: init.cpp, the game's clean-up, restart and start-up sequences (0x00458270..0x004588b7). Each is a fixed sequence of calls into the rest of the game (the sound, the movie player, the trigger
// scripts, the spooler, the front end, the pad ...), reported by address, with the global state it resets written in place. The PlayStation library stubs the retail code also calls (the 87 functions of
// 0x004e86f0..0x004e8d0f answer 0 or nothing and touch no memory) are not reproduced: they have no effect.

namespace opentony::runtime::core {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kRenderViewportEdges = 0x00563a38;      // pointer to the viewport the render code works on
constexpr Word kDefaultViewport = 0x00533fa8;
constexpr Word kLoadingFlag = 0x0052cb38;
constexpr Word kPcMemBuffer = 0x00562018;              // the 6 MB block the heap lives in
constexpr Word kBitsList = 0x0056db38;                 // BitsResourceListHead: nodes with the next pointer at +4
constexpr Word kSkaterHandles = 0x00568648;            // two handles, 4 bytes each

constexpr Word kLists[7] = {0x0056a960, 0x0055fa38, 0x0055f6bc, 0x0056af40, 0x0055f6b0, 0x0056b830, 0x0056af44};
// (the checks run in a different order from the sweeps: the trigger-script list is checked before the game object list)
constexpr Word kCheckedLists[7] = {0x0056a960, 0x0055fa38, 0x0055f6bc, 0x0055f6b0, 0x0056af40, 0x0056b830, 0x0056af44};
constexpr Word kLeftoverMessages[7] = {0x0052de4c, 0x0052de20, 0x0052ddf8, 0x0052ddc8, 0x0052dd90, 0x0052dd64, 0x0052dd38};

}  // namespace

// 0x004584d0: delete every object of a list through its virtual destructor (slot 0, argument 1) unless its flag byte (+0x6a) has bit 5; the next object is read before the delete
void CoreKit::delete_list(Word head) {
    if (head == 0) {
        return;
    }
    Word object = head;
    Word next = next_object(m_, object);
    for (;;) {
        if ((object_at(m_, object).protect_flags() & 0x20) == 0 && object != 0) {
            virtual_call(object, 0, {1});
        }
        if (next == 0) {
            break;
        }
        object = next;
        next = next_object(m_, object);
    }
}

// 0x00458270: delete the lists of the level's objects and check each head is empty afterwards (a destructor unlinks its object)
void CoreKit::init_kill_all() {
    engine(0x004de000);
    for (const Word list : kLists) {
        delete_list(global_word(m_, list));
    }
    for (int k = 0; k < 7; ++k) {
        check(global_word(m_, kCheckedLists[k]) == 0, kLeftoverMessages[k]);
    }
    engine(0x004bd720);
    engine(0x004aa8a0);
    engine(0x00403760);
    engine(0x00473650);
}

// 0x00458500: the level clean-up. Mode 0 is the hard one (everything goes, the loaded skaters and the sound banks too), 1 drops the environment regions, 2 keeps the trick objects of a restart
void CoreKit::init_cleanup(Word mode) {
    if (host_options().draw_hidden_areas) {
        host_options().area_hidden.clear();   // host side: the objects the area boxes hid (HostOptions::draw_hidden_areas) go with the level
    }
    host_options().poly_overflows = 0;   // host side: the next level's own count and log line (RenderKit::model_allocate_polygon)
    host_options().poly_overflow_logged = false;
    set_global_word(m_, kRenderViewportEdges, kDefaultViewport);
    write_scalar<std::uint16_t>(m_, 0x00533fb6, 0x100);
    engine(0x004ad9b0);
    engine(0x004ad9e0);
    set_global_word(m_, kLoadingFlag, 0xffffffffu);
    engine(0x00454e60);
    engine(0x004abc80);
    init_kill_all();
    engine(0x004c4b30);
    engine(0x00411f00);
    if (mode == 0) {
        log_line(0x0052de90);
        engine(0x004adb90);
        for (Word slot = kSkaterHandles; slot < kSkaterHandles + 8; slot += 4) {
            const Word handle = global_word(m_, slot);
            if (handle != 0) {
                engine(0x00422ec0, {handle});
                set_global_word(m_, slot, 0);
            }
        }
        const Word first = global_word(m_, 0x0056b73c);
        if (first != 0) {
            virtual_call(first, 0, {1});
            if (global_word(m_, 0x0056b710) != 0) {
                virtual_call(global_word(m_, 0x0056b710), 0, {1});
            }
            if (global_word(m_, 0x0056b70c) != 0) {
                virtual_call(global_word(m_, 0x0056b70c), 0, {1});
            }
            set_global_word(m_, 0x0056b73c, 0);
        }
        engine(0x004b32b0);
        for (Word node = global_word(m_, kBitsList); node != 0;) {
            const Word next = global_word(m_, node + 4);
            mem_delete(node);
            node = next;
        }
        set_global_word(m_, kBitsList, 0);
        engine(0x004c5100);
        engine(0x004cbca0);
        engine(0x00492b20);
        engine(0x004ada10);
        engine(0x004ad5b0);
        engine(0x0044c670);
        engine(0x0048a320);
        engine(0x00489420);
    } else if (mode == 1) {
        engine(0x004b34b0);
    }
    initial_rand(0x12a6cc58);
    engine(0x004c4b10);
    engine(0x004497e0);
    if (mode == 2) {
        engine(0x004bd720);
        for (Word object = global_word(m_, globals::kEnvironmentList); object != 0; object = next_object(m_, object)) {
            const RuntimeListObjectView view = object_at(m_, object);
            view.set_visibility_flags(static_cast<std::uint16_t>(view.visibility_flags() & 0xfbff));
        }
    }
    engine(0x004bd700);
    if (mode != 2) {
        engine(0x00466080);
        engine(0x004aa870);
    }
    set_global_word(m_, 0x0056a8d4, 0);
    engine(0x00489610);
    set_global_word(m_, 0x00568684, 0);
    set_global_word(m_, 0x00535af8, 13000);
    set_global_word(m_, 0x00533f90, 0x7fffffffu);
    set_global_word(m_, 0x00533f94, 0x7fffffffu);
}

// 0x00458690: lay the two heap zones out in the 6 MB block (`bytes` for zone 0, the rest for zone 1), start the heap and zero the usage counters
void CoreKit::init_reset_memory(Word bytes) {
    const Word base = global_word(m_, kPcMemBuffer);
    memory_zone(m_, 0).set_begin(base);
    memory_zone(m_, 0).set_end(base + bytes);
    memory_zone(m_, 1).set_begin(base + bytes);
    memory_zone(m_, 1).set_end(base + 0x005dc000);
    engine(0x0046e8c0);
    for (const Word counter : {0x0056a920u, 0x0056e224u, 0x0056b704u, 0x0056af6cu, 0x0056db44u, 0x0056c4dcu, 0x0055f728u}) {
        set_global_word(m_, counter, 0);
    }
}

// 0x004586f0
void CoreKit::init_restart() {
    init_cleanup(0);
    engine(0x004301b0);
    engine(0x004b1ac0);
    set_global_word(m_, 0x0052be4c, 0x14);
    engine(0x004497e0);
    engine(0x00489410);
    engine(0x004acfe0);
    engine(0x0048a0b0, {0x100, 0x50});
}

// 0x00458730: the last call is a jump
void CoreKit::init_for_game() {
    set_global_word(m_, kRenderViewportEdges, kDefaultViewport);
    engine(0x0044be50);
    engine(0x004cba30);
    engine(0x00411ef0);
}

// 0x00458760: allocate and clear the 6 MB heap block, then the start-up calls of every subsystem
void CoreKit::init_at_start() {
    const Word block = engine_value(seam::crt::CRT_operator_new, {0x005dc000});
    set_global_word(m_, kPcMemBuffer, block);
    if (m_.contains(block, 0x005dc000)) {
        m_.fill(block, 0, 0x005dc000);        // (retail `rep stosd`; a replay does not hold the block, which is outside every region)
    }
    engine(0x0047a060);
    set_global_word(m_, kRenderViewportEdges, kDefaultViewport);
    init_reset_memory(0x10);
    engine(0x004894a0);
    engine(0x004301a0);
    engine(0x0040b4e0, {0x9f6ad});
    engine(0x004acfe0);
    engine(0x00448f50);
    engine(0x00454d00);
    engine(0x004645f0);
    engine(0x004aba90);
    engine(0x00417300);
    engine(0x004138f0);
    engine(0x004ac3f0);
}

// 0x00458840
void CoreKit::init_at_end() {
    engine(0x004b5190);
    engine(0x004b3df0);
    set_global_word(m_, 0x005481a8, 0);
    log_line(0x0052deb8);
    const Word block = global_word(m_, kPcMemBuffer);
    if (block != 0) {
        engine(seam::crt::FUN_005002f3, {block});
        set_global_word(m_, kPcMemBuffer, 0);
    }
}

}  // namespace opentony::runtime::core
