// Replays a retail trick-tip trace (Phase 4.7b) through the native tip code:
//
//   opentony_trick_tips_retail_trace_test FIXTURE.txt
//
// The fixture (scripts/make_trick_tips_fixture.py) holds, for every recorded call, the raw
// 0x114-byte monitor object before and after and everything the call read from outside it. Each
// record is replayed from its own entry image and must end in the recorded exit image (and tip
// masks); each display call must produce the recorded drawing calls in order.
//
// Exit codes: 0 all match, 1 mismatch, 2 usage/format error.
#include "trick_tips.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace opentony::runtime;

namespace {

int failures = 0;

void fail(const std::string& what) {
    ++failures;
    if (failures <= 20) {
        std::cerr << "MISMATCH: " << what << '\n';
    }
}

std::vector<std::string> split(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::string item;
    std::istringstream stream(text);
    while (std::getline(stream, item, sep)) {
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

void apply_diff(RetailImage& image, const std::vector<std::string>& tokens, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i) {
        const auto colon = tokens[i].find(':');
        if (colon == std::string::npos) {
            throw std::runtime_error("bad diff token " + tokens[i]);
        }
        image.set32(4 * std::stoul(tokens[i].substr(0, colon)),
                    static_cast<std::uint32_t>(std::stoull(tokens[i].substr(colon + 1), nullptr, 16)));
    }
}

std::string describe_difference(const RetailImage& native, const RetailImage& retail) {
    for (std::size_t i = 0; i < kTipMonitorSize; i += 4) {
        if (native.u32(i) != retail.u32(i)) {
            std::ostringstream out;
            out << "word +0x" << std::hex << i << " native 0x" << native.u32(i) << " retail 0x" << retail.u32(i);
            return out.str();
        }
    }
    return "identical";
}

class Replayed final : public TipTextMetrics {
public:
    explicit Replayed(std::vector<std::int32_t> values) : values_(std::move(values)) {}
    std::int32_t text_scale(std::uint32_t) override { return next_ < values_.size() ? values_[next_++] : 0; }

private:
    std::vector<std::int32_t> values_;
    std::size_t next_{};
};

TipDrawCall::Kind kind_of(const std::string& name) {
    using Kind = TipDrawCall::Kind;
    if (name == "set_text_justify") return Kind::SetTextJustify;
    if (name == "set_scale") return Kind::SetScale;
    if (name == "get_scale") return Kind::GetScale;
    if (name == "set_rgb") return Kind::SetRgb;
    if (name == "draw_panel_text") return Kind::DrawPanelText;
    throw std::runtime_error("unknown Mess call " + name);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: opentony_trick_tips_retail_trace_test FIXTURE.txt\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "cannot open fixture " << argv[1] << '\n';
        return 2;
    }
    RetailImage prev(kTipMonitorSize);
    TrickTipInputs inputs;
    int monitors = 0, resets = 0, displays = 0, selects = 0, draws = 0, checks = 0;
    std::uint32_t call_number = 0;
    RetailImage entry(kTipMonitorSize);
    TrickTipState state;
    std::string line;
    try {
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            const char kind = line[0];
            const auto tokens = split(line.substr(2), ' ');
            if (kind == 'R') {
                const auto bar = std::find(tokens.begin(), tokens.end(), "|") - tokens.begin();
                RetailImage before = prev;
                apply_diff(before, tokens, 0, static_cast<std::size_t>(bar));
                RetailImage after = before;
                apply_diff(after, tokens, static_cast<std::size_t>(bar) + 1, tokens.size());
                TrickTipState s;
                s.object = before;
                trick_tips_reset(s);
                ++checks;
                if (!(s.object == after)) {
                    fail("reset: " + describe_difference(s.object, after));
                }
                prev = after;
                ++resets;
            } else if (kind == 'M') {
                call_number = static_cast<std::uint32_t>(std::stoul(tokens[0]));
                inputs = TrickTipInputs{};
                inputs.gate_a = tokens[1] != "0";
                inputs.video_playing = tokens[2] != "0";
                inputs.replay_restart = static_cast<std::uint8_t>(std::stoul(tokens[3]));
                inputs.mode = static_cast<std::int32_t>(std::stol(tokens[4]));
                inputs.level_state = static_cast<std::int32_t>(std::stol(tokens[5]));
                inputs.flag_b098 = static_cast<std::uint8_t>(std::stoul(tokens[6]));
                inputs.flag_b204 = static_cast<std::uint8_t>(std::stoul(tokens[7]));
                inputs.enabled = static_cast<std::uint32_t>(std::stoul(tokens[8]));
                inputs.level = static_cast<std::int32_t>(std::stol(tokens[9]));
                inputs.player = static_cast<std::uint32_t>(std::stoul(tokens[10]));
                inputs.level_masks[0] = static_cast<std::uint32_t>(std::stoul(tokens[11], nullptr, 16));
                inputs.level_masks[1] = static_cast<std::uint32_t>(std::stoul(tokens[12], nullptr, 16));
            } else if (kind == 'S') {
                for (std::size_t i = 0; i < kTipCount; ++i) {
                    inputs.tip_states[i] = static_cast<std::uint32_t>(std::stoul(tokens.at(i)));
                }
            } else if (kind == 'K') {
                for (std::size_t i = 0; i < 9; ++i) {
                    inputs.counters[i] = static_cast<std::int32_t>(std::stol(tokens.at(i)));
                }
            } else if (kind == 'I') {
                entry = prev;
                std::size_t diff_end = tokens.size();
                while (diff_end > 0 && tokens[diff_end - 1].find(':') == std::string::npos) {
                    --diff_end;
                }
                apply_diff(entry, tokens, 0, diff_end);
                state.object = entry;
                for (std::size_t m = 0; m < 4; ++m) {
                    state.masks[m] = static_cast<std::uint32_t>(std::stoul(tokens.at(diff_end + m), nullptr, 16));
                }
                prev = entry;
            } else if (kind == 'O') {
                RetailImage expected = entry;
                std::size_t diff_end = tokens.size();
                while (diff_end > 0 && tokens[diff_end - 1].find(':') == std::string::npos) {
                    --diff_end;
                }
                apply_diff(expected, tokens, 0, diff_end);
                std::array<std::uint32_t, 4> masks{};
                for (std::size_t m = 0; m < 4; ++m) {
                    masks[m] = static_cast<std::uint32_t>(std::stoul(tokens.at(diff_end + m), nullptr, 16));
                }
                trick_tips_monitor(state, inputs);
                ++checks;
                if (!(state.object == expected)) {
                    fail("monitor call " + std::to_string(call_number) + ": " +
                         describe_difference(state.object, expected));
                }
                if (state.masks != masks) {
                    fail("monitor call " + std::to_string(call_number) + ": tip masks differ");
                }
                if (state.object.u32(tip_field::kTimer) == static_cast<std::uint32_t>(kTipDisplayFrames) &&
                    entry.u32(tip_field::kTimer) != static_cast<std::uint32_t>(kTipDisplayFrames)) {
                    ++selects;
                }
                prev = expected;
                ++monitors;
            } else if (kind == 'D') {
                std::vector<std::string> bars;
                std::string rest = line.substr(2);
                std::size_t at = 0;
                for (int part = 0; part < 3; ++part) {
                    const auto next = rest.find(" | ", at);
                    bars.push_back(rest.substr(at, next == std::string::npos ? std::string::npos : next - at));
                    if (next == std::string::npos) {
                        break;
                    }
                    at = next + 3;
                }
                RetailImage before = prev;
                apply_diff(before, split(bars.at(0), ' '), 0, split(bars.at(0), ' ').size());
                RetailImage after = before;
                const auto exit_tokens = bars.at(1).empty() ? std::vector<std::string>{} : split(bars.at(1), ' ');
                apply_diff(after, exit_tokens, 0, exit_tokens.size());
                std::vector<TipDrawCall> expected;
                std::vector<std::int32_t> scales;
                for (const auto& token : split(bars.at(2), ' ')) {
                    const auto parts = split(token, ':');
                    TipDrawCall c;
                    c.kind = kind_of(parts.at(0));
                    for (std::size_t a = 0; a < 4; ++a) {
                        c.args[a] = static_cast<std::uint32_t>(std::stoull(parts.at(1 + a)));
                    }
                    expected.push_back(c);
                    if (c.kind == TipDrawCall::Kind::GetScale) {
                        scales.push_back(static_cast<std::int32_t>(static_cast<std::uint32_t>(std::stoull(parts.at(5)))));
                    }
                }
                TrickTipState s;
                s.object = before;
                Replayed metrics(scales);
                const auto calls = trick_tips_display(s, metrics);
                ++checks;
                if (calls != expected) {
                    fail("display: " + std::to_string(calls.size()) + " native calls vs " +
                         std::to_string(expected.size()) + " retail (or an argument differs)");
                }
                if (!(s.object == after)) {
                    fail("display image: " + describe_difference(s.object, after));
                }
                draws += static_cast<int>(expected.size());
                prev = after;
                ++displays;
            } else {
                std::cerr << "unknown record '" << kind << "'\n";
                return 2;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "fixture error: " << e.what() << " in: " << line.substr(0, 80) << '\n';
        return 2;
    }
    std::cout << "monitors=" << monitors << " resets=" << resets << " displays=" << displays
              << " selected=" << selects << " draw_calls=" << draws << " checks=" << checks
              << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
