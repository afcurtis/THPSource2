#include "mmu_views.hpp"
#include "seam_addresses_generated.hpp"

// Phase 4.14b: the memory-card manager (mmu.cpp 0x00473d20..0x00476200): the record table of the `*.SAV` files, the save file names and codes, the directory scan, and the save and load of a file.
// The record layout, the vtable and the services are described in mmu_kit.hpp.

namespace opentony::runtime::mmu {

using namespace views;

namespace {

using Word = std::uint32_t;

// the assertion messages (addresses of the format strings)
constexpr Word kMsgIndexRange = 0x005351b4;        // "file index out of range"
constexpr Word kMsgNoName = 0x005351cc;            // "no name to check"
constexpr Word kMsgNoFiles = 0x0053519c;           // "No files"
constexpr Word kMsgNoBuffer = 0x005351a8;          // "No buffer"
constexpr Word kMsgNoFilename = 0x005269a0;        // "No filename"
constexpr Word kMsgTokenBuffer = 0x0055fe10;
constexpr Word kMsgDescriptionBuffer = 0x0053521c;
constexpr Word kMsgBlockPointer = 0x0053520c;
constexpr Word kMsgInvalidSize = 0x005351f8;       // "Invalid file size"
constexpr Word kMsgCardNotReady = 0x0053527c;
constexpr Word kMsgSizeAlign = 0x00535250;         // "File size not a multiple of 8192"
constexpr Word kMsgWrongBlocks = 0x00535234;       // "Wrong number of blocks %d"
constexpr Word kMsgNoReturnStatus = 0x005352ac;
constexpr Word kMsgUnrecognizedResult = 0x0053528c;
constexpr Word kMsgTooManyBuffers = 0x005352c0;
constexpr Word kMsgActionType = 0x00535154;        // "Action type %d must be SAVE_TYPE.."
constexpr Word kMsgHorse = 0x00535120;             // "Horse mode is not supported when.."
constexpr Word kMsgUnsupportedType = 0x00535108;   // "Unsupported file type"
constexpr Word kMsgOverflow = 0x005353c4;          // "File overflows block size"
constexpr Word kMsgNoUserName = 0x005353b0;
constexpr Word kMsgReserve = 0x00535398;           // "Went into reserve space"
constexpr Word kMsgWhatFilename = 0x00535380;
constexpr Word kMsgNoSaveCalled = 0x00535330;
constexpr Word kMsgNoSaveBuffer = 0x00535320;
constexpr Word kMsgSaveAlign = 0x005352fc;
constexpr Word kMsgOffsetAlign = 0x005352d8;
constexpr Word kMsgNoLoadBuffer = 0x00535448;
constexpr Word kMsgLoadAlign = 0x0053541c;
constexpr Word kMsgLoadOffsetAlign = 0x005353f0;
constexpr Word kMsgNotComplete = 0x005353e0;

// the engine services
constexpr Word kStat = seam::crt::CRT_stat;
constexpr Word kFindNoCase = 0x004b82b0;
constexpr Word kParkFileExists = 0x004f60d0;
constexpr Word kCardSetBusy = 0x004e8cd0;
constexpr Word kCardSetDone = 0x004e8cf0;
constexpr Word kQuitComboReset = 0x00452dc0;
constexpr Word kSaveCustomLevel = 0x004f62c0;
constexpr Word kSprintf = seam::crt::CRT_sprintf;
constexpr Word kRand = 0x0040b500;

// the constant tables
constexpr Word kPatternSav = 0x00535274;           // "*.SAV"
constexpr Word kExtensionSav = 0x005351e0;         // ".SAV"
constexpr Word kFormatPair = 0x005351e8;           // "%s%s"
constexpr Word kPrefixThps2 = 0x005351f0;          // "THPS2_"
constexpr Word kPrefixCareer = 0x0052e35c;         // the pointer to "THPS2_CAREER_"
constexpr Word kPrefixReplay = 0x0052e360;
constexpr Word kPrefixPark = 0x0052e358;
constexpr Word kLabelTable = 0x0052e37c;           // pointers to the type names (career, replay, park, other)
constexpr Word kCustomParkName = 0x0053d7b4;       // "Custom park name goes here"
// the three SC header templates of Mmu_SaveFile: (block A, block B, name) per kind of file
constexpr Word kCareerBlocks[3] = {0x005191c4, 0x00519264, 0x00519244};
constexpr Word kReplayBlocks[3] = {0x005190a4, 0x00519144, 0x00519124};
constexpr Word kParkBlocks[3] = {0x00518f84, 0x00519024, 0x00519004};

inline bool multiple_of(Word value, Word modulus) { return static_cast<std::int32_t>(value) % static_cast<std::int32_t>(modulus) == 0; }

}  // namespace

// ---- the C runtime ---------------------------------------------------------------------------------------------------------------------------------------------------------------

// stat(path, buffer): the 0x24-byte record is scripted word by word, then the answer (live, the call fills the record: milestone 9's cold boot scans the
// card directory)
MmuKit::Word MmuKit::crt_stat(Word path, Word buffer) {
    s_.note("crt_stat", {path, buffer}, m_.cstring(path, 0x100));
    for (Word k = 0; k < 9 && !s_.live(); ++k) {
        set_word_at(m_, buffer + 4 * k, static_cast<Word>(s_.value("crt_stat")));
    }
    return static_cast<Word>(s_.value("crt_stat"));
}

MmuKit::Word MmuKit::crt_findfirst(Word pattern, Word entry) {
    s_.note("crt_findfirst", {pattern, entry}, m_.cstring(pattern, 0x100));
    for (Word k = 0; k < 0x118 / 4 && !s_.live(); ++k) {
        set_word_at(m_, entry + 4 * k, static_cast<Word>(s_.value("crt_findfirst")));
    }
    return static_cast<Word>(s_.value("crt_findfirst"));
}

MmuKit::Word MmuKit::crt_findnext(Word handle, Word entry) {
    s_.note("crt_findnext", {handle, entry});
    for (Word k = 0; k < 0x118 / 4 && !s_.live(); ++k) {
        set_word_at(m_, entry + 4 * k, static_cast<Word>(s_.value("crt_findnext")));
    }
    return static_cast<Word>(s_.value("crt_findnext"));
}

void MmuKit::crt_findclose(Word handle) { s_.note("crt_findclose", {handle}); }

// 0x004e8d10 (the memory card state of the PSX library, a 4.14a function): the two out words, then the answer
MmuKit::Word MmuKit::card_get_state(Word which, Word state_out, Word flag_out) {
    const std::string name = pcplatform::engine_name(0x004e8d10);
    s_.note(name, {which, state_out, flag_out});
    if (s_.live()) {
        return static_cast<Word>(s_.value(name));   // the call wrote the two words itself (milestone 9's cold boot runs this live)
    }
    set_word_at(m_, state_out, static_cast<Word>(s_.value(name)));
    set_word_at(m_, flag_out, static_cast<Word>(s_.value(name)));
    return static_cast<Word>(s_.value(name));
}

// the byte-wise comparison the compiler inlines for `strcmp(a, b) == 0` (answers 0 when equal)
MmuKit::Word MmuKit::compare_strings(Word a, Word b) {
    while (true) {
        const Word x = byte_at(m_, a);
        const Word y = byte_at(m_, b);
        if (x != y) {
            return x < y ? 0xffffffffu : 1u;
        }
        if (x == 0) {
            return 0;
        }
        ++a;
        ++b;
    }
}

// ---- the manager's own bookkeeping ----------------------------------------------------------------------------------------------------------------------------------------

MmuKit::Word MmuKit::file_count(Word self) { return static_cast<Word>(card_at(m_, self).file_count()); }                    // 0x004741d0
MmuKit::Word MmuKit::free_blocks(Word self) { return static_cast<Word>(card_at(m_, self).free_card_blocks()); }            // 0x00474d50
MmuKit::Word MmuKit::card_state(Word self) { return static_cast<Word>(card_at(m_, self).card_state()); }                   // 0x00475620 Mmu_GetCardState

void MmuKit::assert_index(Word self, std::int32_t index, Word message) { check(index >= 0 && card_at(m_, self).file_count() > index, message); }

// 0x00473d20: which kinds of save the card layer accepts (every accepted kind has one 8 KB block of header room: it answers 1)
MmuKit::Word MmuKit::validate_action_type(std::int32_t type, std::int32_t mode) {
    if (type != 1) {
        if (type == 2) {
            check(mode == 1, kMsgActionType);
            check(global_word(m_, globals::kLevelMode) != 7, kMsgHorse);
        } else if (type != 3) {
            check(false, kMsgUnsupportedType);
        }
    }
    return 1;
}

// 0x00473e50: the card part of the manager's constructor
void MmuKit::construct_card_state(Word self) {
    const RuntimeMmuSaveManagerView card = card_at(m_, self);
    card.set_vtable(GuestPtr<void>{static_cast<Word>(manager::kVtable)});
    card.set_file_count(0);
    card.set_free_card_blocks(0);
    card.set_card_state(1);
    card.set_save_buffer_count(0);
    refresh_card_state();
    engine(kCardSetDone, {0});
}

// 0x00473ee0: the scalar deleting destructor
MmuKit::Word MmuKit::scalar_delete(Word self, Word flags) {
    reset_vtable(self);
    if ((flags & 1) != 0) {
        s_.note("operator_delete", {self});
    }
    return self;
}

void MmuKit::reset_vtable(Word self) { card_at(m_, self).set_vtable(GuestPtr<void>{static_cast<Word>(manager::kBaseVtable)}); }

// 0x00473f30: the file whose modification time is the latest (-1: no files at all, 0 when none can be examined)
std::int32_t MmuKit::newest_file(Word self) {
    const std::int32_t count = card_at(m_, self).file_count();
    check(0 < count, kMsgNoFiles);
    std::int32_t newest_time = 0;
    std::int32_t newest = 0;
    if (count < 1) {
        return -1;
    }
    for (std::int32_t i = 0; i < count; ++i) {
        const std::int32_t result = static_cast<std::int32_t>(crt_stat(record(self, i), kScratchStat));
        const std::int32_t modified = int_at(m_, kScratchStat + 0x1c);
        if (result == 0 && newest_time < modified) {
            newest_time = modified;
            newest = i;
        }
    }
    return newest;
}

// 0x00474010: the 10-bit counter in the file name (two characters: base 32), read the way retail does (the same character twice)
std::int32_t MmuKit::file_code(Word self, std::int32_t index) {
    const std::string name = str(record(self, index), 0x80);
    const std::int8_t c = static_cast<std::int8_t>(name.size() > 11 ? name[11] : 0);
    std::int32_t code = 0;
    for (int k = 0; k < 2; ++k) {
        code = static_cast<std::int32_t>(static_cast<std::uint32_t>(code) << 5);
        if (c >= 0x30 && c <= 0x39) {
            code += c - 0x30;
        }
        if (c >= 0x41 && c <= 0x56) {
            code += c - 0x37;
        } else {
            code += 10;
        }
    }
    return code;
}

// 0x00474100: the two characters of a 10-bit counter (base 32: 0-9, A-V)
void MmuKit::encode_code(Word buffer, std::int32_t code) {
    check(buffer != 0, kMsgNoBuffer);
    for (std::int32_t k = 1; k >= 0; --k) {
        const std::int32_t digit = code & 0x1f;
        std::uint8_t c;
        if (digit >= 0 && digit <= 9) {
            c = static_cast<std::uint8_t>(digit + 0x30);
        } else if (digit >= 10 && digit < 0x20) {
            c = static_cast<std::uint8_t>(digit + 0x37);
        } else {
            c = 0x41;
        }
        set_byte_at(m_, buffer + static_cast<Word>(k), c);
        code >>= 5;
    }
}


// 0x004741f0: how many files are of one type
MmuKit::Word MmuKit::count_of_type(Word self, std::int32_t type) {
    Word found = 0;
    for (std::int32_t i = 0; i < card_at(m_, self).file_count(); ++i) {
        if (file_type_at(m_, self, static_cast<Word>(i)) == type) {
            ++found;
        }
    }
    return found;
}

// 0x00474250: the record of a file
MmuKit::Word MmuKit::file_record(Word self, std::int32_t index) {
    assert_index(self, index, kMsgIndexRange);
    return record(self, index);
}

// 0x004742e0: the name shown for a file (a custom park shows a fixed line)
MmuKit::Word MmuKit::display_name(Word self, std::int32_t index) {
    if (global_word(m_, globals::kParkBytes) == 0) {
        assert_index(self, index, kMsgIndexRange);
        return display_name_at(m_, self, index);
    }
    return kCustomParkName;
}

// 0x00474380: is a display name already used (a park that exists as a park file counts)
bool MmuKit::name_taken(Word self, Word name) {
    check(name != 0, kMsgNoName);
    if (global_word(m_, globals::kParkBytes) != 0 && (engine_value(kParkFileExists, {name}) & 0xff) != 0) {
        return true;
    }
    for (std::int32_t i = 0; i < card_at(m_, self).file_count(); ++i) {
        if (compare_strings(display_name_at(m_, self, i), name) == 0) {
            return true;
        }
    }
    return false;
}

// 0x004744a0: the same, ignoring one file
bool MmuKit::name_taken_by_other(Word self, Word name, std::int32_t skip) {
    check(name != 0, kMsgNoName);
    if (global_word(m_, globals::kParkBytes) != 0 && (engine_value(kParkFileExists, {name}) & 0xff) != 0) {
        return true;
    }
    for (std::int32_t i = 0; i < card_at(m_, self).file_count(); ++i) {
        if (i != skip && compare_strings(display_name_at(m_, self, i), name) == 0) {
            return true;
        }
    }
    return false;
}

// 0x004745d0: is there a file of this name
bool MmuKit::file_exists(Word self, Word name) {
    check(name != 0, kMsgNoName);
    for (std::int32_t i = 0; i < card_at(m_, self).file_count(); ++i) {
        if (compare_strings(record(self, i), name) == 0) {
            return true;
        }
    }
    return false;
}

// 0x004746d0: the index of the file of this name (-1: none)
std::int32_t MmuKit::find_file(Word self, Word name) {
    check(name != 0, kMsgNoName);
    for (std::int32_t i = 0; i < card_at(m_, self).file_count(); ++i) {
        if (compare_strings(record(self, i), name) == 0) {
            return i;
        }
    }
    return -1;
}

// 0x00474950: the four characters of a file name token: the first and last of the name with two random letters between
void MmuKit::make_file_token(Word, Word name, Word out) {
    check(out != 0, kMsgTokenBuffer);
    check(name != 0, kMsgTokenBuffer);
    const std::string text = str(name);
    set_byte_at(m_, out, byte_at(m_, name));
    s_.note("rand", {26});
    set_byte_at(m_, out + 1, static_cast<Word>(s_.value("rand")) + 'A');
    s_.note("rand", {26});
    set_byte_at(m_, out + 2, static_cast<Word>(s_.value("rand")) + 'A');
    set_byte_at(m_, out + 3, byte_at(m_, name + static_cast<Word>(text.size()) - 1));
    set_byte_at(m_, out + 4, 0);
}

// 0x004747d0: THPS2_ + a token + the type letter + a counter that makes the name new + .SAV
void MmuKit::build_save_filename(Word self, Word out, std::int32_t type, Word name) {
    check(out != 0, kMsgNoFilename);
    make_file_token(self, name, kScratchToken);
    set_string(out, pcplatform::crt::format(str(kFormatPair), {str(kPrefixThps2), str(kScratchToken)}));
    if (type == 1) {
        set_byte_at(m_, out + 10, 'G');
    } else if (type == 2) {
        set_byte_at(m_, out + 10, 'V');
    } else if (type == 3) {
        set_byte_at(m_, out + 10, 'P');
    }
    set_byte_at(m_, out + 11, 0);
    const std::int32_t newest = newest_file(self);
    std::int32_t code = newest == -1 ? 0 : file_code(self, newest);
    do {
        code = (code + 1) & 0x3ff;
        encode_code(out + 11, code);
        set_byte_at(m_, out + 13, 0);
    } while (file_exists(self, out));
    set_string(out + static_cast<Word>(str(out).size()), str(kExtensionSav));
}

// 0x00474a50: the type of a file (1 career, 2 replay, 3 park, 4 other)
MmuKit::Word MmuKit::file_type(Word self, std::int32_t index) {
    assert_index(self, index, kMsgIndexRange);
    return static_cast<Word>(file_type_at(m_, self, static_cast<Word>(index)));
}

// 0x00474ae0: the type's name and the size of a file in 8 KB blocks
void MmuKit::describe_file(Word self, std::int32_t index, Word description, Word blocks_out) {
    check(description != 0, kMsgDescriptionBuffer);
    check(blocks_out != 0, kMsgBlockPointer);
    assert_index(self, index, kMsgIndexRange);
    const RuntimeMmuFileRecordView file = file_at(m_, record(self, index));
    check(multiple_of(file.file_size(), 0x2000), kMsgInvalidSize);
    const std::int32_t size = static_cast<std::int32_t>(file.file_size());
    set_word_at(m_, blocks_out, static_cast<Word>((size + static_cast<std::int32_t>((static_cast<std::uint32_t>(size >> 31)) & 0x1fffu)) >> 13));
    const std::int32_t type = file_type_at(m_, self, static_cast<Word>(index));
    if (type >= 1 && type <= 4) {
        copy_string(word_at(m_, kLabelTable + 4 * static_cast<Word>(type - 1)), description);
    }
}

// 0x00474d70: list the card's files of one kind (0: every kind), keep the records in order, and read each file's header for its display name
std::int32_t MmuKit::scan_save_files(Word self, std::int32_t type) {
    const RuntimeMmuSaveManagerView card = card_at(m_, self);
    check(card.card_state() == 0, kMsgCardNotReady);
    scan_directory(0, kPatternSav, self + manager::kRecords, self + manager::kCount, 0, 0xf);
    std::int32_t total = 0;
    for (std::int32_t i = 0; i < card.file_count(); ++i) {
        const RuntimeMmuFileRecordView file = file_at(m_, record(self, i));
        check((file.file_size() & 0x1fff) == 0, kMsgSizeAlign);
        total += static_cast<std::int32_t>(file.file_size());
    }
    card.set_free_card_blocks(static_cast<std::int32_t>(0xf - static_cast<Word>((total + static_cast<std::int32_t>((static_cast<std::uint32_t>(total >> 31)) & 0x1fffu)) >> 13)));
    std::int32_t kept = 0;
    // the kinds, in the order retail tests them: a file name that starts with the prefix of a kind is of that kind
    auto starts_with = [&](std::int32_t i, Word prefix_table) {
        const Word prefix = word_at(m_, prefix_table);
        const Word name = record(self, i);
        if (engine_value(kFindNoCase, {name, prefix}) == 0) {
            return false;
        }
        const Word found = engine_value(kFindNoCase, {name, prefix});
        return compare_strings(found, name) == 0;
    };
    auto keep = [&](std::int32_t i, Word kind) {
        m_.copy(record(self, kept), record(self, i), 0x94);
        set_file_type_at(m_, self, static_cast<Word>(kept), static_cast<std::int32_t>(kind));
    };
    for (std::int32_t i = 0; i < card.file_count(); ++i) {
        if ((type == 0 || type == 1) && starts_with(i, kPrefixCareer)) {
            keep(i, 1);
            ++kept;
            continue;
        }
        if ((type == 0 || type == 2) && starts_with(i, kPrefixReplay)) {
            m_.copy(record(self, kept), record(self, i), 0x94);
            const std::int32_t size = static_cast<std::int32_t>(file_at(m_, record(self, kept)).file_size());
            const std::int32_t blocks = (size + static_cast<std::int32_t>((static_cast<std::uint32_t>(size >> 31)) & 0x1fffu)) >> 13;
            check(blocks == 3 || blocks == 6, kMsgWrongBlocks);
            set_file_type_at(m_, self, static_cast<Word>(kept), 2);
            ++kept;
            continue;
        }
        if ((type == 0 || type == 3) && starts_with(i, kPrefixPark)) {
            keep(i, 3);
            ++kept;
        }
    }
    card.set_file_count(static_cast<std::int32_t>(static_cast<Word>(kept)));
    for (std::int32_t i = 0; i < kept; ++i) {
        refresh_card_state();
        if (!read_file(0, record(self, i), kScratchHeader, 0, 0x200)) {
            return -1;
        }
        refresh_card_state();
        type_label(kScratchLabel, file_type_at(m_, self, static_cast<Word>(i)));
        const Word length = static_cast<Word>(str(kScratchLabel).size());
        copy_string(kScratchHeader + length + 4, display_name_at(m_, self, i));
    }
    return 0;
}

// 0x00475480: the card library's state, turned into the manager's state word and the "finished" answer
void MmuKit::map_card_result(Word self, Word result_out) {
    const RuntimeMmuSaveManagerView card = card_at(m_, self);
    check(result_out != 0, kMsgNoReturnStatus);
    const std::int32_t answer = static_cast<std::int32_t>(card_get_state(1, kScratchA, kScratchB));
    const std::int32_t state = global_int(m_, kScratchA);
    const Word flag = global_word(m_, kScratchB);
    if (state == 3 || answer == -1) {
        engine(kCardSetBusy, {0});
        return;
    }
    if (answer != 1) {
        return;
    }
    switch (flag) {
    case 0:
        card.set_card_state(0);
        set_word_at(m_, result_out, 0);
        break;
    case 1:
        card.set_card_state(1);
        set_word_at(m_, result_out, 0);
        break;
    case 2:
        card.set_card_state(3);
        set_word_at(m_, result_out, 0);
        break;
    case 3:
        set_global_byte(m_, globals::kCardLoadedFlag, 0);
        set_global_byte(m_, globals::kParkNameFlag, 0);
        engine(kCardSetDone, {0});
        card.set_card_state(4);
        set_word_at(m_, result_out, 1);
        break;
    case 4:
        card.set_card_state(2);
        set_word_at(m_, result_out, 0);
        break;
    default:
        check(false, kMsgUnrecognizedResult);
    }
}

// 0x00475640
bool MmuKit::card_ready(Word) {
    refresh_card_state();
    return card_nothing() == 0;
}

// 0x00475660: delete one file through the backend
bool MmuKit::delete_save_file(Word self, std::int32_t index) {
    assert_index(self, index, kMsgIndexRange);
    refresh_card_state();
    return remove_file(0, record(self, index)) == 0;
}

// 0x00475700: the 0x200-byte header of a save file: "SC", the type, the block count, the description line's room, the user name and the three 128-byte blocks
void MmuKit::build_save_header(Word out, Word blocks, std::int32_t type, Word block_a, Word block_b, Word block_c, Word name) {
    set_byte_at(m_, out, 0x53);
    set_byte_at(m_, out + 1, 0x43);
    set_byte_at(m_, out + 2, static_cast<Word>(type) + 0x10);
    set_byte_at(m_, out + 3, blocks);
    for (Word k = 0; k < 0x40; ++k) {
        set_byte_at(m_, out + 4 + k, 0);
    }
    for (Word k = 0; k < 0x1c; ++k) {
        set_byte_at(m_, out + 0x44 + k, 0);
    }
    for (Word k = 0; k < 0x20; ++k) {
        set_byte_at(m_, out + 0x60 + k, byte_at(m_, name + k));
    }
    for (Word k = 0; k < 0x80; ++k) {
        set_byte_at(m_, out + 0x80 + k, byte_at(m_, block_a + k));
        set_byte_at(m_, out + 0x100 + k, byte_at(m_, (type < 2 ? block_a : block_b) + k));
        set_byte_at(m_, out + 0x180 + k, byte_at(m_, (type < 3 ? block_a : block_c) + k));
    }
}

// 0x00475840: register one buffer (up to four) for the next save or load
void MmuKit::add_save_buffer(Word self, Word buffer, Word size) {
    const RuntimeMmuSaveManagerView card = card_at(m_, self);
    check(card.save_buffer_count() < 4, kMsgTooManyBuffers);
    const Word count = static_cast<Word>(card.save_buffer_count());
    set_save_buffer_pointer_at(m_, self, count, buffer);
    set_save_buffer_size_at(m_, self, count, size);
    card.set_save_buffer_count(static_cast<std::int32_t>(static_cast<Word>(card.save_buffer_count()) + 1));
}

// 0x004758e0: write a file (a new one, or over the one at `index`): the header, then the registered buffers
bool MmuKit::save_file(Word self, Word name, std::int32_t index, std::int32_t type) {
    const RuntimeMmuSaveManagerView card = card_at(m_, self);
    engine(kQuitComboReset);
    if (type == 3) {
        engine(kSaveCustomLevel, {name, 0, global_word(m_, globals::kParkBuffer), global_word(m_, globals::kParkBytes)});
        copy_string(name, globals::kParkName);
        set_global_word(m_, globals::kParkBuffer, 0);
        set_global_word(m_, globals::kParkBytes, 0);
        return true;
    }
    Word file = 0;
    refresh_card_state();
    const Word blocks = validate_action_type(type, 1);
    Word total = 0;
    for (Word k = 0; static_cast<std::int32_t>(k) < card.save_buffer_count(); ++k) {
        total += save_buffer_size_at(m_, self, k);
    }
    check(total + 0x200u <= (blocks << 13), kMsgOverflow);
    const bool is_new = card.file_count() == index;
    if (!is_new) {
        file = record(self, index);
        if (remove_file(0, file) != 0) {
            return false;
        }
    }
    if (!is_new) {
        assert_index(self, index, kMsgIndexRange);
        file = record(self, index);
    } else {
        type_label(kScratchName, type);
        set_string(kScratchName + static_cast<Word>(str(kScratchName).size()), str(name));
        set_string(kScratchName + static_cast<Word>(str(kScratchName).size()), str(kExtensionSav));
        file = kScratchName;
    }
    refresh_card_state();
    if (create_file(0, file) != 0) {
        return false;
    }
    refresh_card_state();
    check(name != 0, kMsgNoUserName);
    type_label(kScratchLabel, type);
    set_string(kScratchDescription, pcplatform::crt::format(str(kFormatPair), {str(kScratchLabel), str(name)}));
    for (Word k = 0; k < 0x200; ++k) {
        set_byte_at(m_, kScratchHeader + k, 0);
    }
    if (type == 1) {
        build_save_header(kScratchHeader, blocks, 2, kCareerBlocks[0], kCareerBlocks[1], 0, kCareerBlocks[2]);
    } else if (type == 2) {
        build_save_header(kScratchHeader, blocks, 2, kReplayBlocks[0], kReplayBlocks[1], 0, kReplayBlocks[2]);
    } else if (type == 3) {
        build_save_header(kScratchHeader, blocks, 2, kParkBlocks[0], kParkBlocks[1], 0, kParkBlocks[2]);
        copy_string(name, globals::kParkName);
    } else {
        check(false, kMsgUnsupportedType);
    }
    copy_string(kScratchDescription, kScratchHeader + 4);
    for (Word k = 0; k < 0x1c; ++k) {
        check(byte_at(m_, kScratchHeader + k + 0x44) == 0, kMsgReserve);
    }
    check(file != 0, kMsgWhatFilename);
    Word offset = 0;
    bool written = write_file(0, file, kScratchHeader, 0, 0x200);
    offset += 0x200;
    if (!written) {
        return false;
    }
    refresh_card_state();
    check(card.save_buffer_count() != 0, kMsgNoSaveCalled);
    Word flag = 0;
    for (Word k = 0; static_cast<std::int32_t>(k) < card.save_buffer_count(); ++k) {
        const Word buffer = save_buffer_pointer_at(m_, self, k);
        const Word size = save_buffer_size_at(m_, self, k);
        check(buffer != 0, kMsgNoSaveBuffer);
        check(multiple_of(size, 128), kMsgSaveAlign);
        check(multiple_of(offset, 128), kMsgOffsetAlign);
        if (!write_file(0, file, buffer, offset, size)) {
            card.set_save_buffer_count(0);
            return false;
        }
        card_get_state(0, kScratchA, kScratchB);
        flag = global_word(m_, kScratchB);
        offset += size;
    }
    if (global_byte(m_, globals::kCardSkipFlag) == 0 && type == 1) {
        set_global_byte(m_, globals::kCardNameFlag, 1);
        set_global_byte(m_, globals::kCardLoadedFlag, 1);
        copy_string(file, globals::kLastCareerFile);
    }
    if (type == 3) {
        set_global_byte(m_, globals::kParkNameFlag, 1);
        copy_string(file, globals::kLastParkFile);
    }
    return flag == 0;
}

// 0x00476200: read the registered buffers from a file, after its header
bool MmuKit::load_file(Word self, std::int32_t index, std::int32_t type) {
    engine(kQuitComboReset);
    assert_index(self, index, kMsgIndexRange);
    refresh_card_state();
    Word offset = 0x200;
    Word flag = 0;
    for (Word k = 0; static_cast<std::int32_t>(k) < card_at(m_, self).save_buffer_count(); ++k) {
        const Word buffer = save_buffer_pointer_at(m_, self, k);
        check(buffer != 0, kMsgNoLoadBuffer);
        const Word size = save_buffer_size_at(m_, self, k);
        check(multiple_of(size, 128), kMsgLoadAlign);
        check(multiple_of(offset, 128), kMsgLoadOffsetAlign);
        if (!read_file(0, record(self, index), buffer, offset, size)) {
            return false;
        }
        copy_string(record(self, index), globals::kLoadedFileName);
        card_get_state(0, kScratchA, kScratchB);
        flag = global_word(m_, kScratchB);
        check(global_int(m_, kScratchA) == 3, kMsgNotComplete);
        offset += size;
    }
    if (global_byte(m_, globals::kCardSkipFlag) == 0 && type == 1) {
        set_global_byte(m_, globals::kCardLoadedFlag, 1);
        copy_string(record(self, index), globals::kLastCareerFile);
    }
    if (type == 3) {
        set_global_byte(m_, globals::kParkNameFlag, 1);
        copy_string(record(self, index), globals::kLastParkFile);
        copy_string(display_name_at(m_, self, index), globals::kParkName);
    }
    return flag == 0;
}

}  // namespace opentony::runtime::mmu
