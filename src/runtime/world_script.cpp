#include "world_views.hpp"

// 10g s14: the platform's script methods (vtable 0x005194f8 slots 7 and 8, 0x004a07e0 and 0x004a1060) and the base class's script commands they fall back on (0x004015e0). `self` is the object; the script stream
// is read through the cursor at +0x17c.

namespace opentony::runtime::worldkit {

using namespace views;

namespace {

using Word = std::uint32_t;

std::int32_t sx16(Word value) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(value)); }

constexpr Word kStopVoice = 0x004ad9f0;            // Audio_StopVoice(voice)
constexpr Word kPlaySound = 0x004ad620;            // SFX_PlayX(sound, volume, pan)
constexpr Word kPlaceSound = 0x004ad7d0;           // the positional sound start (sound, position, 0)
constexpr Word kDecodeNode = 0x004c8650;           // TRG_DecodeNodePosition(out, node)
constexpr Word kShatter = 0x004af0b0;              // Shatter_Item(object, 0, 0)
constexpr Word kBaseCommand = 0x004015e0;          // TRG_ExecuteBaddyScriptOpcode(command), thiscall
constexpr Word kRegionTables = 0x0056d43c;         // per region (0x44 bytes each): the pointer to its model pointers
constexpr Word kVector = kScratch + 0x240;         // the locals of the retail code: a position read from the stream

}  // namespace

// 0x004a0710 (thiscall, one argument): a changed move-to target (+0x1c4) is taken and raises the move-to flag
void WorldKit::platform_set_target(Word self, Word target) {
    constexpr Word kSlot = 0x1c4;
    if (word_at(m_, self + kSlot) != word_at(m_, target) || word_at(m_, self + kSlot + 4) != word_at(m_, target + 4) || word_at(m_, self + kSlot + 8) != word_at(m_, target + 8)) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + kSlot + 4 * k, word_at(m_, target + 4 * k));
        }
        set_word_at(m_, self + 0x178, word_at(m_, self + 0x178) | 1u);
    }
}

// 0x004a07e0 (CPlatform__ExecuteScriptCommand, vtable slot 7, thiscall, one argument): the platform's own script commands (0x42xx: die, move-to targets, the collision box, the scale animations, the state bit,
// the bounce kind and its extents; 0x45xx: the sounds); any other command is the base class's. Answers 0 when the script must stop here (the object died, or the cursor was put back), else 1.
Word WorldKit::platform_script_command(Word self, Word command) {
    const auto next = [&] {
        const Word at = platform_at(m_, self).field_17c();
        platform_at(m_, self).set_field_17c(static_cast<Word>(at + 2));
        return half_at(m_, at);
    };
    const auto stop_voice = [&] {
        const Word voice = platform_at(m_, self).field_204();
        if (voice != 0xffffffffu) {
            engine(kStopVoice, {voice});
        }
    };
    const auto start_scale = [&] {       // the first scale command sets the scaled bit and the scale to 1.0
        if ((half_at(m_, self + 4) & 0x200u) == 0) {
            set_half_at(m_, self + 4, half_at(m_, self + 4) | 0x200u);
            for (Word k = 0; k < 3; ++k) {
                set_half_at(m_, self + 0x28 + 2 * k, 0x1000);
            }
        }
    };
    // an amount (percent of the full scale) over a number of frames: the step per frame, as a 16-bit word (a divide by 100 per frame count, or at once with no frames)
    const auto scale_step = [&](Word amount, Word frames) { return u(divide(sx16(amount) << 12, frames != 0 ? s(frames * 100) : 100)); };
    const auto scale_axis = [&](Word counter, Word step_at, Word scale_at) {
        start_scale();
        const Word amount = next(), frames = next();
        set_half_at(m_, self + counter, frames);
        const Word step = scale_step(amount, frames);
        set_half_at(m_, self + step_at, step);
        set_half_at(m_, self + scale_at, half_at(m_, self + scale_at) + (step & 0xffffu));
    };
    const auto read_vector = [&] {       // three dwords from the next aligned place of the stream, shifted left by 12
        Word at = (platform_at(m_, self).field_17c() + 3) & 0xfffffffcu;
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kVector + 4 * k, word_at(m_, at) << 12);
            at += 4;
        }
        platform_at(m_, self).set_field_17c(static_cast<Word>(at));
    };
    switch (command & 0xffffu) {
    case 0x4205:
        virtual_call(self, 2);
        return 0;
    case 0x4220:
        read_vector();
        platform_set_target(self, kVector);
        return 1;
    case 0x4221: {
        Word node = next();
        if ((node & 0x2000u) != 0) {
            node = thiscall_value(word_at(m_, platform_at(m_, self).vtable() + 0x24), self, {node});
        }
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kVector + 4 * k, 0);
        }
        engine(kDecodeNode, {kVector, node & 0xffffu});
        if (!s_.live()) {
            const std::string name = pcplatform::engine_name(kDecodeNode);      // (the position the node gives: the host's answers)
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, kVector + 4 * k, static_cast<Word>(s_.value(name)));
            }
        }
        platform_set_target(self, kVector);
        return 1;
    }
    case 0x4222:
        read_vector();
        vec3_add(kVector, self + 8);
        platform_set_target(self, kVector);
        return 1;
    case 0x4300:
        platform_at(m_, self).set_field_17c(static_cast<Word>(platform_at(m_, self).field_17c() - 2));
        return 0;
    case 0x4301:
        set_byte_at(m_, self + 4, byte_at(m_, self + 4) | 1u);
        virtual_call(self, 2);
        engine(kShatter, {self, 0, 0});
        return 0;
    case 0x4302:
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + 0x20c + 4 * k, u(s(next() << 12) >> 1));
        }
        set_word_at(m_, self + 0x178, word_at(m_, self + 0x178) | 4u);
        return 1;
    case 0x4303:
        platform_at(m_, self).set_field_17c(static_cast<Word>(platform_at(m_, self).field_17c() + 2));
        return 1;
    case 0x4304: {
        start_scale();
        const Word amount = next(), frames = next();
        for (Word k = 0; k < 3; ++k) {
            set_half_at(m_, self + 0x1e4 + 2 * k, frames);
        }
        const Word step = scale_step(amount, frames);
        for (Word k = 0; k < 3; ++k) {
            set_half_at(m_, self + 0x1ea + 2 * k, step);
        }
        for (Word k = 0; k < 3; ++k) {
            set_half_at(m_, self + 0x28 + 2 * k, half_at(m_, self + 0x28 + 2 * k) + half_at(m_, self + 0x1ea + 2 * k));
        }
        return 1;
    }
    case 0x4305:
        if (next() != 0) {
            set_byte_at(m_, self + 4, byte_at(m_, self + 4) | 8u);
        } else {
            set_half_at(m_, self + 4, half_at(m_, self + 4) & 0xfff7u);
        }
        return 1;
    case 0x4306:
        scale_axis(0x1e4, 0x1ea, 0x28);
        return 1;
    case 0x4307:
        scale_axis(0x1e6, 0x1ec, 0x2a);
        return 1;
    case 0x4308:
        scale_axis(0x1e8, 0x1ee, 0x2c);
        return 1;
    case 0x4309: {
        const Word kind = next();
        platform_at(m_, self).set_field_1f2(static_cast<std::uint16_t>(kind));
        set_word_at(m_, self + 0x178, kind != 0 ? word_at(m_, self + 0x178) | 0x10u : word_at(m_, self + 0x178) & 0xffffffefu);
        const Word table = word_at(m_, kRegionTables + platform_at(m_, self).region() * 0x44);
        const Word model = word_at(m_, table + platform_at(m_, self).model_index() * 4) + 0xc;
        for (Word k = 0; k < 3; ++k) {
            set_half_at(m_, self + 0x1f4 + 2 * k, half_at(m_, model + 4 * k) - half_at(m_, model + 4 * k + 2));
        }
        return 1;
    }
    case 0x4507: {
        stop_voice();
        const Word sound = next();
        platform_at(m_, self).set_field_204(static_cast<Word>(engine_value(kPlaySound, {sound, 0x2000, 0})));
        platform_at(m_, self).set_field_208(0xffffffffu);
        return 1;
    }
    case 0x4508: {
        stop_voice();
        const Word sound = next();
        const Word period = next();
        platform_at(m_, self).set_field_208(static_cast<Word>(period));
        platform_at(m_, self).set_field_204(static_cast<Word>(engine_value(kPlaceSound, {sound, self + 8, 0})));
        return 1;
    }
    case 0x4509:
        stop_voice();
        platform_at(m_, self).set_field_204(0xffffffffu);
        return 1;
    default:
        return thiscall_value(kBaseCommand, self, {command});
    }
}

// 0x004027c0 (thiscall): the next operand word of the stream (+0x17c); a word with bit 13 and without bit 15 names a variable, answered by the object's slot 9 (0x24)
Word WorldKit::script_operand(Word self) {
    const Word at = baddy_at(m_, self).field_17c();
    const Word word = half_at(m_, at);
    baddy_at(m_, self).set_field_17c(static_cast<Word>(at + 2));
    if ((word & 0x8000u) == 0 && (word & 0x2000u) != 0) {
        return script_variable(self, word);
    }
    return word;
}

// the object's slot 9 (0x24) on a variable word. A variable may read operand words after it: live the handler moves the cursor itself; the probe's stub moves it by a scripted amount, recorded as
// `variable_cursor` (absent from the fixtures before 10g s15, where the cursor stays)
Word WorldKit::script_variable(Word self, Word word) {
    const Word value = thiscall_value(word_at(m_, baddy_at(m_, self).vtable() + 0x24), self, {word});
    if (!s_.live()) {
        baddy_at(m_, self).set_field_17c(static_cast<Word>(static_cast<Word>(s_.value("variable_cursor", s(baddy_at(m_, self).field_17c())))));
    }
    return value;
}

void WorldKit::baddy_stamp(Word line) {
    set_global_word(m_, 0x00568638, line);
    set_string(0x0056881c, "H:\\TonyHawk\\Pc2\\baddy.cpp");
}

namespace {

constexpr Word kNodeTable = 0x0056e210;        // the node records (a pointer to the table of their pointers)
constexpr Word kGameMode = 0x00533f38;         // LevelEventGameModeSelector (1: the career)
constexpr Word kSkaters = 0x0056a858;          // the two skaters
constexpr Word kLevel = 0x0056a898;            // the level's index
constexpr Word kReplayFlags = 0x0056a838;      // byte: the level flags a replay plays back
constexpr Word kShakeOn = 0x0055fa38;
constexpr Word kClock = 0x0056e320;
constexpr Word kBitCount = 0x0055f72c;
constexpr Word kTargetSkater = 0x0056a960;
constexpr Word kCameraHeights = 0x00533f90;    // two dwords, one per skater
constexpr Word kWord42a2 = 0x00568684;
constexpr Word kSignalListA = 0x0055f6bc;
constexpr Word kActiveObjects = 0x0056af40;
constexpr Word kVideoPlaying = 0x004cde70;     // Video_Playing (the answer is AL)
constexpr Word kTrickConfig = 0x00416050;      // TrickConfig_FindByCharacter(character): the career record
constexpr Word kSkipBlock = 0x004014a0;        // TRG_SkipConditionalBlock (thiscall)
constexpr Word kSelectRegion = 0x0047fe30;     // Object_SelectNamedPsxRegion(name) (thiscall)
constexpr Word kAnimationCycle = 0x00480890;   // (thiscall: index, direction)
constexpr Word kAnimationStart = 0x00480730;   // (thiscall: index, 0, -1, -1)
constexpr Word kCameraShake = 0x0040bd40;      // Camera_ApplyShake(position, kind) (thiscall on the skater's camera)
constexpr Word kSfxNothing2 = 0x004adc50;
constexpr Word kSfxNothing1 = 0x004adc30;
constexpr Word kSparkTrajectory = 0x00406070, kSparkCone = 0x00406090, kSparkSize = 0x00406160, kSparkRgb = 0x004061c0, kSparkFade = 0x004061e0;
constexpr Word kMemFree = 0x0046f2c0;          // Mem_GetFree(heap)
constexpr Word kBitNew = 0x00403d30;           // CBit_New(size)
constexpr Word kSparkConstruct = 0x00406200;   // CSpark_Construct(position, 0x20, 0x2000, 0x20) (thiscall)
constexpr Word kPowerupCreate = 0x004a8e50;    // Powerup_CreateFromTrigger(kind, position, a, b, -1)
constexpr Word kPowerupSetFields = 0x004a8260; // (thiscall: lifetime, 5)
constexpr Word kNodeLinks = 0x004c8550;        // TRG_GetNodeLinks(node)
constexpr Word kSignalNodes = 0x004c5b60, kExecuteList = 0x004c5b00, kSignalNode = 0x004c5b30, kSendPulse = 0x004c58b0;
constexpr Word kFade = 0x00449810;             // Fade_Start
constexpr Word kGotGoalType = 0x00414650;      // Career_GotGoalType(type)
constexpr Word kCollectible = 0x0048d8a0;      // Panel_CollectibleCounted(argument)
constexpr Word kNodeLocal = kScratch + 0x2c0;  // the locals of the retail code: a node's position (12 bytes)
constexpr Word kStaged = kScratch + 0x2d0;     // ... the operand words of an arithmetic command and its result (at most 8 words)
constexpr Word kConeLocal = kScratch + 0x2e0;  // ... the spark cone (three words)

}  // namespace

// 0x004015e0 (TRG_ExecuteBaddyScriptOpcode, thiscall, one argument): the base class's script commands (0x41xx: the labels, gotos and conditionals, the level flags and goals, arithmetic on variables; 0x42xx:
// the animations, the state bits, the waits, the sounds, the sparks, the camera shake, the power-ups, the node signals, the fades and the logged no-ops). Answers 0 when the script must stop for this frame.
Word WorldKit::base_script_command(Word self, Word command) {
    const auto cursor = [&] { return baddy_at(m_, self).field_17c(); };
    const auto next = [&] {
        const Word at = cursor();
        baddy_at(m_, self).set_field_17c(static_cast<Word>(at + 2));
        return half_at(m_, at);
    };
    const auto value_of = [&](Word word) { return (word & 0x2000u) != 0 ? script_variable(self, word) : word; };   // (a variable's answer whole, a plain word zero-extended)
    const auto signed_value_of = [&](Word word) { return (word & 0x2000u) != 0 ? u(sx16(script_variable(self, word))) : word; };
    const auto skip = [&] { thiscall(kSkipBlock, self); };
    const auto video_playing = [&] { return (engine_value(kVideoPlaying) & 0xffu) != 0; };
    const auto career = [&] { return engine_value(kTrickConfig, {word_at(m_, global_word(m_, kSkaters) + 0x2cc0)}); };
    const auto skip_string = [&] {      // past the NUL, to the next even address
        Word at = cursor();
        while (byte_at(m_, at) != 0) {
            ++at;
        }
        baddy_at(m_, self).set_field_17c(static_cast<Word>((at & 1u) != 0 ? at + 1 : at + 2));
    };
    const Word op = command & 0xffffu;
    switch (op) {
    case 0x4106: {
        const Word node = value_of(next()) & 0xffffu;
        const Word record = word_at(m_, global_word(m_, kNodeTable) + 4 * node);
        baddy_stamp(0x38e);
        check(half_at(m_, record) == 1000, 0x005243b8);
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kNodeLocal + 4 * k, 0);
        }
        baddy_at(m_, self).set_field_17c(static_cast<Word>(engine_value(kDecodeNode, {kNodeLocal, node}) + 6));
        return 1;
    }
    case 0x4104: {
        const Word label = next();
        baddy_stamp(0x39a);
        check(label < 8, 0x00524398);
        set_word_at(m_, self + 0x180 + 4 * label, cursor());
        return 1;
    }
    case 0x4105: {
        const Word start = cursor();
        while (half_at(m_, cursor()) != 0x4100) {
            if (next() == 0x4104) {
                const Word label = next();
                baddy_stamp(0x3a7);
                check(label < 8, 0x00524398);
                set_word_at(m_, self + 0x180 + 4 * label, cursor());
            }
        }
        baddy_at(m_, self).set_field_17c(static_cast<Word>(start));
        return 1;
    }
    case 0x4101:
    case 0x4102: {
        const Word label = next();
        baddy_stamp(op == 0x4101 ? 0x3b2 : 0x3bb);
        check(label < 8, 0x00524380);
        baddy_stamp(op == 0x4101 ? 0x3b3 : 0x3bc);
        const Word slot = self + 0x180 + 4 * label;
        check(word_at(m_, slot) != 0, 0x00524360);
        baddy_at(m_, self).set_field_17c(static_cast<Word>(word_at(m_, slot)));
        return op == 0x4101 ? 1 : 0;
    }
    case 0x4116:
        if ((next() & baddy_at(m_, self).field_178()) != 0) {
            skip();
        }
        return 1;
    case 0x4115: {
        const Word mask = next();
        if ((baddy_at(m_, self).field_178() & mask) != mask) {
            skip();
        }
        return 1;
    }
    case 0x4121: {
        const Word flag = next();
        if (global_word(m_, kGameMode) != 1) {
            return 1;
        }
        baddy_stamp(0x3e0);
        check(flag <= 7, 0x0052434c);
        if (video_playing()) {
            return 1;
        }
        const Word at = career() + global_word(m_, kLevel) + 0x26;
        set_byte_at(m_, at, byte_at(m_, at) | ((1u << (flag & 31u)) & 0xffu));
        return 1;
    }
    case 0x4122: {
        const Word flag = next();
        baddy_stamp(0x3f1);
        check(flag <= 7, 0x0052434c);
        const Word flags = video_playing() ? u(static_cast<std::int8_t>(global_byte(m_, kReplayFlags))) : u(static_cast<std::int8_t>(byte_at(m_, career() + global_word(m_, kLevel) + 0x26)));
        if ((flags & (1u << (flag & 31u))) == 0) {
            skip();
        }
        return 1;
    }
    case 0x4123: {
        const Word goal = next();
        baddy_stamp(0x410);
        check(goal <= 10, 0x00524338);
        const Word goals = u(sx16(half_at(m_, career() + 2 * global_word(m_, kLevel) + 0xc)));
        if ((goals & (1u << (goal & 31u))) == 0) {
            skip();
        }
        return 1;
    }
    case 0x4110:
    case 0x4111: {
        const Word destination = next();
        baddy_stamp(0x425);
        check(((destination >> 13) & 1u) != 0, 0x00524324);
        Word at = cursor();
        const Word current = u(sx16(script_variable(self, destination)));
        Word count = 0;
        while (at != cursor()) {       // the words the destination's variable read: staged with the result for the field handler
            if (count == 7) {
                throw std::runtime_error("script arithmetic destination has too many operand words");
            }
            set_half_at(m_, kStaged + 2 * count, half_at(m_, at));
            at += 2;
            ++count;
        }
        const Word operand = signed_value_of(next());
        set_half_at(m_, kStaged + 2 * count, op == 0x4110 ? operand + current : current - operand);
        const Word resume = cursor();
        baddy_at(m_, self).set_field_17c(static_cast<Word>(kStaged));
        std::string staged;
        for (Word k = 0; k < 2 * (count + 1); ++k) {
            staged.push_back(static_cast<char>(byte_at(m_, kStaged + k)));
        }
        engine(word_at(m_, baddy_at(m_, self).vtable() + 0x20), {self, destination}, staged);
        baddy_at(m_, self).set_field_17c(static_cast<Word>(resume));
        return 1;
    }
    case 0x4112:
    case 0x4113:
    case 0x4114: {
        const std::int32_t first = s(signed_value_of(next()));
        const std::int32_t second = s(signed_value_of(next()));
        const bool go_on = op == 0x4112 ? first > second : op == 0x4113 ? first < second : first == second;
        if (!go_on) {
            skip();
        }
        return 1;
    }
    case 0x4107:
        return 0;
    case 0x4120:
        return 1;
    case 0x4200:
        thiscall(kSelectRegion, self, {cursor()});
        skip_string();
        return 1;
    case 0x4201: {
        const Word index = next();
        const Word direction = next();
        thiscall(kAnimationCycle, self, {index, direction});
        return 1;
    }
    case 0x4202:
        thiscall(kAnimationStart, self, {next(), 0, 0xffffffffu, 0xffffffffu});
        return 1;
    case 0x4203:
        set_half_at(m_, self + 4, half_at(m_, self + 4) & 0xfffeu);
        return 1;
    case 0x4204:
        set_byte_at(m_, self + 4, byte_at(m_, self + 4) | 0x41u);
        return 1;
    case 0x4205:
        thiscall(word_at(m_, baddy_at(m_, self).vtable() + 8), self);
        return 1;
    case 0x4226:
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + 0x4c + 4 * k, 0);
        }
        return 1;
    case 0x4227: {
        const Word value = next();
        if (value != 0) {
            baddy_at(m_, self).set_field_172(static_cast<std::uint8_t>(value & 0xffu));
            baddy_at(m_, self).set_field_173(0);
            baddy_at(m_, self).set_field_178(static_cast<Word>(baddy_at(m_, self).field_178() | 2u));
        } else {
            baddy_at(m_, self).set_field_178(static_cast<Word>(baddy_at(m_, self).field_178() & 0xfffffffdu));
        }
        return 1;
    }
    case 0x4298: {
        const Word selector = value_of(next()) & 0xffffu;
        if (global_word(m_, kShakeOn) == 0) {
            return 1;
        }
        for (Word k = 0; k < 2; ++k) {
            const Word skater = word_at(m_, kSkaters + 4 * k);
            if (skater != 0) {
                thiscall(kCameraShake, word_at(m_, skater + 0x29b0), {self + 8, selector == 0 ? 2u : selector == 1 ? 1u : 0u});
            }
        }
        return 1;
    }
    case 0x4296:
        engine(kSfxNothing2, {next()});
        return 1;
    case 0x4297:
        engine(kSfxNothing1, {next()});
        return 1;
    case 0x4293: {
        const Word at = (cursor() + 3) & 0xfffffffcu;
        log_line(0x005242f4);
        baddy_at(m_, self).set_field_17c(static_cast<Word>(at + 4));
        return 1;
    }
    case 0x4294:
        log_line(0x005242d0);
        return 1;
    case 0x4280: {
        const Word word = next();
        baddy_at(m_, self).set_field_1c0(static_cast<Word>(word));
        if ((word & 0x2000u) != 0) {
            baddy_at(m_, self).set_field_1c0(static_cast<Word>(u(sx16(script_variable(self, word)))));
        }
        if ((half_at(m_, self + 4) & 0x100u) != 0) {
            baddy_at(m_, self).set_field_1dc(0);
            baddy_at(m_, self).set_field_1e0(static_cast<Word>(global_word(m_, kClock)));
        } else {
            baddy_at(m_, self).set_field_1c0(static_cast<Word>(baddy_at(m_, self).field_1c0() << 12));
        }
        return 0;
    }
    case 0x4281:
        if ((baddy_at(m_, self).field_174() & 1u) != 0) {
            return 1;
        }
        baddy_at(m_, self).set_field_17c(static_cast<Word>(cursor() - 2));
        return 0;
    case 0x4240:
        baddy_at(m_, self).set_field_16c(0);
        return 0;
    case 0x4290:
        engine(kPlaySound, {u(sx16(next())), 0x2000, 0});
        return 1;
    case 0x4291:
        engine(kPlaceSound, {u(sx16(next())), self + 8, 0});
        return 1;
    case 0x4292: {
        const Word count = next();
        if (s(global_word(m_, kBitCount)) >= 0xfa) {
            return 1;
        }
        engine(kSparkTrajectory, {self + 0x14});
        set_global_half(m_, kConeLocal, 0x200);
        set_global_half(m_, kConeLocal + 2, 0x1000);
        set_global_half(m_, kConeLocal + 4, 0);
        std::string cone;      // (the probe records the cone's three words)
        for (Word k = 0; k < 6; ++k) {
            cone.push_back(static_cast<char>(byte_at(m_, kConeLocal + k)));
        }
        engine(kSparkCone, {kConeLocal}, cone);
        engine(kSparkSize, {1});
        engine(kSparkRgb, {0x80, 0x80, 0x80});
        engine(kSparkFade, {4, 4, 4});
        set_global_word(m_, kSpriteFlag, 0);
        for (Word k = 0; k < count; ++k) {
            if (s(engine_value(kMemFree, {1})) > 0x2710) {
                const Word bit = engine_value(kBitNew, {0x50});
                if (bit != 0) {
                    thiscall(kSparkConstruct, bit, {self + 8, 0x20, 0x2000, 0x20});
                }
            }
        }
        set_global_word(m_, kSpriteFlag, 1);
        return 1;
    }
    case 0x4295:
        return 1;
    case 0x4299:
        next();
        log_line(0x005242a4);
        return 1;
    case 0x42a0: {
        const Word kind = value_of(next());
        const Word first = next();
        const Word second = next();
        const Word lifetime = next();
        const Word powerup = engine_value(kPowerupCreate, {kind, self + 8, first, second, 0xffffffffu});
        if (powerup != 0) {
            thiscall(kPowerupSetFields, powerup, {lifetime << 12, 5});
        }
        return 1;
    }
    case 0x429e: {
        const Word target = baddy_at(m_, self).field_1d8();
        const Word height = baddy_at(m_, self).field_c();
        if (target == 0) {
            set_global_word(m_, kCameraHeights, height);
            set_global_word(m_, kCameraHeights + 4, height);
        } else {
            set_word_at(m_, target == global_word(m_, kTargetSkater) ? kCameraHeights : kCameraHeights + 4, height);
        }
        return 1;
    }
    case 0x42a2:
        set_global_word(m_, kWord42a2, half_at(m_, cursor()));
        baddy_at(m_, self).set_field_17c(static_cast<Word>(cursor() + 2));
        return 1;
    case 0x429a:
    case 0x42a6: {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kNodeLocal + 4 * k, 0);
        }
        if (op == 0x429a) {
            engine(kDecodeNode, {kNodeLocal, value_of(next()) & 0xffffu});
        } else {
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, kNodeLocal + 4 * k, word_at(m_, self + 8 + 4 * k));
            }
        }
        const Word type = next();
        if (type <= 1) {
            return 1;
        }
        if (type == 2) {
            script_operand(self);
            baddy_at(m_, self).set_field_17c(static_cast<Word>(cursor() + 4));
            return 1;
        }
        baddy_stamp(0x2a4);
        check(false, 0x00524290);
        return 1;
    }
    case 0x429d:
        log_line(0x00524260);
        return 1;
    case 0x42b1:
    case 0x42b2: {
        const Word links = engine_value(kNodeLinks, {value_of(next()) & 0xffffu});
        engine(op == 0x42b1 ? kSignalNodes : kExecuteList, {links});
        return 1;
    }
    case 0x42b3:
    case 0x42b4: {
        const Word node = value_of(next()) & 0xffffu;
        if (op == 0x42b3) {
            engine(kSignalNode, {global_word(m_, kSignalListA), node});
            engine(kSignalNode, {global_word(m_, kActiveObjects), node});
        } else {
            engine(kSendPulse, {node});
        }
        return 1;
    }
    case 0x42b0:
        skip_string();
        return 1;
    case 0x42a3:
        baddy_stamp(0x4c5);
        check(false, 0x00524248);
        return 1;
    case 0x429c: {
        // the six arguments as retail pushes them: the three bytes are loaded into registers that hold other values (the cursor, the fourth and the sixth argument), so their upper bits come along
        const Word at = cursor();
        const Word fourth = half_at(m_, at + 6), sixth = half_at(m_, at + 8);
        engine(kFade, {(at & 0xffffff00u) | byte_at(m_, at), (fourth & 0xff00u) | byte_at(m_, at + 2), (sixth & 0xff00u) | byte_at(m_, at + 4), fourth, 0, sixth});
        baddy_at(m_, self).set_field_17c(static_cast<Word>(cursor() + 10));
        return 1;
    }
    case 0x42c0: {
        const Word argument = next();
        if (global_word(m_, kGameMode) != 1 || engine_value(kGotGoalType, {4}) != 0 || video_playing()) {
            return 1;
        }
        engine(kCollectible, {argument});
        return 1;
    }
    case 0x450a:
        baddy_stamp(0x4ce);
        check(false, 0x0052420c);
        return 1;
    default:
        baddy_stamp(0x4fb);
        check(false, 0x00524230);
        return 1;
    }
}

// 0x004a1060 (CPlatform__ApplyScriptField, vtable slot 8, thiscall, one argument): the platform's script fields (0x21xx and 0x2fxx: the gravity byte, the model, the spin and its acceleration, the model by checksum, the
// velocity, the angles); any other field is the base class's (0x004027f0)
void WorldKit::platform_script_field(Word self, Word field) {
    const auto model_flag = [&](Word index) {       // the model's flag 0x10 decides the object's state bit 0x20
        const Word table = word_at(m_, kRegionTables + platform_at(m_, self).region() * 0x44);
        const Word model = word_at(m_, table + index * 4);
        if ((byte_at(m_, model) & 0x10u) != 0) {
            set_byte_at(m_, self + 4, byte_at(m_, self + 4) | 0x20u);
        } else {
            set_half_at(m_, self + 4, half_at(m_, self + 4) & 0xffdfu);
        }
    };
    switch (field & 0xffffu) {
    case 0x2123: {
        const Word at = platform_at(m_, self).field_17c();
        platform_at(m_, self).set_field_16e(static_cast<std::uint8_t>(byte_at(m_, at)));
        platform_at(m_, self).set_field_17c(static_cast<Word>(at + 2));
        return;
    }
    case 0x2124: {
        const Word at = platform_at(m_, self).field_17c();
        const Word index = half_at(m_, at);
        platform_at(m_, self).set_field_17c(static_cast<Word>(at + 2));
        platform_at(m_, self).set_model_index(static_cast<std::uint16_t>(index));
        model_flag(index);
        return;
    }
    case 0x212f: {
        const Word at = (platform_at(m_, self).field_17c() + 3) & 0xfffffffcu;
        const Word index = engine_value(0x004b1de0, {word_at(m_, at), platform_at(m_, self).region()}) & 0xffffu;
        platform_at(m_, self).set_model_index(static_cast<std::uint16_t>(index));
        model_flag(index);
        set_half_at(m_, self + 4, half_at(m_, self + 4) & 0xfffeu);
        platform_at(m_, self).set_field_17c(static_cast<Word>(at + 4));
        return;
    }
    case 0x2127:
    case 0x2128:
    case 0x2134:
    case 0x2137: {
        const Word first = script_operand(self) & 0xffffu;
        const Word second = script_operand(self) & 0xffffu;
        const Word third = script_operand(self) & 0xffffu;
        const Word values[3] = {first, second, third};
        for (Word k = 0; k < 3; ++k) {
            switch (field & 0xffffu) {
            case 0x2127: set_half_at(m_, self + 0x70 + 2 * k, values[k]); break;
            case 0x2128: set_half_at(m_, self + 0x76 + 2 * k, values[k]); break;
            case 0x2134: set_word_at(m_, self + 0x4c + 4 * k, u(sx16(values[k]) * 4096)); break;
            default: set_half_at(m_, self + 0x14 + 2 * k, values[k]); break;
            }
        }
        return;
    }
    default:
        thiscall(0x004027f0, self, {field});
        return;
    }
}

// 0x004014a0 (TRG_SkipConditionalBlock, thiscall): skips the stream to the 0x4120 that ends the current conditional, counting the conditionals (0x4112..0x4116) nested in it; the terminator 0x4100 asserts
void WorldKit::skip_conditional_block(Word self) {
    Word depth = 0;
    for (;;) {
        const Word at = baddy_at(m_, self).field_17c();
        const Word word = half_at(m_, at);
        baddy_at(m_, self).set_field_17c(static_cast<Word>(at + 2));
        if (word == 0x4120) {
            if (depth == 0) {
                return;
            }
            --depth;
            continue;
        }
        if (word >= 0x4112 && word <= 0x4116) {
            ++depth;
        }
        if (word == 0x4100) {
            baddy_stamp(0x224);
            check(false, 0x005241e0);
            return;
        }
    }
}

// 0x004027f0 (thiscall, one argument): the base class's script fields (0x21xx: the position, the model by checksum, the flags, the counters, the attribute and register words); an unknown field asserts
void WorldKit::base_script_field(Word self, Word field) {
    const auto cursor = [&] { return baddy_at(m_, self).field_17c(); };
    const auto next = [&] {
        const Word at = cursor();
        baddy_at(m_, self).set_field_17c(static_cast<Word>(at + 2));
        return half_at(m_, at);
    };
    switch (field & 0xffffu) {
    case 0x2135:
        baddy_at(m_, self).set_field_17c(static_cast<Word>(((cursor() + 3) & 0xfffffffcu) + 4));
        return;
    case 0x212f: {
        const Word at = (cursor() + 3) & 0xfffffffcu;
        const Word index = engine_value(0x004b1de0, {word_at(m_, at), baddy_at(m_, self).region()});     // Spool_FindModelIndex(checksum, region)
        baddy_at(m_, self).set_field_17c(static_cast<Word>(at + 4));
        baddy_at(m_, self).set_model_index(static_cast<std::uint16_t>(index));
        return;
    }
    case 0x2140:
    case 0x2141:
    case 0x2142:
        set_word_at(m_, self + 8 + 4 * ((field & 0xffffu) - 0x2140), u(sx16(script_operand(self))) << 12);
        return;
    case 0x2131:
        if (next() != 0) {
            set_byte_at(m_, self + 4, byte_at(m_, self + 4) | 8u);
        } else {
            set_half_at(m_, self + 4, half_at(m_, self + 4) & 0xfff7u);
        }
        return;
    case 0x212c:
        baddy_at(m_, self).set_field_170(static_cast<std::uint8_t>(byte_at(m_, cursor())));
        baddy_at(m_, self).set_field_17c(static_cast<Word>(cursor() + 2));
        return;
    case 0x212d:
        baddy_at(m_, self).set_field_16f(static_cast<std::uint8_t>(byte_at(m_, cursor())));
        baddy_at(m_, self).set_field_17c(static_cast<Word>(cursor() + 2));
        return;
    case 0x2100:
        baddy_at(m_, self).set_field_b4(static_cast<std::uint16_t>(next()));
        return;
    case 0x2101:
        baddy_at(m_, self).set_field_ae(static_cast<std::uint16_t>(next()));
        return;
    case 0x2125: {
        const Word index = next();
        baddy_stamp(0x569);
        check(index < 6, 0x00524428);
        set_half_at(m_, self + 0x1a0 + 2 * index, half_at(m_, cursor()));
        baddy_at(m_, self).set_field_17c(static_cast<Word>(cursor() + 2));
        return;
    }
    case 0x212e:
        baddy_at(m_, self).set_field_17c(static_cast<Word>(cursor() + 2));
        return;
    case 0x2114:
        baddy_at(m_, self).set_field_1bc(static_cast<std::uint16_t>(next()));
        return;
    case 0x2115:
        baddy_at(m_, self).set_field_1be(static_cast<std::uint16_t>(next()));
        return;
    case 0x2120: {
        const Word index = next() & 0xffu;        // (retail tests and indexes with the low byte)
        const Word value = script_operand(self);
        set_global_word(m_, 0x00568638, 0xac);
        set_string(0x0056881c, "H:\\TonyHawk\\Pc2\\baddy.h");
        check(index < 6, 0x005243fc);
        set_half_at(m_, self + 0x1b0 + 2 * index, value);
        return;
    }
    case 0x2121:
        baddy_at(m_, self).set_field_108(static_cast<Word>(u(sx16(next()))));
        return;
    case 0x2122:
        baddy_at(m_, self).set_field_1ac(static_cast<std::uint16_t>(next()));
        return;
    default:
        baddy_stamp(0x59a);
        check(false, 0x005243e4);
        return;
    }
}

// 0x00402bb0 (TRG_GetScriptVariable, thiscall, one argument): the base class's script variables (the distance and the line of sight to the target skater, the object's and the skater's position in units, the
// counters, a random number, the attribute words, the node, a node's link count). The answer is a 16-bit word (above it retail leaves what the registers held)
Word WorldKit::base_script_variable(Word self, Word variable) {
    constexpr Word kTarget = 0x0056a960;
    constexpr Word kSightLocal = kScratch + 0x2f0;     // the locals of the retail code: the object's position raised by its eye height (12 bytes)
    const auto cursor = [&] { return baddy_at(m_, self).field_17c(); };
    const auto next = [&] {
        const Word at = cursor();
        baddy_at(m_, self).set_field_17c(static_cast<Word>(at + 2));
        return half_at(m_, at);
    };
    const Word skater = global_word(m_, kTarget);
    switch (variable & 0xffffu) {
    case 0x2136:
        return global_half(m_, 0x00561cfc);
    case 0x2132: {
        if (skater == 0) {
            return 0x1fff;
        }
        const Word distance = engine_value(0x004c95e0, {self + 8, skater + 8});      // Utils_XZDist
        return distance <= 0x1fff ? distance : 0x1fff;
    }
    case 0x2133: {
        if (skater == 0) {
            return 0;
        }
        set_global_word(m_, kSightLocal, baddy_at(m_, self).field_8());
        set_global_word(m_, kSightLocal + 4, baddy_at(m_, self).field_c() - (u(sx16(baddy_at(m_, self).field_1ae())) << 12));
        set_global_word(m_, kSightLocal + 8, baddy_at(m_, self).field_10());
        std::string local;
        for (Word k = 0; k < 12; ++k) {
            local.push_back(static_cast<char>(byte_at(m_, kSightLocal + k)));
        }
        return engine_value(0x004ca0c0, {kSightLocal, skater + 8}, local);          // Utils_LineOfSight
    }
    case 0x212e:
        baddy_stamp(0x5cd);
        check(false, 0x005244a4);
        return 0;
    case 0x212c:
        return baddy_at(m_, self).field_170();
    case 0x212d:
        return baddy_at(m_, self).field_16f();
    case 0x2140:
    case 0x2141:
    case 0x2142:
        return u(s(word_at(m_, self + 8 + 4 * ((variable & 0xffffu) - 0x2140))) >> 12);
    case 0x2150:
    case 0x2151:
    case 0x2152:
        return skater == 0 ? 0 : u(s(word_at(m_, skater + 8 + 4 * ((variable & 0xffffu) - 0x2150))) >> 12);
    case 0x2100:
        return baddy_at(m_, self).field_b4();
    case 0x2129:
        return u(rnd(next()));
    case 0x2120: {
        const Word index = byte_at(m_, cursor());
        baddy_at(m_, self).set_field_17c(static_cast<Word>(cursor() + 2));
        set_global_word(m_, 0x00568638, 0xad);
        set_string(0x0056881c, "H:\\TonyHawk\\Pc2\\baddy.h");
        check(index < 6, 0x005243fc);
        return half_at(m_, self + 0x1b0 + 2 * index);
    }
    case 0x212a:
        baddy_stamp(0x61f);
        check(baddy_at(m_, self).field_b0() != 0xffff, 0x0052447c);
        return baddy_at(m_, self).field_b0();
    case 0x212b: {
        const Word word = next();
        const Word node = ((word & 0x2000u) != 0 ? script_variable(self, word) : word) & 0xffffu;
        const Word links = engine_value(0x004c8550, {node});                          // TRG_GetNodeLinks
        return half_at(m_, links) == 0 ? 0 : half_at(m_, links + 2);
    }
    default:
        baddy_stamp(0x635);
        check(false, 0x00524448);
        return 0;
    }
}

// 0x004a1270 (vtable 0x005194f8 slot 9, thiscall, one argument): the platform's variables: 0x2200 (removed) asserts and answers 0, any other is the base class's
Word WorldKit::platform_script_variable(Word self, Word variable) {
    if ((variable & 0xffffu) == 0x2200) {
        set_global_word(m_, 0x00568638, 0x539);
        set_string(0x0056881c, "H:\\TonyHawk\\Pc2\\platform.cpp");
        check(false, 0x00536fd4);
        return 0;
    }
    return thiscall_value(0x00402bb0, self, {variable});
}

// 0x00403410 (TRG_ApplyScriptCommand, thiscall, one argument): the script object's own commands (0x4506 the position of the target skater, 0x4507..0x4509 the voice, 0x4700 the flag 0x100 of +0x178;
// 0x429b, 0x42a1, 0x4504 skip their operands, 0x429f and 0x4503 do nothing); any other is the base class's
Word WorldKit::script_object_command(Word self, Word command) {
    constexpr Word kSoundReference = 0x0056bed8;
    const auto next = [&] {
        const Word at = word_at(m_, self + 0x17c);
        set_word_at(m_, self + 0x17c, at + 2);
        return half_at(m_, at);
    };
    const auto stop_voice = [&] {
        const Word voice = word_at(m_, self + 0x1e8);
        if (voice != 0xffffffffu) {
            engine(kStopVoice, {voice});
        }
    };
    switch (command & 0xffffu) {
    case 0x4506: {
        const Word target = global_word(m_, 0x0056a960);
        if (target != 0) {
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, self + 8 + 4 * k, word_at(m_, target + 8 + 4 * k));
            }
        }
        return 1;
    }
    case 0x4504:
        set_word_at(m_, self + 0x17c, word_at(m_, self + 0x17c) + 8);
        [[fallthrough]];
    case 0x429b:
    case 0x42a1:
        set_word_at(m_, self + 0x17c, word_at(m_, self + 0x17c) + 12);
        return 1;
    case 0x429f:
    case 0x4503:
        return 1;
    case 0x4507: {
        stop_voice();
        const Word sound = next();
        set_word_at(m_, self + 0x1e8, engine_value(kPlaySound, {sound, 0x2000, 0}));
        set_word_at(m_, self + 0x1ec, 0xffffffffu);
        set_word_at(m_, self + 0x1f0, global_word(m_, kSoundReference));
        return 1;
    }
    case 0x4508: {
        stop_voice();
        const Word sound = next();
        const Word period = next();
        set_word_at(m_, self + 0x1ec, period);
        set_word_at(m_, self + 0x1e8, engine_value(kPlaceSound, {sound, self + 8, 0}));
        set_word_at(m_, self + 0x1f0, global_word(m_, kSoundReference));
        return 1;
    }
    case 0x4509:
        stop_voice();
        set_word_at(m_, self + 0x1e8, 0xffffffffu);
        return 1;
    case 0x4700:
        if (next() != 0) {
            set_word_at(m_, self + 0x178, word_at(m_, self + 0x178) | 0x100u);
        } else {
            set_word_at(m_, self + 0x178, word_at(m_, self + 0x178) & 0xfffffeffu);
        }
        return 1;
    default:
        return thiscall_value(kBaseCommand, self, {command});
    }
}

// 0x004031a0 (vtable 0x005183b0 slot 3, thiscall): the script object's per-frame update. The input flags (+0x68) are latched in +0x174 (bit 0 moved into the counter +0x170) and the state gets bits 0 and 6; then
// the script: with the mode byte (+0x16c) set a wait (+0x1c0) runs down by the frame's ticks, else the stream runs from the cursor (+0x17c); with it clear the object dies (slot 2). Every +0x1ec ticks the
// positional sound (+0x1e8) follows the object, its volume word (0x0052be44) scaled by +0x1f0 percent for the call. Then each attached model (+0x1dc records of 0x10 bytes at +0x1e0: an offset vector, the model at +0xc)
// is put at the object's offset position, or, when that is above the skater's +0x94, at a random spot (three Rnd calls) around the skater, and its second position (the vector +0x40 names) is set from that,
// the camera's vector +0x4c halved and the offset taken off again.
void WorldKit::script_object_update(Word self) {
    constexpr Word kFrameTicks = 0x0056a93c, kTick = 0x0056a8d4, kVolume = 0x0052be44, kTarget = 0x0056a960, kCamera = 0x0055fa38, kRandom = 0x004c9340;
    constexpr Word kPosition = kScratch + 0x300, kJitter = kScratch + 0x310, kSum = kScratch + 0x320, kHalf = kScratch + 0x330, kDelta = kScratch + 0x340, kFinal = kScratch + 0x350, kOne = kScratch + 0x360;
    const Word input = script_object_at(m_, self).field_68();
    set_byte_at(m_, self + 4, byte_at(m_, self + 4) | 0x41u);
    set_half_at(m_, self + 0x174, input);
    if ((input & 1u) != 0) {
        set_byte_at(m_, self + 0x170, byte_at(m_, self + 0x170) + 1);
        script_object_at(m_, self).set_field_68(static_cast<std::uint16_t>(input & 0xfffeu));
    }
    if (byte_at(m_, self + 0x16c) != 0) {
        const Word wait = word_at(m_, self + 0x1c0);
        if (wait != 0) {
            const Word ticks = global_word(m_, kFrameTicks);
            set_word_at(m_, self + 0x1c0, wait > ticks ? wait - ticks : 0);
        } else {
            thiscall(0x00401520, self, {word_at(m_, self + 0x17c)});                 // TRG_RunScriptStream
        }
    } else {
        virtual_call(self, 2);
    }
    const Word period = word_at(m_, self + 0x1ec);
    if (period != 0xffffffffu) {
        const Word tick = global_word(m_, kTick);
        if (tick - u(divide(s(tick), s(period))) * period == 0) {
            const Word volume = global_word(m_, kVolume);
            const Word scaled = u(divide(s(word_at(m_, self + 0x1f0) * volume), 100));
            set_global_word(m_, kVolume, scaled);
            std::string seen;               // (the volume word the sound service reads, as the probe's stub records it)
            for (unsigned k = 0; k < 4; ++k) {
                seen.push_back(static_cast<char>(scaled >> (8 * k)));
            }
            engine(0x004ad8b0, {word_at(m_, self + 0x1e8), self + 8, 0}, seen);       // SFX_ModifyPos
            set_global_word(m_, kVolume, volume);
        }
    }
    const Word models = word_at(m_, self + 0x1e0);
    if (models == 0) {
        return;
    }
    const Word threshold = word_at(m_, global_word(m_, kTarget) + 0x94);
    const Word count = half_at(m_, self + 0x1dc);
    Word offset = 0;
    for (Word index = 0; index < count; ++index, offset += 0x10) {
        const Word record = models + offset;
        const Word model = word_at(m_, record + 0xc);
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kPosition + 4 * k, word_at(m_, model + 0x44 + 4 * k));
        }
        vec3_add(kPosition, record);
        if (s(global_word(m_, kPosition + 4)) > s(threshold)) {
            const Word first = u((engine_value(kRandom, {0x960}) - 0x4b0) << 12);
            const Word second = u((engine_value(kRandom, {0x190}) - 0x4b0) << 12);
            const Word third = u((engine_value(kRandom, {0x960}) - 0x4b0) << 12);
            set_global_word(m_, kJitter, third);
            set_global_word(m_, kJitter + 4, second);
            set_global_word(m_, kJitter + 8, first);
            core_.vector_add_out(kSum, global_word(m_, kTarget) + 8, kJitter);
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, kPosition + 4 * k, word_at(m_, kSum + 4 * k));
            }
        }
        set_global_word(m_, kOne, 1);
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, model + 0x44 + 4 * k, word_at(m_, kPosition + 4 * k));
        }
        core_.vector_shift_right_out(kHalf, global_word(m_, kCamera) + 0x4c, kOne);
        core_.vector_subtract_out(kDelta, kPosition, record);
        core_.vector_add_out(kFinal, kDelta, kHalf);
        const Word second_position = word_at(m_, model + 0x40);
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, second_position + 4 * k, word_at(m_, kFinal + 4 * k));
        }
    }
}

}  // namespace opentony::runtime::worldkit
