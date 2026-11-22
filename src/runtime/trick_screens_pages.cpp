#include "auto_views.hpp"
#include "trick_screens.hpp"

#include <algorithm>

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kMsgEmpty = 0x0055fe10;
constexpr std::uint32_t kMsgOutOfRangeIndex = 0x0054146c;
constexpr std::uint32_t kMsgTrickNotFound = 0x00541254;
constexpr std::uint32_t kMsgNoRecord = 0x00541928;
constexpr std::uint32_t kMsgButtonRange = 0x0054188c;
constexpr std::uint32_t kMsgSpecialNotRemoved = 0x005418dc;
constexpr std::uint32_t kMsgBadTrickIndex = 0x0052531c;
constexpr std::uint32_t kMsgBadItem = 0x00541874;
constexpr std::uint32_t kMsgBadCategory = 0x005418a8;
constexpr std::uint32_t kMsgBadState = 0x005345d8;
constexpr std::uint32_t kMsgPossibleLeak = 0x005265b8;
constexpr std::uint32_t kMsgTrickKeysMoved = 0x005417fc;
constexpr std::uint32_t kMsgNoFont = 0x0052576c;
constexpr std::uint32_t kMsgNoFont2 = 0x00539d70;

// TrickMenu fields (see trick_screens_menu.cpp)
constexpr std::uint32_t kCategoryList = 0x874;
constexpr std::uint32_t kComboList = 0x9f8;
constexpr std::uint32_t kComboRows = 0x9fc;
constexpr std::uint32_t kPageList = 0xb7c;
constexpr std::uint32_t kPageRows = 0xb80;
constexpr std::uint32_t kStyleGlyphs = 0xd08;
constexpr std::uint32_t kStyleTricks = 0xd0c;
constexpr std::uint32_t kStyleDim = 0xd10;
constexpr std::uint32_t kStyleEmpty = 0xd14;
constexpr std::uint32_t kDialog = 0xd18;
constexpr std::uint32_t kPanel = 0xd28;
constexpr std::uint32_t kManager = 0xd44;
constexpr std::uint32_t kFontC = 0x344;
constexpr std::uint32_t kFontF = 0x728;
constexpr std::uint32_t kState = 0x2150;
constexpr std::uint32_t kBlock = 0x2154;

// TrickPanel fields
constexpr std::uint32_t kPanelPoints = 0xac;
constexpr std::uint32_t kPanelPointsLabel = 0xb0;
constexpr std::uint32_t kPanelCount = 0xb4;
constexpr std::uint32_t kPanelCountLabel = 0xb8;
constexpr std::uint32_t kPanelGlyphs = 0xbc;
constexpr std::uint32_t kPanelGlyphsLabel = 0xc0;
constexpr std::uint32_t kPanelTimers = 0xc4;
constexpr std::uint32_t kPanelCounts = 0xd0;

// the manager
constexpr std::uint32_t kRecordSize = 0x28;
constexpr std::uint32_t kManagerCount = 0x1404;

constexpr std::uint32_t kComboTable = 0x00540cb8;
constexpr std::uint32_t kComboTableEnd = 0x00540ec4;
constexpr std::uint32_t kSpecialTable = 0x00540e30;
constexpr std::uint32_t kRowUnlocked = skater::kRowUnlocked;     // a dword: the character is available
constexpr std::uint32_t kRowFlagsOffset = skater::kRowFlags;      // a dword of flags

constexpr std::uint32_t kScratch = 0x7ff00400;


}  // namespace

// ---- helpers --------------------------------------------------------------------------------------------------------------------------------

// The screens' stack buffer for a line of text (a region of its own, outside every region a replay compares).
std::uint32_t TrickScreens::scratch() {
    m_.ensure_region(kScratch, 0x100);
    return kScratch;
}

std::uint32_t TrickScreens::scratch_text(const std::string& text) {
    scratch();
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m_.write(kScratch, bytes);
    return kScratch;
}

std::uint32_t TrickScreens::character_record(std::int32_t character) const {
    return skater::kCareerRecords + u(character) * skater::kCareerRecordStride;
}

// The block the bindings are read from: a character's record, except the first character in the third costume (another block).
std::uint32_t TrickScreens::current_block() {
    if (sk_.current_character() == 0 && sk_.current_costume() == 2) {
        return 0x0056a690;
    }
    return character_record(sk_.current_character()) + 0xcc;
}

// 0x004bd170: the record `index` of the manager (an index outside the records is logged and the address computed anyway).
std::uint32_t TrickScreens::record_at(std::uint32_t manager, std::int32_t index) {
    check(index >= 0 && index < int_at(m_, manager + kManagerCount), kMsgOutOfRangeIndex);
    return manager + 4 + u(index) * kRecordSize;
}

// 0x004bc300: the record whose source offset is `source`, or 0.
std::uint32_t TrickScreens::find_by_source(std::uint32_t manager, std::uint32_t source) {
    const std::int32_t count = int_at(m_, manager + kManagerCount);
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t record = manager + 4 + u(i) * kRecordSize;
        if (half_at(m_, record + 0x20) == (source & 0xffff)) {
            return record;
        }
    }
    return 0;
}

// 0x004bc330: the index of the record whose source offset is `source`; -1 (logged) when there is none.
std::int32_t TrickScreens::index_by_source(std::uint32_t manager, std::uint32_t source) {
    const std::int32_t count = int_at(m_, manager + kManagerCount);
    for (std::int32_t i = 0; i < count; ++i) {
        if (half_at(m_, manager + 4 + u(i) * kRecordSize + 0x20) == (source & 0xffff)) {
            return i;
        }
    }
    check(false, kMsgTrickNotFound);
    return -1;
}

// The manager's records as the 4.7b model reads them (a record's name, source, cost, flags and descriptor).
TrickDatabase TrickScreens::database_view(std::uint32_t manager) {
    TrickDatabase db;
    const std::int32_t count = std::min<std::int32_t>(int_at(m_, manager + kManagerCount), 0x80);
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t record = manager + 4 + u(i) * kRecordSize;
        db.add_or_merge(m_.cstring(record, 0x20), half_at(m_, record + 0x24), half_at(m_, record + 0x22), half_at(m_, record + 0x20), half_at(m_, record + 0x26));
    }
    return db;
}

std::vector<std::uint8_t> TrickScreens::read_block(std::uint32_t block) {
    std::vector<std::uint8_t> bytes(kBindingBlockSize);
    for (std::uint32_t i = 0; i < kBindingBlockSize; ++i) {
        bytes[i] = byte_at(m_, block + i);
    }
    return bytes;
}

void TrickScreens::write_block(std::uint32_t block, const std::vector<std::uint8_t>& bytes) {
    for (std::uint32_t i = 0; i < kBindingBlockSize; ++i) {
        set_byte_at(m_, block + i, bytes[i]);
    }
}

std::uint32_t TrickScreens::pad_edges_value(std::uint32_t third) {
    s_.note(kSvcPadEdges, {global_word(m_, kPadCurrent), 0, third});
    return static_cast<std::uint32_t>(s_.value(kSvcPadEdges));
}

// A row's text in its second column: a toolkit row runs the method itself, any other object (a fake row of a probe) is reported with the text.
void TrickScreens::set_column(std::uint32_t row, const std::string& text, std::int32_t column, std::uint32_t offset, std::uint32_t props) {
    const std::uint32_t vt = word_at(m_, row);
    if (vt == kMenuVtRow) {
        menu_.row_set_column_from(row, text, column, offset, props);
        return;
    }
    s_.note("item_set_column", {row, u(column), offset, props}, text);
}

// ---- the decisions of the 4.7b model, over the manager's and the block's memory ------------------------------------------------------------------------

// 0x004c3350: the page id (6..10) to the class mask of its tricks; an id outside them is logged and gives 0.
std::uint32_t TrickScreens::category_mask(std::int32_t category) {
    const std::uint32_t mask = trick_menu_category_mask(category);
    if (mask == 0) {
        check(false, kMsgBadCategory);
    }
    return mask;
}

// 0x004c1310: the button codes of a combination (a count, then the buttons) as the symbol font's glyph characters, ended by a zero.
void TrickScreens::button_glyphs(std::uint32_t combination, std::uint32_t out) {
    std::vector<std::uint16_t> words;
    const std::uint32_t count = half_at(m_, combination);
    for (std::uint32_t i = 0; i <= count && i < 8; ++i) {
        words.push_back(half_at(m_, combination + 2 * i));
    }
    const std::string glyphs = trick_button_glyphs(words);
    std::vector<std::uint8_t> bytes(glyphs.begin(), glyphs.end());
    bytes.push_back(0);
    m_.write(out, bytes);
}

// 0x004c2800: the record bound to the highlighted combination (the special page: the special slot of that combination), or 0.
std::uint32_t TrickScreens::assigned_record(std::uint32_t self) {
    const std::int32_t combo = static_cast<std::int8_t>(menu_.list_highlighted_id(word_at(m_, self + kComboList)) & 0xff);
    const std::uint32_t mask = category_mask(static_cast<std::int32_t>(menu_.list_highlighted_id(word_at(m_, self + kCategoryList))));
    const TrickDatabase db = database_view(self + kManager);
    const std::vector<std::uint8_t> block = read_block(word_at(m_, self + kBlock));
    const TrickRecord* record = trick_menu_assigned_record(db, block, mask, combo);
    if (record == nullptr) {
        return 0;
    }
    return self + kManager + 4 + u(static_cast<std::int32_t>(record - db.records().data())) * kRecordSize;
}

// 0x004c3cf0: no trick on the highlighted combination and every special slot used.
bool TrickScreens::specials_full(std::uint32_t self) {
    const bool has_trick = assigned_record(self) != 0;
    return trick_menu_specials_full(read_block(word_at(m_, self + kBlock)), has_trick);
}

// 0x004c2890: the page's `count` rows in the order of their tricks' cost (the order of equal costs kept).
void TrickScreens::sort_by_cost(std::uint32_t self, std::int32_t count) {
    std::vector<std::uint16_t> sources;
    std::vector<std::uint32_t> rows;
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t row = word_at(m_, self + kPageRows + 4 * u(i));
        rows.push_back(row);
        sources.push_back(half_at(m_, row + 8));
        check(find_by_source(self + kManager, half_at(m_, row + 8)) != 0, kMsgEmpty);
    }
    const TrickDatabase db = database_view(self + kManager);
    const std::vector<std::size_t> order = trick_menu_sort_by_cost(db, sources);
    for (std::int32_t i = 0; i < count; ++i) {
        set_word_at(m_, self + kPageRows + 4 * u(i), rows[order[static_cast<std::size_t>(i)]]);
    }
}

// The assertions the 4.7b model noted while it assigned a slot, as the assertions retail logs: assigning the empty marker 0xff to a special slot is a bad trick
// index (retail passes 0xff, not -1: see trick-menu-runtime.md), and a special slot that did not clear is "should have been removed". The model's own range
// notes ("button index out of range") are checked by the caller.
void TrickScreens::report_assertions(const TrickDatabase& db, std::size_t from) {
    for (std::size_t n = from; n < db.diagnostics().size(); ++n) {
        const std::string& note = db.diagnostics()[n];
        if (note.rfind("Bad trick index", 0) == 0) {
            check(false, kMsgBadTrickIndex);
        } else if (note == "Special trick should have been removed") {
            check(false, kMsgSpecialNotRemoved);
        }
    }
}

// 0x004c36d0: bind a trick to the highlighted combination. Ordinary pages assign the slot; on the special page a trick may sit on one special combination
// only (already on another: sound 0x92 and message 2 with that combination's buttons) and the five slots can fill (sound 0x92 and message 1).
void TrickScreens::bind_selected(std::uint32_t self, std::int32_t record) {
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t widget = word_at(m_, self + kComboRows + 4 * i);
        if (widget == 0) {
            continue;
        }
        const std::int32_t combo = int_at(m_, widget + 8);
        if (menu_.list_highlighted_id(word_at(m_, self + kComboList)) != u(combo)) {
            continue;
        }
        const std::uint32_t mask = category_mask(static_cast<std::int32_t>(menu_.list_highlighted_id(word_at(m_, self + kCategoryList))));
        check(combo >= 0 && combo < 0x2b, kMsgButtonRange);
        const TrickDatabase db = database_view(self + kManager);
        std::vector<std::uint8_t> block = read_block(word_at(m_, self + kBlock));
        const std::size_t before = db.diagnostics().size();
        const TrickMenuBindResult result = trick_menu_bind(db, block, mask, combo, record);
        write_block(word_at(m_, self + kBlock), block);
        report_assertions(db, before);
        if ((mask & 0x8000) == 0) {
            continue;
        }
        if (result.outcome == TrickMenuBindResult::Outcome::AlreadyOnOtherCombo) {
            check(static_cast<std::uint8_t>(result.other_combo) != 0xff, kMsgEmpty);
            sfx(0x92);
            panel_show_message(word_at(m_, self + kPanel), 2, scratch_text(result.other_glyphs));
        } else if (result.outcome == TrickMenuBindResult::Outcome::SpecialSlotsFull) {
            sfx(0x92);
            panel_show_message(word_at(m_, self + kPanel), 1, 0);
        }
    }
}

// 0x004c38f0: clear the binding of the highlighted combination. On the special page the empty marker is 0xff, which leaves the combination recorded on a
// slot whose trick is empty (4.7b).
void TrickScreens::unbind_selected(std::uint32_t self) {
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t widget = word_at(m_, self + kComboRows + 4 * i);
        if (widget == 0) {
            continue;
        }
        if (menu_.list_highlighted_id(word_at(m_, self + kComboList)) != word_at(m_, widget + 8)) {
            continue;
        }
        const std::uint32_t mask = category_mask(static_cast<std::int32_t>(menu_.list_highlighted_id(word_at(m_, self + kCategoryList))));
        const std::int32_t combo = int_at(m_, widget + 8);
        check(combo >= 0 && combo < 0x2b, kMsgButtonRange);
        const TrickDatabase db = database_view(self + kManager);
        std::vector<std::uint8_t> block = read_block(word_at(m_, self + kBlock));
        const std::size_t before = db.diagnostics().size();
        trick_menu_unbind(db, block, mask, combo);
        write_block(word_at(m_, self + kBlock), block);
        report_assertions(db, before);
    }
}

// 0x004c2700: may this character's list show a trick with these requirement flags? 0x80 and 0x08 need an unlocked character with 0x100 set; 0x80, 0x40 and
// 0x20 each need the unlocked flag of the last character carrying the matching mark (0x100, 0x80, 0x40); 0x10 is not for the first character when it
// has flag 8.
bool TrickScreens::requirements_met(std::int32_t character, std::uint32_t flags) {
    const auto row = [](std::int32_t index) { return skater::kRows + u(index) * skater::kRowStride; };
    if ((flags & 0x80) != 0 && (flags & 8) != 0 && (word_at(m_, row(character) + kRowFlagsOffset) & 0x100) == 0) {
        return false;
    }
    std::int32_t mark_100 = -1;
    std::int32_t mark_80 = -1;
    std::int32_t mark_40 = -1;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(skater::kRowCount); ++i) {
        const std::uint32_t row_flags = word_at(m_, row(i) + kRowFlagsOffset);
        if ((row_flags & 0x100) != 0) {
            mark_100 = i;
        }
        if ((row_flags & 0x80) != 0) {
            mark_80 = i;
        }
        if ((row_flags & 0x40) != 0) {
            mark_40 = i;
        }
    }
    if (mark_100 != -1 && (flags & 0x80) != 0 && word_at(m_, row(mark_100) + kRowUnlocked) == 0) {
        return false;
    }
    if (mark_80 != -1 && (flags & 0x40) != 0 && word_at(m_, row(mark_80) + kRowUnlocked) == 0) {
        return false;
    }
    if (mark_40 != -1 && (flags & 0x20) != 0 && word_at(m_, row(mark_40) + kRowUnlocked) == 0) {
        return false;
    }
    if ((flags & 0x10) != 0 && (word_at(m_, row(0) + kRowFlagsOffset) & 8) != 0) {
        return false;
    }
    return true;
}

// ---- the lists ----------------------------------------------------------------------------------------------------------------------------------

// 0x004c29d0
void TrickScreens::highlight_first(std::uint32_t self) {
    for (std::uint32_t slot : {kCategoryList, kComboList, kPageList}) {
        if (word_at(m_, self + slot) != 0) {
            menu_.list_highlight_first(word_at(m_, self + slot));
        }
    }
}

// 0x004c2000: a page was chosen: drop the old combination list and widgets, build a new scroll list, check the tricks tables still point at the original
// data and fill the list for the page.
void TrickScreens::rebuild_combo_list(std::uint32_t self) {
    if (word_at(m_, self + kComboList) != 0) {
        vcall_list("virtual_delete", word_at(m_, self + kComboList), 0, {1});
    }
    for (std::uint32_t i = 0; i < 0x60; ++i) {
        const std::uint32_t widget = word_at(m_, self + kComboRows + 4 * i);
        if (widget != 0) {
            delete_object(widget);
            set_word_at(m_, self + kComboRows + 4 * i, 0);
        }
    }
    const std::uint32_t memory = operator_new(0x350);
    std::uint32_t list = 0;
    if (memory != 0) {
        list = menu_.scroll_construct(memory, 0x2a, 0x43, 0xe0, 0x7d, 0, 3, 6, 1);
    }
    set_word_at(m_, self + kComboList, list);
    set_word_at(m_, list + 0x1b4, 1);
    check(global_word(m_, 0x00568688) + u(sk_.character_of_slot(0)) * 2 != global_word(m_, 0x00568a50), kMsgTrickKeysMoved);
    check(global_word(m_, 0x00568688) + u(sk_.character_of_slot(1)) * 2 != global_word(m_, 0x00568a4c), kMsgTrickKeysMoved);
    const std::uint32_t mask = category_mask(static_cast<std::int32_t>(menu_.list_highlighted_id(word_at(m_, self + kCategoryList))));
    if ((mask & 0x8000) != 0) {
        build_special_combos(self);
    } else {
        build_normal_combos(self);
    }
}

// The "Done" text of the lists' last row: the format 0x00541830 ("            %s") filled with the global text.
std::string TrickScreens::done_text() {
    std::string format = m_.cstring(0x00541830, 0x20);
    const std::size_t at = format.find("%s");
    if (at != std::string::npos) {
        format.replace(at, 2, m_.cstring(global_word(m_, 0x0052e444), 0x20));
    }
    return format;
}

// 0x004c2a10: the special page: one row per special combination (its buttons in the symbol font), then the blank row.
void TrickScreens::build_special_combos(std::uint32_t self) {
    std::uint32_t at = self + kComboRows;
    std::int32_t count = 0;
    for (std::uint32_t record = kSpecialTable; half_at(m_, record) != 0; record += 12) {
        button_glyphs(record, scratch());
        check(count < 0x60, 0x00541860);
        const std::uint32_t memory = operator_new(0x100);
        std::uint32_t row = 0;
        if (memory != 0) {
            row = menu_.row_construct(memory, u(count), kScratch, word_at(m_, self + kStyleGlyphs), 2, 0, 0);
        }
        set_word_at(m_, at, row);
        vcall_list("list_v6", word_at(m_, self + kComboList), 6, {row});
        ++count;
        at += 4;
    }
    check(count < 0x60, 0x00541860);
    const std::uint32_t done = scratch_text(done_text());
    const std::uint32_t memory = operator_new(0x54);
    std::uint32_t text = 0;
    if (memory != 0) {
        text = menu_.text_construct(memory, 0xffffffff, done, word_at(m_, self + kStyleDim));
    }
    set_word_at(m_, self + kComboRows + 4 * u(count), text);
    vcall_list("list_v6", word_at(m_, self + kComboList), 6, {text});
    check(count != -1 && -1 < count + 1, 0x00541840);
    vcall_list("list_v2", word_at(m_, self + kComboList), 2);
}

// 0x004c2c10: an ordinary page: the combinations of the page's class (manual: two-button ones ending in 10 and marked 0x300, grind: ending in 9 and marked
// 0x300, air: ending in 12 and marked 0x4000, lip: ending in 12 and marked 0x800), one row each (the row's id is the combination's index), then the blank row.
void TrickScreens::build_normal_combos(std::uint32_t self) {
    const std::uint32_t mask = category_mask(static_cast<std::int32_t>(menu_.list_highlighted_id(word_at(m_, self + kCategoryList))));
    std::int32_t count = 0;
    std::int32_t index = 0;
    for (std::uint32_t record = kComboTable; record + 8 < kComboTableEnd && half_at(m_, record) != 0; record += 12, ++index) {
        const std::uint16_t last = half_at(m_, record + 2 * half_at(m_, record));
        const std::uint8_t kind = byte_at(m_, record + 9);
        bool take = false;
        if ((mask & 0x200) != 0) {
            take = last == 10 && (kind & 3) != 0;
        } else if ((mask & 0x100) != 0) {
            take = last == 9 && (kind & 3) != 0;
        } else if ((mask & 0x4000) != 0) {
            take = last == 12 && (kind & 0x40) != 0;
        } else if ((mask & 0x800) != 0) {
            take = last == 12 && (kind & 8) != 0;
        }
        if (!take) {
            continue;
        }
        button_glyphs(record, scratch());
        check(count < 0x60, 0x00541860);
        const std::uint32_t memory = operator_new(0x100);
        std::uint32_t row = 0;
        if (memory != 0) {
            row = menu_.row_construct(memory, u(index), kScratch, word_at(m_, self + kStyleGlyphs), 2, 0, 0);
        }
        set_word_at(m_, self + kComboRows + 4 * u(count), row);
        vcall_list("list_v6", word_at(m_, self + kComboList), 6, {row});
        ++count;
    }
    check(count < 0x60, 0x00541860);
    const std::uint32_t done = scratch_text(done_text());
    const std::uint32_t memory = operator_new(0x54);
    std::uint32_t text = 0;
    if (memory != 0) {
        text = menu_.text_construct(memory, 0xffffffff, done, word_at(m_, self + kStyleDim));
    }
    set_word_at(m_, self + kComboRows + 4 * u(count), text);
    vcall_list("list_v6", word_at(m_, self + kComboList), 6, {text});
    check(count != -1 && -1 < count + 1, 0x00541840);
    vcall_list("list_v2", word_at(m_, self + kComboList), 2);
}

}  // namespace opentony::runtime::frontend
