#pragma once

#include <cstdint>

namespace opentony::trg {

// The game's pseudo-random generator, Rnd (0x004c9340). It keeps three 32-bit
// words (DAT_0056e2ac, DAT_0056e28c, DAT_0056e290) and returns
// `(low16(a) * range) >> 16`, so the result is in [0, range).
class RetailRandom final {
public:
    struct State {
        std::uint32_t a{};  // DAT_0056e2ac
        std::uint32_t b{};  // DAT_0056e28c
        std::uint32_t c{};  // DAT_0056e290
    };

    RetailRandom() = default;
    explicit RetailRandom(State state) noexcept : state_(state) {}

    void seed(State state) noexcept { state_ = state; }
    [[nodiscard]] const State& state() const noexcept { return state_; }

    [[nodiscard]] std::int32_t next(std::int32_t range) noexcept {
        state_.a = state_.b * state_.a + state_.c;
        state_.b = (state_.b ^ state_.a) + static_cast<std::uint32_t>(static_cast<std::int32_t>(state_.a) >> 4);
        state_.c = state_.c - 0x10101010U + static_cast<std::uint32_t>(static_cast<std::int32_t>(state_.a) >> 3);
        const std::uint32_t product = (state_.a & 0xffffU) * static_cast<std::uint32_t>(range);
        return static_cast<std::int32_t>(product) >> 16;
    }

private:
    State state_{};
};

} // namespace opentony::trg
