#include "auto_views.hpp"
#include "level_gen.hpp"

namespace opentony::runtime::levelgen {

using namespace opentony::runtime::autoviews;

namespace {
constexpr std::uint32_t kModels = 0x18;
constexpr std::uint32_t kModelBytes = 0x24;
constexpr std::uint32_t kClassCounter = 0x0056a920;      // CClass_OpNew's running total of the bytes it handed out
constexpr std::uint32_t kClock = 0x0056e31c;
constexpr std::uint32_t kMsgLeaving = 0x0052ae60;
constexpr std::uint32_t kNumNodes = 0x0056e214;
constexpr std::uint32_t kOffsetList = 0x0056e210;
constexpr std::uint32_t kTrigFile = 0x0056e20c;
constexpr std::uint32_t kNumCheatRestarts = 0x0056e1b4;
constexpr std::uint32_t kNumHorseRestarts = 0x0056e208;
constexpr std::uint32_t kRestartNode = 0x005477ec;
constexpr std::uint32_t kNodeColour = 0x0056114c;
constexpr std::uint32_t kFillerA = 0x005614a4;
constexpr std::uint32_t kFillerB = 0x00561158;
constexpr std::uint32_t kGapNames = 0x005283d0;          // per item: 0x2c bytes (a 16-bit colour at +0, a 16-bit id at +6, the name at +8)
constexpr std::uint32_t kGapTypes = 0x005286bc;

// the node classes of the trigger file: the constructor (a service), the class's vtable and the size of the object
constexpr std::uint32_t kCtorPosition = 0x004c8870;      // a start or horse position: 7 arguments
constexpr std::uint32_t kVtPosition = 0x005198a0;
constexpr std::uint32_t kCtorRail = 0x004c8a20;          // a rail point
constexpr std::uint32_t kVtRail = 0x005198ac;
constexpr std::uint32_t kCtorEnd = 0x004c8bd0;           // the closing node
constexpr std::uint32_t kVtEnd = 0x005198b8;
constexpr std::uint32_t kCtorZone = 0x004c8c30;          // a model (zone) node
constexpr std::uint32_t kVtZone = 0x005198c4;
constexpr std::uint32_t kCtorGap = 0x004c8cd0;           // a gap
constexpr std::uint32_t kVtGap = 0x005198d0;
constexpr std::uint32_t kCtorStart = 0x004c8da0;         // a start marker
constexpr std::uint32_t kVtStart = 0x005198dc;
constexpr std::uint32_t kLinkNode = 0x004c8a80;          // the rail point's link (next, zone)
}  // namespace

// 0x00466b70: CClass_OpNew: a zeroed block from the allocator, counted.
std::uint32_t LevelGen::op_new(std::uint32_t size) {
    const std::uint32_t block = alloc(size);
    const std::uint32_t words = (size + 3) & 0xfffffffc;
    if ((words >> 2) != 0) {
        m_.fill(block, 0, words);
    }
    set_global_word(m_, kClassCounter, global_word(m_, kClassCounter) + words + 4);
    return block;
}

// a virtual call on an engine object: the slot of its class's vtable is the service (the object comes first).
std::uint32_t LevelGen::virtual_call(std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args) {
    const std::uint32_t target = word_at(m_, word_at(m_, object) + 4 * slot);
    args.insert(args.begin(), object);
    s_.note(pspool::service_name(target), std::move(args));
    return static_cast<std::uint32_t>(s_.value(pspool::service_name(target)));
}

// 0x0043e7c0: the outlines of every piece's rail polygons (the in-model's list, moved by the piece's offset and turn) are laid together: the polygons of all the pieces are
// collected in one list and merged into one set of rails, kept at +0x23b8.
void LevelGen::create_rails(std::uint32_t self) {
    const std::uint32_t block_a = g_.new_zeroed(0xc);
    const std::uint32_t list_a = block_a != 0 ? g_.plist_init(block_a) : 0;
    const std::uint32_t block_b = g_.new_zeroed(0xc);
    const std::uint32_t list_b = block_b != 0 ? g_.plist_init(block_b) : 0;
    rails_list_ = list_b;
    g_.plist_free_all(self + 0x23b8);
    std::uint32_t index = 0;
    for (std::uint32_t piece = word_at(m_, self + 0x2344); piece != 0; piece = word_at(m_, piece + 0x230)) {
        for (std::uint32_t poly = word_at(m_, word_at(m_, word_at(m_, piece + 0x224) + 0x20)); poly != 0; poly = word_at(m_, poly + 0x10)) {
            const std::uint32_t block = g_.new_zeroed(0x1c);
            const std::uint32_t copy = block != 0 ? g_.poly_init(block) : 0;
            g_.poly_copy_transformed(copy, poly, static_cast<std::uint32_t>(short_at(m_, piece + 0x21c)), static_cast<std::uint32_t>(short_at(m_, piece + 0x21e)),
                                     static_cast<std::uint32_t>(short_at(m_, piece + 0x220)), byte_at(m_, piece + 0xc), index);
            g_.plist_append(list_a, copy);
        }
        ++index;
    }
    set_word_at(m_, list_b + 8, global_word(m_, kClock));
    if (word_at(m_, list_a) != 0) {
        g_.plist_merge(list_b, list_a, 0);
        set_word_at(m_, list_a, word_at(m_, list_b));
        set_word_at(m_, list_b, 0);
        g_.plist_merge(list_b, list_a, 1);
    }
    set_word_at(m_, self + 0x23b8, word_at(m_, list_b));
    set_word_at(m_, list_b, 0);
    if (list_b != 0) {
        g_.plist_free_all_thunk(list_b);
        g_.delete_block(list_b);
    }
    set_word_at(m_, list_a, 0);
    if (list_a != 0) {
        g_.plist_free_all_thunk(list_a);
        g_.delete_block(list_a);
    }
    log(kMsgLeaving);
}

// 0x0043e980: the trigger file of the park: a chain of node objects (the start positions, the horse points, the rail points of the outlines, a zone node per piece and gap
// and a closing node) is made, asked its size, laid out in one block with a table of offsets (the engine's NumNodes, OffsetList and TrigFile) and freed again. Each node class
// is an engine object: its constructor, and the size and write methods of its vtable, are services.
void LevelGen::build_nodes(std::uint32_t self) {
    const auto make = [&](std::uint32_t size, std::uint32_t ctor, std::uint32_t vtable, std::vector<std::uint32_t> args) -> std::uint32_t {
        const std::uint32_t object = op_new(size);
        if (object == 0) {
            return 0;
        }
        args.insert(args.begin(), object);
        s_.note(pspool::service_name(ctor), std::move(args));
        set_word_at(m_, object, vtable);
        return object;
    };
    g_.model_info_free_all();
    for (std::uint32_t i = 0; i < word_at(m_, self + 0x2340); ++i) {
        g_.in_destruct(self + kModels + i * kModelBytes);
    }
    bool any_horse = false;
    for (std::uint32_t i = 0; i < 6; ++i) {
        if (byte_at(m_, self + 0x36c4 + 0x14 * i) != 0) {
            any_horse = true;
        }
    }
    const bool extra = byte_at(m_, self + 0x3744) != 0 && global_word(m_, kLevelState) == 7 && any_horse && byte_at(m_, self + 0x369c) == 0;
    set_global_word(m_, kNodeColour, word_at(m_, 0x0052a4a8 + global_word(m_, kTheme) * 16));
    std::uint32_t previous = 0;
    for (std::uint32_t e = extra ? 1 : 0; e < 2; ++e) {
        const std::uint32_t record = self + 0x369c + 0x14 * e;
        std::uint32_t node;
        if (byte_at(m_, record) == 0) {
            std::uint32_t layer_top = 5;
            const std::int32_t z = global_int(m_, kGridD) >> 1;
            const std::int32_t x = (global_int(m_, kGridW) >> 1) + static_cast<std::int32_t>(e) * 2;
            std::uint32_t at = word_at(m_, word_at(m_, self + 0x2348) + 4 * static_cast<std::uint32_t>(z)) + 0x10 + static_cast<std::uint32_t>(x) * 0x14;
            for (std::int32_t k = 4; k >= 0; --k, at -= 4) {
                const std::uint32_t piece = word_at(m_, at);
                if (piece != 0) {
                    const std::uint32_t type = byte_at(m_, word_at(m_, piece + 0x224) + 8);
                    if (type == global_word(m_, kFillerA) || type == global_word(m_, kFillerB)) {
                        layer_top = static_cast<std::uint32_t>(sbyte_at(m_, piece + 0xb) + 1 + static_cast<std::int32_t>(byte_at(m_, piece + 6)));
                    } else {
                        layer_top = byte_at(m_, piece + 6) + 5u;
                    }
                    break;
                }
            }
            node = make(0x2c, kCtorPosition, kVtPosition, {static_cast<std::uint32_t>(x), layer_top, static_cast<std::uint32_t>(z), 1, e, 0xffffffff, global_word(m_, kNodeColour)});
        } else {
            node = make(0x2c, kCtorPosition, kVtPosition,
                        {word_at(m_, record + 4), word_at(m_, record + 0xc), word_at(m_, record + 8), word_at(m_, record + 0x10), e, 0xffffffff, global_word(m_, kNodeColour)});
        }
        if (previous == 0) {
            set_word_at(m_, self + 0x23c4, node);
        } else {
            set_word_at(m_, previous + 4, node);
        }
        previous = node;
    }
    s_.check(word_at(m_, self + 0x23c4) != 0, kMsgNodeList);
    std::uint32_t index = 2;
    for (std::uint32_t k = 0; k < 6; ++k) {
        const std::uint32_t record = self + 0x36c4 + 0x14 * k;
        if (byte_at(m_, record) != 0) {
            const std::uint32_t node = make(0x2c, kCtorPosition, kVtPosition, {word_at(m_, record + 4), word_at(m_, record + 0xc), word_at(m_, record + 8), word_at(m_, record + 0x10), 0, k, global_word(m_, kNodeColour)});
            s_.check(previous != 0, kMsgNoPrevNode);
            set_word_at(m_, previous + 4, node);
            previous = node;
            ++index;
        }
    }
    // the rail points of the outlines
    std::uint32_t first_rail = 0;
    std::uint32_t counter = index;
    for (std::uint32_t poly = word_at(m_, self + 0x23b8); poly != 0; poly = word_at(m_, poly + 0x10)) {
        std::uint32_t point = word_at(m_, poly + 0xc);
        std::uint32_t node = previous;
        for (std::int32_t i = 0; i < int_at(m_, poly + 0x18); ++i) {
            node = make(0x20, kCtorRail, kVtRail, {point});
            if (first_rail == 0) {
                first_rail = node;
            }
            set_word_at(m_, previous + 4, node);
            point = word_at(m_, point + 8);
            ++counter;
            previous = node;
        }
    }
    // the zone nodes the outlines name, linked to their rail points
    std::uint32_t zone_index = counter;
    std::uint32_t rail_node = first_rail;
    std::uint32_t base = index;
    std::uint32_t running = index;
    for (std::uint32_t poly = word_at(m_, self + 0x23b8); poly != 0; poly = word_at(m_, poly + 0x10)) {
        std::uint32_t point = word_at(m_, poly + 0xc);
        std::int32_t last_key = -1;
        running = base;
        for (std::int32_t j = 0; j < int_at(m_, poly + 0x18); ++j) {
            s_.check(rail_node != 0, kMsgNoRailNode);
            const std::int32_t key = int_at(m_, point + 0xc);
            std::uint32_t zone = 0xffffffff;
            if (key != last_key) {
                const std::uint32_t node = make(0x14, kCtorZone, kVtZone, {static_cast<std::uint32_t>(key) + 1});
                set_word_at(m_, previous + 4, node);
                zone = zone_index;
                previous = node;
                last_key = key;
                ++zone_index;
            }
            if (j < int_at(m_, poly + 0x18) - 1 || byte_at(m_, poly + 0x14) == 0) {
                engine(kLinkNode, {rail_node, word_at(m_, point + 8) == 0 ? 0xffffffff : running + 1, zone});
            } else {
                s_.check(word_at(m_, point + 8) != 0, kMsgRailLoop);
                engine(kLinkNode, {rail_node, base, zone});
            }
            ++running;
            rail_node = word_at(m_, rail_node + 4);
            point = word_at(m_, point + 8);
        }
        base = running;
    }
    std::uint32_t piece_index = 1;
    for (std::uint32_t piece = word_at(m_, self + 0x2344); piece != 0; piece = word_at(m_, piece + 0x230)) {
        const std::uint32_t node = make(0x14, kCtorZone, kVtZone, {piece_index});
        set_word_at(m_, previous + 4, node);
        previous = node;
        ++piece_index;
    }
    for (std::uint32_t gap = word_at(m_, self + 0x2388); gap != 0; gap = word_at(m_, gap + 0xc)) {
        const std::uint32_t item = word_at(m_, gap + 0x1c);
        const std::uint32_t node = make(0x1c, kCtorGap, kVtGap, {word_at(m_, gap + 0x18), word_at(m_, word_at(m_, gap + 8) + 0x18), item + 0xa8c});
        if (byte_at(m_, gap) != 0) {
            put_string(kGapNames + 8 + item * 0x2c, "[" + m_.cstring(global_word(m_, kItems) + item * 0x2c + 0xd) + "]");
            set_half_at(m_, kGapNames + 6 + item * 0x2c, half_at(m_, kGapTypes + 4 * word_at(m_, global_word(m_, kItems) + item * 0x2c + 0x28)));
            set_half_at(m_, kGapNames + item * 0x2c, byte_at(m_, global_word(m_, kItems) + item * 0x2c + 0xc));
        }
        set_word_at(m_, previous + 4, node);
        previous = node;
    }
    for (std::uint32_t start = word_at(m_, self + 0x23a4); start != 0; start = word_at(m_, start + 8)) {
        const std::uint32_t node = make(0x18, kCtorStart, kVtStart, {0, word_at(m_, start + 0xc)});
        set_word_at(m_, previous + 4, node);
        previous = node;
    }
    {
        const std::uint32_t node = make(0x10, kCtorEnd, kVtEnd, {});
        set_word_at(m_, previous + 4, node);
    }
    // lay the nodes out
    set_global_word(m_, kNumNodes, 0);
    std::uint32_t total = 0;
    for (std::uint32_t node = word_at(m_, self + 0x23c4); node != 0; node = word_at(m_, node + 4)) {
        total += virtual_call(node, 1);
        set_global_word(m_, kNumNodes, global_word(m_, kNumNodes) + 1);
    }
    const std::uint32_t buffer = alloc(total + 0xc + global_word(m_, kNumNodes) * 4, 1, 1, 0);
    set_global_word(m_, kTrigFile, buffer);
    set_word_at(m_, buffer, 0x4752545f);
    set_word_at(m_, buffer + 4, 2);
    set_half_at(m_, buffer + 8, global_half(m_, kNumNodes));
    set_global_word(m_, kOffsetList, buffer + 0xc);
    std::uint32_t write_at = buffer + 0xc + global_word(m_, kNumNodes) * 4;
    std::uint32_t offset = 0;
    for (std::uint32_t node = word_at(m_, self + 0x23c4); node != 0; node = word_at(m_, node + 4)) {
        set_word_at(m_, global_word(m_, kOffsetList) + offset, write_at);
        offset += 4;
        write_at += virtual_call(node, 2, {write_at});
    }
    set_global_word(m_, kNumCheatRestarts, 0);
    set_global_word(m_, kNumHorseRestarts, 0);
    set_global_word(m_, kRestartNode, 0);
    for (std::uint32_t node = word_at(m_, self + 0x23c4); node != 0;) {
        const std::uint32_t next = word_at(m_, node + 4);
        (void)virtual_call(node, 0, {1});
        node = next;
    }
}

}  // namespace opentony::runtime::levelgen
