#include "auto_views.hpp"
#include "create_spool.hpp"
#include "frontend_text.hpp"

#include <string>
#include <vector>

namespace opentony::runtime::create {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t entry_at(std::uint32_t queue, std::uint32_t i) { return queue + kSpoolEntries + i * kSpoolStride; }

}  // namespace

void CreateSpool::compact(std::uint32_t queue) {
    std::uint32_t used = 0;
    for (std::uint32_t i = 0; i < kSpoolCount; ++i) {
        if (word_at(m_, entry_at(queue, i) + kSpoolFree) == 0) {
            m_.copy(entry_at(queue, used), entry_at(queue, i), kSpoolStride);
            ++used;
        }
    }
    for (std::uint32_t i = used; i < kSpoolCount; ++i) {
        set_word_at(m_, entry_at(queue, i) + kSpoolFree, 1);
    }
}

std::int32_t CreateSpool::find_free(std::uint32_t queue) {
    compact(queue);
    for (std::uint32_t i = 0; i < kSpoolCount; ++i) {
        if (word_at(m_, entry_at(queue, i) + kSpoolFree) != 0) {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

void CreateSpool::add(std::uint32_t queue, std::uint32_t key, std::uint32_t name, std::int32_t slot, std::uint32_t a,
                      std::uint32_t b, std::uint32_t c) {
    std::uint32_t entry = 0;
    bool found = false;
    for (std::uint32_t i = 0; i < kSpoolCount && !found; ++i) {
        const std::uint32_t e = entry_at(queue, i);
        if (word_at(m_, e + kSpoolFree) == 0 && word_at(m_, e) == key && int_at(m_, e + kSpoolSlot) == slot) {
            entry = e;
            found = true;
        }
    }
    if (!found) {
        const std::int32_t free_index = find_free(queue);
        if (free_index == -1) {
            s_.check(false, kMsgTooManySpoolerEntries);
            return;
        }
        entry = entry_at(queue, static_cast<std::uint32_t>(free_index));
        set_word_at(m_, entry, key);
        set_word_at(m_, entry + kSpoolSlot, static_cast<std::uint32_t>(slot));
        set_word_at(m_, entry + kSpoolRequestB, a);
        set_word_at(m_, entry + kSpoolRequestA, b);
        set_word_at(m_, entry + kSpoolFree, 0);
        set_word_at(m_, entry + kSpoolRequestC, c);
    }
    std::string text = m_.cstring(name);
    text.push_back('\0');
    m_.write(entry + kSpoolName, std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::uint32_t CreateSpool::next_word(std::uint32_t src, std::uint32_t dst) {
    s_.check(src != 0, kMsgNoSource);
    std::uint8_t c = byte_at(m_, src);
    while (c != 0 && c != ' ' && c != '&') {
        set_byte_at(m_, dst, c);
        ++dst;
        ++src;
        c = byte_at(m_, src);
    }
    if (byte_at(m_, src) == ' ') {
        ++src;
    }
    set_byte_at(m_, dst, 0);
    return src;
}

void CreateSpool::layout(std::uint32_t box, std::uint32_t text) {
    if (s_.live()) {
        // live nothing scripts the widths: each candidate line is measured with the font code, as retail asks Font_TextWidth of the font the box holds at +0x10
        // (the main menu's memory card message is laid out this way)
        frontend::FontTools fonts(m_, s_);
        const std::uint32_t font = word_at(m_, box + 0x10);
        layout(box, text, [&](const std::string& line) { return fonts.text_width_of(font, line); });
        return;
    }
    layout(box, text, {});
}

void CreateSpool::layout(std::uint32_t box, std::uint32_t text, const std::function<std::int32_t(const std::string&)>& measure) {
    set_word_at(m_, box + kTextLineCount, 0);
    for (std::uint32_t i = 0; i < 8; ++i) {
        set_byte_at(m_, box + kTextLines + i * kTextLineStride, 0);
    }
    // retail's word buffer and candidate line are locals; they need no memory of their own here
    std::uint32_t src = text;
    for (;;) {
        // the word: up to a space, a `&` or the end of the message
        std::string word;
        std::uint32_t after = src;
        s_.check(src != 0, kMsgNoSource);
        for (std::uint8_t c = byte_at(m_, after); c != 0 && c != ' ' && c != '&'; c = byte_at(m_, after)) {
            word.push_back(static_cast<char>(c));
            ++after;
        }
        if (byte_at(m_, after) == ' ') {
            ++after;
        }
        const std::int32_t count = int_at(m_, box + kTextLineCount);
        const std::uint32_t line = box + kTextLines + static_cast<std::uint32_t>(count) * kTextLineStride;
        const std::string current = m_.cstring(line);
        const std::string candidate = current + (current.empty() ? "" : " ") + word;
        std::int32_t width;
        if (measure) {
            width = measure(candidate);
        } else {
            s_.note(kTextWidth, {}, candidate);
            width = s_.value(kTextWidth);
        }
        auto store = [&] {
            std::vector<std::uint8_t> bytes(candidate.begin(), candidate.end());
            bytes.push_back(0);
            m_.write(line, bytes);
        };
        if (width < kTextMaxWidth) {
            store();
            src = after;
        } else {
            if (candidate.find(' ') == std::string::npos) {
                store();   // a single word wider than the box: it gets a line of its own, and the layout moves on past it
                src = after;
            }
            s_.check(int_at(m_, box + kTextLineCount) < kTextMaxLines, kMsgTooManyTextLines);
            set_word_at(m_, box + kTextLineCount, word_at(m_, box + kTextLineCount) + 1);
        }
        if (byte_at(m_, src) == '&') {
            s_.check(int_at(m_, box + kTextLineCount) < kTextMaxLines, kMsgTooManyTextLines);
            ++src;
            set_word_at(m_, box + kTextLineCount, word_at(m_, box + kTextLineCount) + 1);
        }
        if (byte_at(m_, src) == 0) {
            set_word_at(m_, box + kTextLineCount, word_at(m_, box + kTextLineCount) + 1);
            return;
        }
    }
}

}  // namespace opentony::runtime::create
