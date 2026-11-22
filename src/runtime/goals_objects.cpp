#include "auto_views.hpp"
#include "goals.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kCamera = 0x005620e0;             // the camera record: +4, +8, +0xc its position, +0x34 its rotation (nine words)
constexpr std::uint32_t kGteRotation = 0x006a3e10;        // the GTE's rotation matrix (nine words)
constexpr std::uint32_t kGteVector = 0x006a3e58;          // and its first input vector (three words)
constexpr std::uint32_t kGteResult = 0x006a3eb8;          // MAC1 .. MAC3
constexpr std::uint32_t kPowerupFlags = 0x0056af28;
constexpr std::uint32_t kPowerupList = 0x0056b830;
constexpr std::uint32_t kModelByte = 0x0056dcad;          // the model variant a medal takes
constexpr std::uint32_t kModelByteTape = 0x0056dcac;

std::string dwords_text(std::initializer_list<std::uint32_t> values) {
    std::string out;
    for (const std::uint32_t v : values) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
        }
    }
    return out;
}

}  // namespace

// 0x0046e550: a medal object at a place in front of the camera. The place goes through the GTE (the camera's rotation times the position), the object is a power-up
// of type 0x664 (gold), 0x665 (silver) or 0x666 (bronze).
std::uint32_t GoalScreens::medal_create(std::int32_t type, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t scale, std::int32_t speed) {
    const std::uint32_t camera = global_word(m_, kCamera);
    for (std::uint32_t k = 0; k < 9; ++k) {
        set_half_at(m_, kGteRotation + 2 * k, half_at(m_, camera + 0x34 + 2 * k));
    }
    set_global_half(m_, kGteVector, u(x));
    set_global_half(m_, kGteVector + 2, u(y));
    set_global_half(m_, kGteVector + 4, u(z));
    engine(0x004e2ef0, {0, 0, 0, 3, 0});                                   // gte_mvmva: MAC1 .. MAC3
    if (!s_.live()) {
        // a replay has them as scripted values; live, the native (MathKit::mvmva, RT x V0) has written them to the register file. Reading the scripted values live
        // stopped the game at the first medal of a competition ("live service value 'engine_004e2ef0' has no call before it")
        for (std::uint32_t k = 0; k < 3; ++k) {
            set_word_at(m_, kGteResult + 4 * k, u(s_.value(engine_name(0x004e2ef0))));
        }
    }
    const std::uint32_t px = (word_at(m_, camera + 4) << 12) + global_word(m_, kGteResult);
    const std::uint32_t py = (word_at(m_, camera + 8) << 12) + global_word(m_, kGteResult + 4);
    const std::uint32_t pz = (word_at(m_, camera + 0xc) << 12) + global_word(m_, kGteResult + 8);
    std::uint32_t id;
    switch (type - 10) {
    case 0: id = 0x664; break;
    case 1: id = 0x665; break;
    case 2: id = 0x666; break;
    default:
        check(false, 0x005348bc);                                          // "Bad medal type"
        return 0;
    }
    std::uint32_t object = engine_value(0x0047fd30, {0x100});              // TRG_AllocObjectStorage
    if (object != 0) {
        scratch_line("");
        set_global_word(m_, kScratchLine, px);
        set_global_word(m_, kScratchLine + 4, py);
        set_global_word(m_, kScratchLine + 8, pz);
        object = engine_value(0x004a7c50, {object, id, kScratchLine, kPowerupFlags, 0, 0xffff, 0xffffffff}, dwords_text({px, py, pz}));   // Powerup_Construct
    }
    s_.note("printf_log", {0x00534908});
    set_half_at(m_, object + 0xf2, u(x));
    set_half_at(m_, object + 0xf4, u(y));
    set_half_at(m_, object + 0xf6, u(z));
    set_byte_at(m_, object + 5, byte_at(m_, object + 5) | 2);
    set_half_at(m_, object + 0xee, 0x50);
    set_half_at(m_, object + 0x28, u(scale));
    set_half_at(m_, object + 0x2a, u(scale));
    set_half_at(m_, object + 0x2c, u(scale));
    set_half_at(m_, object + 0x72, 0x1e);
    set_half_at(m_, object + 0xec, u(speed));
    set_half_at(m_, object + 0xea, u(speed));
    return object;
}

// 0x004b8fc0: a tape or a medal as a power-up object. `position` and `target` point to three dwords each, `colour` to three more.
std::uint32_t GoalScreens::tape_construct(std::uint32_t self, std::uint32_t type, std::uint32_t position, std::uint32_t target, std::int32_t scale, std::uint32_t arg5,
                                          std::uint32_t colour, std::uint32_t arg7, std::uint32_t flag) {
    (void)arg7;
    engine(0x00480060, {self});                                            // the power-up base constructor
    for (std::uint32_t at : {0xf0u, 0xf4u, 0xf8u, 0xfcu, 0x100u, 0x104u, 0x114u, 0x118u, 0x11cu}) {
        set_word_at(m_, self + at, 0);
    }
    set_word_at(m_, self + 0xec, arg5);
    set_word_at(m_, self, 0x0051979c);
    set_word_at(m_, self + 0xe4, u(scale));
    set_word_at(m_, self + 0xe8, u(scale));
    for (std::uint32_t k = 0; k < 3; ++k) {
        set_word_at(m_, self + 0xf0 + 4 * k, word_at(m_, position + 4 * k));
        set_word_at(m_, self + 0xfc + 4 * k, word_at(m_, target + 4 * k));
    }
    set_half_at(m_, self + 0x28, u(scale));
    set_half_at(m_, self + 0x2a, u(scale));
    set_half_at(m_, self + 0x2c, u(scale));
    set_half_at(m_, self + 0x3c, type);
    set_word_at(m_, self + 0x12c, 0);
    set_half_at(m_, self + 0x128, flag);
    set_word_at(m_, self + 0xd8, 0);
    set_word_at(m_, self + 0xd4, 0);
    for (std::uint32_t k = 0; k < 3; ++k) {
        set_word_at(m_, self + 0x114 + 4 * k, word_at(m_, position + 4 * k));
        set_word_at(m_, self + 8 + 4 * k, word_at(m_, position + 4 * k));
        set_word_at(m_, self + 0x4c + 4 * k, word_at(m_, colour + 4 * k));
    }
    set_byte_at(m_, self + 0x6c, 5);
    set_byte_at(m_, self + 0x6d, 5);
    set_byte_at(m_, self + 0x6e, 5);
    set_half_at(m_, self + 0x72, 0x64);
    set_word_at(m_, self + 0x5c, 0x8000);
    s_.note("rnd", {0x14});
    set_half_at(m_, self + 0x126, u(s_.value("rnd")) + 0x5a);
    set_half_at(m_, self + 0x122, 0x32);
    set_half_at(m_, self + 0x124, 0x32);
    set_half_at(m_, self + 0xae, 0x12c);
    engine_named(0x0047fe30, {self}, "items");                                 // Object_SelectNamedPsxRegion
    const std::uint32_t kind = half_at(m_, self + 0x3c);
    auto find_model = [&](std::uint32_t checksum, std::uint32_t variant) {
        set_half_at(m_, self + 0x1a, engine_value(0x004b1de0, {checksum, variant}));
    };
    if (kind == 0x664 || kind == 0x665 || kind == 0x666) {
        engine_named(0x0047fe30, {self}, "skmedals");
        set_byte_at(m_, self + 0x109, 1);
        find_model(kind == 0x664 ? 0x54636518u : kind == 0x665 ? 0xba6d0434u : 0x2364558eu, global_byte(m_, kModelByte));
    } else if (kind == 0x10 || kind == 0x12) {
        set_half_at(m_, self + 0x72, 0x96);
        set_byte_at(m_, self + 0x109, 1);
        find_model(0xcc4e141f, global_byte(m_, kModelByteTape));
    } else {
        check(false, 0x005373d8);                                          // "Bad powerup type"
    }
    // the tapes that count for a goal the career has (or the mode has) are marked
    const std::uint32_t slot = kind - 4;
    if (slot <= 0xc) {
        static const std::uint8_t other[13] = {0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0};          // 0: marked, 1: not (game modes other than the career)
        static const std::uint8_t career_mode[13] = {0, 0, 0, 2, 2, 2, 0, 2, 2, 2, 2, 0, 1};    // 0: marked if goal type 1 is got, 1: if goal type 6 is got, 2: not
        if (global_int(m_, career::kGameMode) != 1) {
            if (other[slot] == 0) {
                set_half_at(m_, self + 0x128, 1);
            }
        } else if (career_mode[slot] == 0) {
            if (career_.got_goal_type(1)) {
                set_half_at(m_, self + 0x128, 1);
            }
        } else if (career_mode[slot] == 1) {
            if (career_.got_goal_type(6)) {
                set_half_at(m_, self + 0x128, 1);
            }
        }
    }
    engine(0x004801d0, {self, kPowerupList});                              // ObjectList_PushHead
    if (byte_at(m_, self + 0x109) == 0) {
        engine(0x004b93a0, {self});
    }
    return self;
}

}  // namespace opentony::runtime::frontend
