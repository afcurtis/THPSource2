#include "world_views.hpp"

// Phase 4.15c: the trigger file (`trig.cpp`): the loaded `_TRG` image and its table of records, the list of command points and its 256 buckets, the restart points the player is put at, the
// decoding of a record's position and the links of a record. `self`-less: everything lives in globals at their retail addresses.

namespace opentony::runtime::worldkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kStreamInterpreter = 0x004c5dc0;        // TRG_InterpretCommandStream(stream, index, mode)
constexpr Word kNetRingPush = 0x004df250;              // NetRing_Push(id)
constexpr Word kMemShrink = 0x0046f4e0;                // Mem_Shrink(block, size)
constexpr Word kOpenResource = 0x00449030;             // FileIO_OpenResource(name, mode) -> the size of the resource
constexpr Word kLoadResource = 0x00449230;             // FileIO_LoadResource(buffer)
constexpr Word kSyncResource = 0x00449660;             // FileIO_SyncResource()

constexpr Word kFileName = 0x0056dd60;                 // dword: the name TRG_LoadFile was given
constexpr Word kFileSize = 0x0056dcb0;                 // dword: the size the image was shrunk to
constexpr Word kMenuTable = 0x0056dcb8;                // 0x28 dwords
constexpr Word kBuckets = 0x0056dd64;                  // 256 dwords: the first command point of each id & 0xff
constexpr Word kRestartNames = 0x0056e164;             // up to 19 pointers: the names of the restart nodes
constexpr Word kRestartCount = 0x0056e1b4;
constexpr Word kHomeNames = 0x0056e1b8;                // up to 20 pointers: the "Ho_" nodes
constexpr Word kHomeCount = 0x0056e208;
constexpr Word kImage = 0x0056e20c;                    // the loaded file
constexpr Word kRecords = 0x0056e210;                  // the table of record pointers
constexpr Word kRecordCount = 0x0056e214;
constexpr Word kMenuFlag = 0x0056e21c;
constexpr Word kCommandPoints = 0x0056e220;            // head of the list of command points (the next one is at +0x14)
constexpr Word kCommandPointBytes = 0x0056e224;
constexpr Word kDualBuffer = 0x0056e228;
constexpr Word kRestartIndex = 0x005477ec;             // dword: the restart node found (0xffff for none)
constexpr Word kExtension = 0x00547958;                // the file name's extension (a string)
constexpr Word kAutoexecFlag = 0x0055ff94;
constexpr Word kDefaultRestartFlag = 0x005614c4;       // byte: a level without a "Ho_" node still gets its first restart node
constexpr Word kNumberOfPlayers = 0x00533f3c;
constexpr Word kGameMode = 0x00533f38;
constexpr Word kOwner = 0x0056a954;
constexpr Word kPlayerRecord = 0x0056a960;
constexpr Word kPlayerRecordBytes = 0x2cc4;            // the per-owner counter the command points compare with

constexpr Word kImageMagic = 0x4752545f;               // "_TRG"

// the fake locals of the port (the retail code keeps them in its frame)
constexpr Word kLocalVector = kScratch + 0x100;        // an out vector of three dwords
constexpr Word kLocalSecond = kScratch + 0x120;
constexpr Word kLocalScalar = kScratch + 0x140;
constexpr Word kLocalProduct = kScratch + 0x160;
constexpr Word kLocalRotated = kScratch + 0x180;       // FudgeAngles: the out vectors and the inputs
constexpr Word kLocalRotatedIn = kScratch + 0x1a0;
constexpr Word kLocalName = pcplatform::kScratchBase + 0x400;          // the file name TRG_LoadFile builds

// the file names the assertion macro records
const std::string kTrigFile = "H:\\TonyHawk\\Pc2\\trig.cpp";            // 0x00547818
const std::string kCompetitionFile = "H:\\TonyHawk\\Pc2\\competition.h"; // 0x00547e68
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;

}  // namespace

// the assertion location of the retail macro: the line, then the file name (strcpy, the NUL included). Defined here rather than in world_trig_commands.cpp so that the older Kit tests
// that link only the earlier world files still resolve it (TRG_DecodeNodePosition stamps it)
void WorldKit::trig_stamp(Word line, bool competition) {
    set_global_word(m_, kAssertionLine, line);
    set_string(kAssertionFile, competition ? kCompetitionFile : kTrigFile);
}

// 0x004c4b10
void WorldKit::trig_clear_menu() {
    for (Word k = 0; k < 0x28; ++k) {
        set_word_at(m_, kMenuTable + 4 * k, 0);
    }
    set_global_word(m_, kMenuFlag, 0);
}

// 0x004c4b30
void WorldKit::trig_delete_command_points() {
    for (Word k = 0; k < 0x100; ++k) {
        set_word_at(m_, kBuckets + 4 * k, 0);
    }
    Word node = global_word(m_, kCommandPoints);
    while (node != 0) {
        const Word next = word_at(m_, node + 0x14);
        mem_delete(node);
        set_global_word(m_, kCommandPointBytes, global_word(m_, kCommandPointBytes) - 0x1c);
        node = next;
    }
    set_global_word(m_, kCommandPoints, 0);
}

// 0x004c4b80
void WorldKit::trig_reset_collision_flags(Word mask) {
    for (Word node = global_word(m_, kCommandPoints); node != 0; node = word_at(m_, node + 0x14)) {
        set_byte_at(m_, node + 4, byte_at(m_, node + 4) & ~mask);
    }
}

// 0x004c4ba0
void WorldKit::trig_reset_executed_flags(Word mask) {
    for (Word node = global_word(m_, kCommandPoints); node != 0; node = word_at(m_, node + 0x14)) {
        const Word executed = byte_at(m_, node + 5);
        if (((mask & executed) & 0xff) != 0 && ((byte_at(m_, node + 4) & mask) & 0xff) == 0) {
            set_byte_at(m_, node + 5, ~mask & executed);
        }
    }
}

// 0x004c4bd0
Word WorldKit::trig_trigger_command_point(Word id) {
    engine(kNetRingPush, {id});
    for (Word node = word_at(m_, kBuckets + 4 * (id & 0xff)); node != 0; node = word_at(m_, node + 0x10)) {
        if (word_at(m_, node + 0xc) != id) {
            continue;
        }
        const Word bit = (word_at(m_, global_word(m_, kOwner) + kPlayerRecordBytes) + 1) & 0xff;
        const Word done = byte_at(m_, node + 5);
        set_byte_at(m_, node + 4, byte_at(m_, node + 4) | bit);
        if ((bit & done) == 0) {
            engine(kStreamInterpreter, {word_at(m_, node), half_at(m_, node + 0xa), 0});
            set_byte_at(m_, node + 5, byte_at(m_, node + 5) | bit);
            return node;
        }
    }
    return 0;
}

// 0x004c4c50
void WorldKit::trig_find_restart_by_name(Word name) {
    const std::int32_t count = s(global_word(m_, kRecordCount));
    set_global_word(m_, kRestartIndex, 0xffff);
    for (std::int32_t index = 0; index < count; ++index) {
        if (half_at(m_, word_at(m_, global_word(m_, kRecords) + 4u * static_cast<Word>(index))) != 8) {
            continue;
        }
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kLocalVector + 4 * k, 0);
        }
        const Word position = trg_decode_node_position(kLocalVector, static_cast<Word>(index));
        if (core_.name_equal(position + 6, name, 0xffffffff) != 0) {
            set_global_word(m_, kRestartIndex, static_cast<Word>(index));
            return;
        }
    }
    check(false, 0x005477f4);
}

// 0x004c4d10: the two unit vectors of the view angle (a quarter turn off the player's heading), kept as words and as sign-extended dwords for the camera and the physics
void WorldKit::fudge_angles(Word object) {
    const Word heading = (half_at(m_, object + 0x16) - 0x800u) & 0xfffu;
    set_half_at(m_, object + 0x18, 0);
    set_half_at(m_, object + 0x16, heading);
    set_half_at(m_, object + 0x14, 0);
    const std::int32_t angle = static_cast<std::int16_t>(heading);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kLocalRotated + 4 * k, 0);
    }
    set_global_word(m_, kLocalRotatedIn, 0xfffff000);
    set_global_word(m_, kLocalRotatedIn + 4, 0);
    set_global_word(m_, kLocalRotatedIn + 8, 0);
    core_.rotate_y(kLocalRotated, kLocalRotatedIn, angle);
    const Word side_x = global_word(m_, kLocalRotated);
    const Word side_y = global_word(m_, kLocalRotated + 4);
    const Word side_z = global_word(m_, kLocalRotated + 8);
    set_global_word(m_, kLocalRotatedIn, 0);
    set_global_word(m_, kLocalRotatedIn + 4, 0);
    set_global_word(m_, kLocalRotatedIn + 8, 0xfffff000);
    set_half_at(m_, object + 0x2e58, side_x);
    set_word_at(m_, object + 0x3100, u(static_cast<std::int16_t>(side_x)));
    set_half_at(m_, object + 0x2e5e, side_y);
    set_word_at(m_, object + 0x3104, u(static_cast<std::int16_t>(side_y)));
    set_half_at(m_, object + 0x2e64, side_z);
    set_word_at(m_, object + 0x3108, u(static_cast<std::int16_t>(side_z)));
    core_.rotate_y(kLocalRotated, kLocalRotatedIn, static_cast<std::int16_t>(half_at(m_, object + 0x16)));
    const Word front_x = global_word(m_, kLocalRotated);
    const Word front_y = global_word(m_, kLocalRotated + 4);
    const Word front_z = global_word(m_, kLocalRotated + 8);
    set_half_at(m_, object + 0x2e5c, front_x);
    set_half_at(m_, object + 0x2e5a, 0);
    set_word_at(m_, object + 0x30f4, u(static_cast<std::int16_t>(front_x)));
    set_half_at(m_, object + 0x2e62, front_y);
    set_word_at(m_, object + 0x310c, 0);
    set_half_at(m_, object + 0x2e66, 0);
    set_word_at(m_, object + 0x3114, 0);
    set_word_at(m_, object + 0x30f8, u(static_cast<std::int16_t>(front_y)));
    set_half_at(m_, object + 0x2e68, front_z);
    set_word_at(m_, object + 0x30fc, u(static_cast<std::int16_t>(front_z)));
    set_half_at(m_, object + 0x2e60, 0xf000);
    set_word_at(m_, object + 0x3110, 0xfffff000);
}

// 0x004c4e30: puts every skater at the restart node found by name (position, heading, the camera's copy of both), then runs the node's command stream; in a two-player game (other than mode 7)
// the second skater starts a step in front of the first, or at the node named by the second-player marker
void WorldKit::trig_execute_restart() {
    check(global_word(m_, kRestartIndex) != 0xffff, 0x00547894);
    const Word index = global_word(m_, kRestartIndex);
    check(half_at(m_, word_at(m_, global_word(m_, kRecords) + 4 * index)) == 8, 0x0054786c);
    check(global_word(m_, kPlayerRecord) != 0, 0x0054783c);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kLocalVector + 4 * k, 0);
    }
    const Word node = trg_decode_node_position(kLocalVector, index);
    for (Word skater = global_word(m_, kPlayerRecord); skater != 0; skater = word_at(m_, skater + object::kNext)) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, skater + 8 + 4 * k, word_at(m_, kLocalVector + 4 * k));
            set_word_at(m_, skater + 0xbc + 4 * k, word_at(m_, kLocalVector + 4 * k));
        }
        set_word_at(m_, skater + 0x14, word_at(m_, node));
        set_half_at(m_, skater + 0x18, half_at(m_, node + 4));
        fudge_angles(skater);
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, skater + 0x90 + 4 * k, word_at(m_, skater + 8 + 4 * k));
        }
        set_word_at(m_, skater + 0x94, word_at(m_, skater + 0x94) + 0x1e000);
    }
    Word at = node + 6;
    while (byte_at(m_, at) != 0) {
        ++at;
    }
    ++at;
    engine(kStreamInterpreter, {at + (at & 1u), global_word(m_, kRestartIndex), 1});
    if (global_word(m_, kNumberOfPlayers) != 2 || global_word(m_, kGameMode) == 7) {
        return;
    }
    const Word second = global_word(m_, kPlayerRecord);
    set_global_word(m_, kLocalScalar, 0x64);
    core_.vector_add_out(second + 8, second + 8, core_.vector_mul_out(kLocalProduct, second + 0x30f4, kLocalScalar));
    for (std::int32_t record = 0; record < s(global_word(m_, kRecordCount)); ++record) {
        if (half_at(m_, word_at(m_, global_word(m_, kRecords) + 4u * static_cast<Word>(record))) != 8) {
            continue;
        }
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kLocalVector + 4 * k, 0);
        }
        const Word name = trg_decode_node_position(kLocalVector, static_cast<Word>(record));
        if (core_.name_equal(name + 6, 0x00547834, 0xffffffff) == 0) {
            continue;
        }
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kLocalSecond + 4 * k, 0);
        }
        const Word place = trg_decode_node_position(kLocalSecond, static_cast<Word>(record));
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, second + 8 + 4 * k, word_at(m_, kLocalSecond + 4 * k));
            set_word_at(m_, second + 0xbc + 4 * k, word_at(m_, kLocalSecond + 4 * k));
        }
        set_word_at(m_, second + 0x14, word_at(m_, place));
        set_half_at(m_, second + 0x18, half_at(m_, place + 4));
        fudge_angles(second);
    }
}

// 0x004c5100
void WorldKit::trig_delete_file() {
    if (global_word(m_, kImage) != 0) {
        mem_delete(global_word(m_, kImage));
        set_global_word(m_, kImage, 0);
    }
    set_global_word(m_, kRestartCount, 0);
    set_global_word(m_, kHomeCount, 0);
}

// 0x004c5130: loads "<name><extension>", checks the "_TRG" header (version 2) and turns the table of offsets into pointers; the image is shrunk to the end of its terminating record
void WorldKit::trg_load_file(Word name) {
    set_global_word(m_, kFileName, name);
    check(global_word(m_, kImage) == 0, 0x00547960);
    Word at = kLocalName;
    for (Word from = name; byte_at(m_, from) != 0; ++from) {
        set_byte_at(m_, at++, byte_at(m_, from));
    }
    if (global_byte(m_, kExtension) != 0) {
        for (Word from = kExtension; byte_at(m_, from) != 0; ++from) {
            set_byte_at(m_, at++, byte_at(m_, from));
        }
    }
    set_byte_at(m_, at, 0);
    const Word size = engine_value(kOpenResource, {kLocalName}, m_.cstring(kLocalName, 256));     // (the second argument is the caller's ebx: not recorded)
    const Word image = alloc(size, 0, 1, 0);
    set_global_word(m_, kImage, image);
    engine(kLoadResource, {image});
    take_filled(image, size, pcplatform::engine_name(kLoadResource));
    engine(kSyncResource);
    check(word_at(m_, image) == kImageMagic, 0x00547940);
    check((word_at(m_, image + 4) & 0xffff) == 2, 0x0054791c);
    check((word_at(m_, image + 4) & 0xffff0000) == 0, 0x005478f8);
    const Word count = half_at(m_, image + 8);
    set_global_word(m_, kRecordCount, count);
    set_global_word(m_, kRecords, image + 0xc);
    for (Word k = 0; k < count; ++k) {
        set_word_at(m_, image + 0xc + 4 * k, word_at(m_, image + 0xc + 4 * k) + global_word(m_, kImage));
    }
    const Word last = word_at(m_, global_word(m_, kRecords) + 4 * count - 4);
    if (half_at(m_, last) == 0xff) {
        const Word end = ((last - global_word(m_, kImage)) + 5) & ~3u;
        engine(kMemShrink, {global_word(m_, kImage), end});
        set_global_word(m_, kFileSize, end);
        return;
    }
    check(false, 0x005478c8);
}

// 0x004c5380: the image a level without a trigger file gets: one restart node ("ack") and the terminator
void WorldKit::trig_init_default_file() {
    const Word image = alloc(0x32, 1, 1, 0);
    set_global_word(m_, kImage, image);
    set_global_word(m_, kRecordCount, 2);
    set_global_word(m_, kRecords, image + 0xc);
    set_word_at(m_, image, kImageMagic);
    set_word_at(m_, image + 4, 2);
    set_half_at(m_, image + 8, global_half(m_, kRecordCount));
    const Word node = image + 0x14;
    set_word_at(m_, image + 0xc, node);
    set_half_at(m_, node, 8);
    set_half_at(m_, node + 2, 0);
    set_word_at(m_, node + 4, 0);
    set_word_at(m_, node + 8, 0xfffff830);
    set_word_at(m_, node + 0xc, 0);
    set_half_at(m_, node + 0x10, 0xb);
    set_half_at(m_, node + 0x12, 0x16);
    set_half_at(m_, node + 0x14, 0x21);
    set_byte_at(m_, node + 0x16, 0x61);
    set_byte_at(m_, node + 0x17, 0x63);
    set_byte_at(m_, node + 0x18, 0x6b);
    set_byte_at(m_, node + 0x19, 0);
    set_half_at(m_, node + 0x1a, 0xffff);
    set_word_at(m_, image + 0x10, node + 0x1c);
    set_half_at(m_, node + 0x1c, 0xff);
    set_global_word(m_, kRestartCount, 0);
    set_global_word(m_, kHomeCount, 0);
    set_global_word(m_, kRestartIndex, 0);
}

// 0x004c5420 (cdecl, al): does the byte string ending in 0xff hold this flag
Word WorldKit::trg_find_preamble_flag(Word flag, Word stream) {
    for (Word at = stream; byte_at(m_, at) != 0xff; ++at) {
        if (byte_at(m_, at) == (flag & 0xff)) {
            return 1;
        }
    }
    return 0;
}

// 0x004c5440 (cdecl): the address after the 0xff that ends the preamble
Word WorldKit::trg_find_preamble_end(Word stream) {
    Word at = stream;
    while (byte_at(m_, at) != 0xff) {
        ++at;
    }
    return at + 1;
}

// 0x004c5ac0 (cdecl): the command point of a link record
Word WorldKit::trg_find_command_point_link(Word index) {
    if (index == 0xffff) {
        return 0;
    }
    if (half_at(m_, word_at(m_, global_word(m_, kRecords) + 4 * index)) != 6) {
        return 0;
    }
    for (Word node = global_word(m_, kCommandPoints); node != 0; node = word_at(m_, node + 0x14)) {
        if (half_at(m_, node + 0xa) == index) {
            return node;
        }
    }
    return 0;
}

// 0x004c5d70 (cdecl): the address after a NUL-terminated string, rounded up to an even address
Word WorldKit::trg_skip_aligned_cstring(Word string) {
    Word at = string;
    while (byte_at(m_, at) != 0) {
        ++at;
    }
    ++at;
    return at + (at & 1u);
}

// 0x004c5da0
void WorldKit::trig_remove_dual_buffer() {
    if (global_word(m_, kDualBuffer) != 0) {
        mem_delete(global_word(m_, kDualBuffer));
    }
    set_global_word(m_, kDualBuffer, 0);
}

// 0x004c7ea0: the names of the restart nodes (up to 19) and of the "Ho_" home nodes (up to 20)
void WorldKit::trig_scan_restart() {
    Word found = 0;
    set_global_word(m_, kRestartCount, 0);
    for (std::int32_t index = 0; index < s(global_word(m_, kRecordCount)); ++index) {
        if (half_at(m_, word_at(m_, global_word(m_, kRecords) + 4u * static_cast<Word>(index))) == 8) {
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, kLocalVector + 4 * k, 0);
            }
            const Word position = trg_decode_node_position(kLocalVector, static_cast<Word>(index));
            set_word_at(m_, kRestartNames + 4 * found, position + 6);
            ++found;
            set_global_word(m_, kRestartCount, found);
        }
        if (found == 0x13) {
            break;
        }
    }
    check(s(found) < 0x14, 0x00547fb4);
    Word homes = 0;
    set_global_word(m_, kHomeCount, 0);
    for (std::int32_t index = 0; index < s(global_word(m_, kRecordCount)); ++index) {
        if (half_at(m_, word_at(m_, global_word(m_, kRecords) + 4u * static_cast<Word>(index))) != 8) {
            continue;
        }
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kLocalVector + 4 * k, 0);
        }
        const Word name = trg_decode_node_position(kLocalVector, static_cast<Word>(index)) + 6;
        if (byte_at(m_, name) == 0x48 && (byte_at(m_, name + 1) == 0x6f || byte_at(m_, name + 1) == 0x4f) && byte_at(m_, name + 2) == 0x5f) {
            check(s(global_word(m_, kHomeCount)) < 0x14, 0x00547f8c);
            homes = global_word(m_, kHomeCount);
            set_word_at(m_, kHomeNames + 4 * homes, name);
            set_global_word(m_, kHomeCount, homes + 1);
        }
    }
    if (global_word(m_, kHomeCount) == 0 && global_byte(m_, kDefaultRestartFlag) != 0) {
        set_global_word(m_, kHomeCount, 1);
        set_global_word(m_, kHomeNames, global_word(m_, kRestartNames));
    }
}

// 0x004c8050: runs the "autoexec" records: those of type 0xf in a two-player game, otherwise (or when there are none) those of type 4
void WorldKit::trg_execute_autoexec() {
    set_global_word(m_, kAutoexecFlag, 0);
    trig_scan_restart();
    bool ran = false;
    set_global_word(m_, kRestartIndex, 0xffff);
    if (global_word(m_, kNumberOfPlayers) == 2) {
        for (std::int32_t index = 0; index < s(global_word(m_, kRecordCount)); ++index) {
            const Word record = word_at(m_, global_word(m_, kRecords) + 4u * static_cast<Word>(index));
            if (half_at(m_, record) == 0xf) {
                ran = true;
                engine(kStreamInterpreter, {record + 2, static_cast<Word>(index), 1});
            }
        }
    }
    if (!ran) {
        for (std::int32_t index = 0; index < s(global_word(m_, kRecordCount)); ++index) {
            const Word record = word_at(m_, global_word(m_, kRecords) + 4u * static_cast<Word>(index));
            if (half_at(m_, record) == 0x4) {
                ran = true;
                engine(kStreamInterpreter, {record + 2, static_cast<Word>(index), 1});
            }
        }
    }
    check(ran, 0x00547fe0);
}

// 0x004c84d0: a new command point at the head of the list and of its id's bucket
Word WorldKit::trg_create_link_record(Word key, Word index, Word stream) {
    const Word node = alloc(0x18, 0, 1, 0);
    set_word_at(m_, node + 0x14, global_word(m_, kCommandPoints));
    set_global_word(m_, kCommandPointBytes, global_word(m_, kCommandPointBytes) + 0x1c);
    set_global_word(m_, kCommandPoints, node);
    const Word bucket = kBuckets + 4 * (key & 0xff);
    set_word_at(m_, node + 0x10, word_at(m_, bucket));
    set_word_at(m_, bucket, node);
    set_half_at(m_, node + 0xa, index);
    set_byte_at(m_, node + 4, 0);
    set_byte_at(m_, node + 5, 0);
    set_byte_at(m_, node + 7, 0);
    set_byte_at(m_, node + 6, 0);
    set_half_at(m_, node + 8, 0);
    set_word_at(m_, node, stream);
    set_word_at(m_, node + 0xc, key);
    return node;
}

// 0x004c8550 (cdecl): the address of the links of a record (after its fixed part), by the record's type
Word WorldKit::trg_get_node_links(Word index) {
    check(s(index) >= 0 && s(index) < s(global_word(m_, kRecordCount)), 0x00548078);
    const Word record = word_at(m_, global_word(m_, kRecords) + 4 * index);
    const Word type = half_at(m_, record);
    if (type >= 0x3e8 && type <= 0x3e9) {
        return record + 2;
    }
    if (type == 1) {
        return record + 6;
    }
    if (type == 5) {
        return record + 4;
    }
    if (type == 2 || type == 3 || type == 6 || (type >= 8 && type <= 14)) {
        return record + 2;
    }
    check(false, 0x00548048);
    return 0;
}

// 0x004c8650 (cdecl): the position and angles of a record: three dwords <<12 into `out`; answers the address after them
Word WorldKit::trg_decode_node_position(Word out, Word index) {
    trig_stamp(0xafe);   // the assertion location the retail macro leaves (the trace of TRG_ParseNodes, Milestone 10e, records it)
    check(s(index) >= 0 && s(index) < s(global_word(m_, kRecordCount)), 0x005480cc);
    const Word record = word_at(m_, global_word(m_, kRecords) + 4 * index);
    const Word type = half_at(m_, record);
    Word at = 0;
    if (type == 0x1f4 || type == 0x1f5) {
        at = (record + 5) & ~3u;
    } else if (type == 1 || type == 7) {
        at = record + 2 * half_at(m_, record + 6) + 8;
        while (byte_at(m_, at) != 0xff) {
            ++at;
        }
        at = (at + 4) & ~3u;
    } else if (type == 5) {
        at = (record + 2 * half_at(m_, record + 4) + 9) & ~3u;
    } else if (type == 3 || type == 8 || type == 10 || type == 11 || type == 13 || type == 0x3e8 || type == 0x3e9) {
        at = (record + 2 * half_at(m_, record + 2) + 7) & ~3u;
    } else {
        check(false, 0x005480a0);
        return 0;
    }
    set_word_at(m_, out, word_at(m_, at) << 12);
    set_word_at(m_, out + 4, word_at(m_, at + 4) << 12);
    set_word_at(m_, out + 8, word_at(m_, at + 8) << 12);
    return at + 12;
}

// 0x004c8f10 Utils_StartFade (cdecl): the fade's colour/target words
void WorldKit::start_fade(Word first, Word second) {
    set_global_word(m_, 0x0056e314, first);
    set_global_word(m_, 0x0056e310, first);
    set_global_word(m_, 0x0056e318, second);
}

// 0x004c8f30 Utils_StopFade: clears the fade and lets the display know (a jump to 0x0044bde0)
void WorldKit::stop_fade() {
    set_global_word(m_, 0x0056e310, 0);
    engine(0x0044bde0);
}

}  // namespace opentony::runtime::worldkit
