#include "object_script.hpp"

#include <limits>
#include <string>

namespace opentony::trg {
namespace {

// A malformed or self-jumping program would spin forever in retail too; the
// native interpreter refuses instead of hanging the frame.
constexpr std::size_t kMaxCommandsPerRun = 1U << 20;

constexpr std::uint16_t kOpEnd = 0x4100;

[[nodiscard]] std::int32_t sign16(std::uint32_t value) noexcept {
    return static_cast<std::int32_t>(static_cast<std::int16_t>(static_cast<std::uint16_t>(value)));
}

[[nodiscard]] std::int32_t shift12(std::int32_t value) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << 12U);
}

[[nodiscard]] std::uint32_t align4(std::uint32_t address) noexcept {
    return (address + 3U) & ~3U;
}

} // namespace

ScriptObject ScriptObject::construct_cb(
    std::uint16_t node, std::uint32_t first_command, ScriptVec3 position,
    std::array<std::uint16_t, 3> params) {
    ScriptObject object;
    object.node = node;
    object.cursor = first_command;
    object.position = position;
    object.params = params;
    // Base constructors (0x00480060, 0x00480610) leave state bit 0x02
    // (animated) and list flags 0x12 (suspend-when-far, 0x10); the subtype-0xcb
    // constructor adds 0x41 to the state and clears list flag 0x10.
    object.state = 0x43;
    object.list_flags = 0x0002;
    object.active = 1;
    object.field_ae = 0;
    object.sound_period = -1;
    return object;
}

ScriptObject ScriptObject::construct_platform(
    std::uint16_t node, std::uint32_t first_command, ScriptVec3 position,
    std::array<std::uint16_t, 3> params) {
    ScriptObject object;
    object.node = node;
    object.cursor = first_command;
    object.position = position;
    object.params = params;
    // Base constructors (0x00480610, 0x00401270) leave state bit 0x02, list
    // flags 0x12 and +0xae = 0x80; the platform constructor rewrites the state
    // as (state & ~0x02) | 0x111 and keeps the suspend-when-far list flag.
    object.state = 0x0111;
    object.list_flags = 0x0012;
    object.active = 1;
    object.field_ae = 0x0080;
    object.attributes[0] = 0x0020;  // +0x1a0: arrival distance
    object.platform.kind = 0x0192;
    object.platform.sound_period = -1;
    return object;
}

ObjectScriptVm::ObjectScriptVm(
    const TrgFile& file, ObjectScriptServices& services, ScriptClass script_class)
    : file_(file), services_(services), script_class_(script_class) {}

std::uint16_t ObjectScriptVm::peek_word(std::uint32_t address) const {
    if (address >= kScriptScratchBase) {
        const std::size_t index = (address - kScriptScratchBase) / 2U;
        if (index >= scratch_words_) {
            throw FormatError("script scratch read is outside the temporary operands");
        }
        return scratch_[index];
    }
    const auto bytes = file_.bytes();
    if (address > bytes.size() || bytes.size() - address < 2U) {
        throw FormatError("script read is outside the TRG file");
    }
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(bytes[address])
        | (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[address + 1U])) << 8U));
}

std::uint16_t ObjectScriptVm::read_word(ScriptObject& object) const {
    const std::uint16_t word = peek_word(object.cursor);
    object.cursor += 2U;
    return word;
}

std::uint32_t ObjectScriptVm::read_u32_aligned(ScriptObject& object) const {
    const std::uint32_t address = align4(object.cursor);
    const std::uint32_t low = peek_word(address);
    const std::uint32_t high = peek_word(address + 2U);
    object.cursor = address + 4U;
    return low | (high << 16U);
}

void ObjectScriptVm::assert_that(bool condition, std::string_view message) {
    if (!condition) {
        ++assertions_;
        services_.on_assertion(message);
    }
}

void ObjectScriptVm::unknown_command(std::string_view message) {
    ++assertions_;
    services_.on_assertion(message);
}

std::uint32_t ObjectScriptVm::script_point_cursor(std::size_t node) const {
    const NodeView& view = file_.node(node);
    if (view.type != 1000) {
        throw FormatError("script node is not a type-1000 script point");
    }
    const std::uint16_t link_words = peek_word(view.offset + 2U);
    const std::uint32_t position = (view.offset + static_cast<std::uint32_t>(link_words) * 2U + 7U) & ~3U;
    if (position + 18U > view.offset + view.size) {
        throw FormatError("script point commands are outside their node");
    }
    // TRG_DecodeNodePosition returns the end of the 12-byte position; the
    // runner skips three u16 parameters after it.
    return position + 12U + 6U;
}

// TRG_ReadScriptOperand (0x004027c0): immediates are returned as-is, a word
// with 0x2000 set and 0x8000 clear names a variable.
std::uint32_t ObjectScriptVm::read_operand_word(ScriptObject& object) {
    const std::uint16_t word = read_word(object);
    if ((word & 0x8000U) == 0 && (word & 0x2000U) != 0) {
        return variable(object, word) & 0xffffU;
    }
    return word;
}

// The comparison/arithmetic forms resolve only on bit 0x2000 and sign-extend a
// variable's 16-bit result while immediates stay unsigned.
std::int32_t ObjectScriptVm::operand(ScriptObject& object, std::uint16_t word) {
    if ((word & 0x2000U) != 0) {
        return sign16(variable(object, word));
    }
    return static_cast<std::int32_t>(word);
}

// TRG_GetScriptVariable (0x00402bb0)
std::uint32_t ObjectScriptVm::variable(ScriptObject& object, std::uint16_t id) {
    const std::uint32_t result = variable_value(object, id);
    if (variable_trace_) {
        variable_trace_(id, result);
    }
    return result;
}

std::uint32_t ObjectScriptVm::variable_value(ScriptObject& object, std::uint16_t id) {
    switch (id) {
    case 0x2100:
        return object.field_b4;
    case 0x2120: {
        const std::uint8_t index = static_cast<std::uint8_t>(peek_word(object.cursor));
        object.cursor += 2U;
        assert_that(index < kScriptRegisterCount, "Bad register index");
        return index < kScriptRegisterCount ? object.registers[index] : 0U;
    }
    case 0x2129: {
        const std::uint16_t range = read_word(object);
        return services_.random(range);
    }
    case 0x212a:
        assert_that(object.node != 0xffff, "V_MY_NODE in script object with no node");
        return object.node;
    case 0x212b: {
        std::uint16_t node = read_word(object);
        if ((node & 0x2000U) != 0) {
            node = static_cast<std::uint16_t>(variable(object, node));
        }
        const std::vector<std::uint16_t> links = file_.links(node);
        return links.empty() ? 0U : links.front();
    }
    case 0x212c:
        return object.byte_170;
    case 0x212d:
        return object.byte_16f;
    case 0x212e:
        assert_that(false, "V_VERTICAL_COMPLIANCE removed");
        return 0;
    case 0x2132: {
        if (!services_.has_target()) {
            return 0x1fff;
        }
        const std::uint32_t distance = services_.xz_distance(object.position, services_.target_position());
        return distance < 0x2000U ? distance : 0x1fffU;
    }
    case 0x2133: {
        if (!services_.has_target()) {
            return 0;
        }
        const ScriptVec3 eye{
            object.position[0],
            static_cast<std::int32_t>(
                static_cast<std::uint32_t>(object.position[1])
                + static_cast<std::uint32_t>(sign16(object.height_1ae)) * static_cast<std::uint32_t>(-0x1000)),
            object.position[2],
        };
        return services_.line_of_sight(eye, services_.target_position());
    }
    case 0x2136:
        return services_.global_2136();
    case 0x2140:
        return static_cast<std::uint32_t>(object.position[0] >> 12);
    case 0x2141:
        return static_cast<std::uint32_t>(object.position[1] >> 12);
    case 0x2142:
        return static_cast<std::uint32_t>(object.position[2] >> 12);
    case 0x2150:
    case 0x2151:
    case 0x2152:
        if (services_.has_target()) {
            return static_cast<std::uint32_t>(services_.target_position()[id - 0x2150U] >> 12);
        }
        return 0;
    case 0x2200:
        // Only the platform's variable handler (0x004a1270) knows this id; it
        // reports the removed V_NOT_TROD_UPON_TIMER and yields 0.
        if (script_class_ == ScriptClass::Platform) {
            assert_that(false, "V_NOT_TROD_UPON_TIMER removed by ...");
            return 0;
        }
        assert_that(false, "Unknown script variable (maybe v...)");
        return 0;
    default:
        assert_that(false, "Unknown script variable (maybe v...)");
        return 0;
    }
}

// TRG_ApplyScriptFieldWord (0x004027f0)
void ObjectScriptVm::set_field(ScriptObject& object, std::uint16_t id) {
    if (script_class_ == ScriptClass::Platform && platform_field(object, id)) {
        return;
    }
    switch (id) {
    case 0x2100:
        object.field_b4 = read_word(object);
        return;
    case 0x2101:
        object.field_ae = read_word(object);
        return;
    case 0x2114:
        object.field_1bc = read_word(object);
        return;
    case 0x2115:
        object.field_1be = read_word(object);
        return;
    case 0x2120: {
        const std::uint16_t index_word = read_word(object);
        const std::uint16_t value = static_cast<std::uint16_t>(read_operand_word(object));
        const std::uint8_t index = static_cast<std::uint8_t>(index_word);
        assert_that(index < kScriptRegisterCount, "Bad register index");
        if (index < kScriptRegisterCount) {
            object.registers[index] = value;
        }
        return;
    }
    case 0x2121:
        object.field_108 = sign16(read_word(object));
        return;
    case 0x2122:
        object.field_1ac = read_word(object);
        return;
    case 0x2125: {
        const std::uint16_t index = read_word(object);
        assert_that(index < kScriptAttributeCount, "Attribute index out of bounds");
        if (index < kScriptAttributeCount) {
            object.attributes[index] = peek_word(object.cursor);
        }
        // Falls into the 0x212e case: the value word is consumed either way.
        object.cursor += 2U;
        return;
    }
    case 0x212e:
        object.cursor += 2U;
        return;
    case 0x212c:
        object.byte_170 = static_cast<std::uint8_t>(peek_word(object.cursor));
        object.cursor += 2U;
        return;
    case 0x212d:
        object.byte_16f = static_cast<std::uint8_t>(peek_word(object.cursor));
        object.cursor += 2U;
        return;
    case 0x212f: {
        const std::uint32_t address = align4(object.cursor);
        const std::uint32_t checksum =
            static_cast<std::uint32_t>(peek_word(address)) | (static_cast<std::uint32_t>(peek_word(address + 2U)) << 16U);
        object.cursor = address + 4U;
        object.model_index = services_.find_model_index(checksum, object.region);
        return;
    }
    case 0x2131: {
        const std::int16_t value = static_cast<std::int16_t>(read_word(object));
        if (value != 0) {
            object.state = static_cast<std::uint16_t>(object.state | 0x08U);
        } else {
            object.state = static_cast<std::uint16_t>(object.state & 0xfff7U);
        }
        return;
    }
    case 0x2135:
        object.cursor = align4(object.cursor) + 4U;
        return;
    case 0x2140:
        object.position[0] = shift12(sign16(read_operand_word(object)));
        return;
    case 0x2141:
        object.position[1] = shift12(sign16(read_operand_word(object)));
        return;
    case 0x2142:
        object.position[2] = shift12(sign16(read_operand_word(object)));
        return;
    default:
        assert_that(false, "Unknown script variable");
        return;
    }
}

// TRG_SkipConditionalBlock (0x004014a0): a linear word scan, deliberately not
// operand-aware; it nests on the five conditional opcodes.
void ObjectScriptVm::skip_conditional_block(ScriptObject& object) {
    int depth = 0;
    while (true) {
        while (true) {
            const std::uint16_t word = read_word(object);
            if (word != 0x4120U) {
                if (word > 0x4111U && word < 0x4117U) {
                    ++depth;
                }
                if (word == kOpEnd) {
                    assert_that(false, "Mismatched conditionals");
                    return;
                }
                continue;
            }
            if (depth == 0) {
                return;
            }
            --depth;
        }
    }
}

void ObjectScriptVm::stop_object_sound(ScriptObject& object) {
    if (object.sound_handle != -1) {
        services_.stop_sound(object.sound_handle);
    }
}

bool ObjectScriptVm::execute(ScriptObject& object, std::uint16_t opcode) {
    if (script_class_ == ScriptClass::Platform) {
        return execute_platform(object, opcode);
    }
    if (script_class_ == ScriptClass::ScriptObjectCb) {
        // TRG_ApplyScriptCommand (0x00403410) handles a few extra commands and
        // hands everything else to the base handler.
        switch (opcode) {
        case 0x4506:
            if (services_.has_target()) {
                object.position = services_.target_position();
            }
            return true;
        case 0x4507:
            stop_object_sound(object);
            {
                const std::int16_t id = static_cast<std::int16_t>(read_word(object));
                object.sound_handle = services_.start_object_sound(id, false);
                object.sound_period = -1;
                object.sound_scale = services_.sound_reference();
            }
            return true;
        case 0x4508:
            stop_object_sound(object);
            {
                const std::int16_t id = static_cast<std::int16_t>(read_word(object));
                object.sound_period = static_cast<std::int32_t>(read_word(object));
                object.sound_handle = services_.start_object_sound(id, true);
                object.sound_scale = services_.sound_reference();
            }
            return true;
        case 0x4509:
            stop_object_sound(object);
            object.sound_handle = -1;
            return true;
        case 0x4700:
            if (static_cast<std::int16_t>(read_word(object)) == 0) {
                object.flags &= 0xfffffeffU;
            } else {
                object.flags |= 0x100U;
            }
            return true;
        case 0x429b:
        case 0x42a1:
            object.cursor += 12U;
            return true;
        case 0x429f:
        case 0x4503:
            return true;
        case 0x4504:
            object.cursor += 8U + 12U;
            return true;
        default:
            break;
        }
    }
    return execute_base(object, opcode);
}

// TRG_ExecuteBaddyScriptOpcode (0x004015e0). Returns false to yield.
bool ObjectScriptVm::execute_base(ScriptObject& object, std::uint16_t opcode) {
    switch (opcode) {
    case 0x4200:
    case 0x42b0: {
        // NUL-terminated string, cursor left after the terminator rounded up
        // to a 2-byte boundary.
        const auto bytes = file_.bytes();
        std::uint32_t end = object.cursor;
        while (end < bytes.size() && bytes[end] != std::byte{0}) {
            ++end;
        }
        if (end >= bytes.size()) {
            throw FormatError("script string is not terminated inside the TRG file");
        }
        if (opcode == 0x4200) {
            std::string name;
            for (std::uint32_t at = object.cursor; at < end; ++at) {
                name.push_back(static_cast<char>(std::to_integer<std::uint8_t>(bytes[at])));
            }
            services_.select_psx_region(name);
        }
        object.cursor = (end + 2U) & ~1U;
        return true;
    }
    case 0x4101:
    case 0x4102: {
        const std::uint16_t label = read_word(object);
        assert_that(label < kScriptLabelCount, "C_GOTO out of bounds");
        assert_that(label < kScriptLabelCount && object.labels[label] != 0, "C_GOTO to uninitialised label");
        if (label < kScriptLabelCount) {
            object.cursor = object.labels[label];
        }
        return opcode == 0x4101;
    }
    case 0x4103:
        unknown_command("Unknown script command");
        return true;
    case 0x4104: {
        const std::uint16_t label = read_word(object);
        assert_that(label < kScriptLabelCount, "Label exceeds MAX_BADDY_LABELS");
        if (label < kScriptLabelCount) {
            object.labels[label] = object.cursor;
        }
        return true;
    }
    case 0x4105: {
        // Pre-scan: record every label up to the terminator, then leave the
        // cursor where it started.
        const std::uint32_t start = object.cursor;
        std::uint32_t at = start;
        std::size_t guard = 0;
        while (peek_word(at) != kOpEnd) {
            if (++guard > kMaxCommandsPerRun) {
                throw FormatError("script label scan does not terminate");
            }
            const std::uint16_t word = peek_word(at);
            at += 2U;
            if (word == 0x4104U) {
                const std::uint16_t label = peek_word(at);
                at += 2U;
                assert_that(label < kScriptLabelCount, "Label exceeds MAX_BADDY_LABELS");
                if (label < kScriptLabelCount) {
                    object.labels[label] = at;
                }
            }
        }
        object.cursor = start;
        return true;
    }
    case 0x4106: {
        std::uint16_t node = read_word(object);
        if ((node & 0x2000U) != 0) {
            node = static_cast<std::uint16_t>(variable(object, node));
        }
        assert_that(file_.node(node).type == 1000, "C_SET_SCRIPT_NODE for non-SCRIPT");
        object.cursor = script_point_cursor(node);
        return true;
    }
    case 0x4107:
        return false;
    case 0x4110:
    case 0x4111: {
        // Read-modify-write: evaluate the destination variable (which may
        // consume operand words), stage those words plus the new value in the
        // scratch buffer, and run the field writer over the staged copy.
        const std::uint16_t destination = read_word(object);
        assert_that(((destination >> 13U) & 1U) != 0, "Bad C_ADD syntax");
        const std::uint32_t operands_begin = object.cursor;
        const std::int32_t current = sign16(variable(object, destination));
        const std::uint32_t operands_end = object.cursor;
        const std::size_t staged = (operands_end - operands_begin) / 2U;
        if (staged + 1U > scratch_.size()) {
            throw FormatError("script arithmetic destination has too many operand words");
        }
        for (std::size_t index = 0; index < staged; ++index) {
            scratch_[index] = peek_word(operands_begin + static_cast<std::uint32_t>(index) * 2U);
        }
        const std::uint16_t addend_word = read_word(object);
        const std::int32_t addend = operand(object, addend_word);
        const std::int32_t result = opcode == 0x4110 ? addend + current : current - addend;
        scratch_[staged] = static_cast<std::uint16_t>(result);
        scratch_words_ = staged + 1U;
        const std::uint32_t resume = object.cursor;
        object.cursor = kScriptScratchBase;
        set_field(object, destination);
        object.cursor = resume;
        scratch_words_ = 0;
        return true;
    }
    case 0x4112:
    case 0x4113:
    case 0x4114: {
        const std::uint16_t first_word = read_word(object);
        const std::int32_t first = operand(object, first_word);
        const std::uint16_t second_word = read_word(object);
        const std::int32_t second = operand(object, second_word);
        bool keep_going = false;
        if (opcode == 0x4112) {
            keep_going = second < first;
        } else if (opcode == 0x4113) {
            keep_going = first < second;
        } else {
            keep_going = first == second;
        }
        if (!keep_going) {
            skip_conditional_block(object);
        }
        return true;
    }
    case 0x4115: {
        const std::uint32_t mask = read_word(object);
        if ((object.flags & mask) != mask) {
            skip_conditional_block(object);
        }
        return true;
    }
    case 0x4116: {
        const std::uint32_t mask = read_word(object);
        if ((mask & object.flags) != 0) {
            skip_conditional_block(object);
        }
        return true;
    }
    case 0x4120:
        return true;
    case 0x4121: {
        const std::uint16_t flag = read_word(object);
        if (services_.game_mode_selector() != 1) {
            return true;
        }
        assert_that(flag < 8, "Illegal Flag (%d)");
        if (!services_.video_playing()) {
            services_.set_level_flag(flag);
        }
        return true;
    }
    case 0x4122: {
        const std::uint16_t flag = read_word(object);
        assert_that(flag < 8, "Illegal Flag (%d)");
        if (flag < 8 && services_.level_flag(flag)) {
            return true;
        }
        skip_conditional_block(object);
        return true;
    }
    case 0x4123: {
        const std::uint16_t goal = read_word(object);
        assert_that(goal < 0xb, "Illegal Goal (%d)");
        const std::int32_t bits = sign16(services_.level_goal_bits());
        if (goal < 32 && ((1 << goal) & bits) != 0) {
            return true;
        }
        skip_conditional_block(object);
        return true;
    }
    case 0x4201: {
        const std::uint16_t index = read_word(object);
        const std::uint16_t direction = read_word(object);
        services_.animation_cycle(index, direction);
        return true;
    }
    case 0x4202:
        services_.animation_start(read_word(object));
        return true;
    case 0x4203:
        object.state = static_cast<std::uint16_t>(object.state & 0xfffeU);
        return true;
    case 0x4204:
        object.state = static_cast<std::uint16_t>(object.state | 0x41U);
        return true;
    case 0x4205:
        die(object);
        return true;
    case 0x4226:
        object.velocity = {0, 0, 0};
        return true;
    case 0x4227: {
        const std::int16_t value = static_cast<std::int16_t>(read_word(object));
        if (value != 0) {
            object.byte_172 = static_cast<std::uint8_t>(value);
            object.byte_173 = 0;
            object.flags |= 2U;
        } else {
            object.flags &= 0xfffffffdU;
        }
        return true;
    }
    case 0x4240:
        object.active = 0;
        return false;
    case 0x4280: {
        const std::uint16_t word = read_word(object);
        object.wait = word;
        if ((word & 0x2000U) != 0) {
            object.wait = static_cast<std::uint32_t>(sign16(variable(object, word)));
        }
        if ((object.state & 0x0100U) == 0) {
            object.wait = static_cast<std::uint32_t>(shift12(static_cast<std::int32_t>(object.wait)));
        } else {
            object.field_1dc = 0;
            object.field_1e0 = services_.simulation_time();
        }
        return false;
    }
    case 0x4281:
        if ((object.signal_latch & 1U) != 0) {
            return true;
        }
        object.cursor -= 2U;
        return false;
    case 0x4290:
        services_.play_sound(static_cast<std::int16_t>(read_word(object)));
        return true;
    case 0x4291:
        services_.play_positional_sound(static_cast<std::int16_t>(read_word(object)), object.position);
        return true;
    case 0x4292: {
        const std::uint16_t count = read_word(object);
        if (!services_.effect_slots_available()) {
            return true;
        }
        services_.on_repeated_effect(ScriptRepeatedEffect{object.position, object.params, count});
        return true;
    }
    case 0x4293:
        services_.on_log("ignoring script cmd: setting fmv");
        object.cursor = align4(object.cursor) + 4U;
        return true;
    case 0x4294:
        services_.on_log("ignoring script cmd: starting fmv");
        return true;
    case 0x4295:
        return true;
    case 0x4296:
    case 0x4297:
        // Both PC handlers (0x004adc50 / 0x004adc30) are empty.
        (void)read_word(object);
        return true;
    case 0x4298: {
        std::uint16_t selector = read_word(object);
        if ((selector & 0x2000U) != 0) {
            selector = static_cast<std::uint16_t>(variable(object, selector));
        }
        services_.apply_camera_shake(object.position, selector == 0 ? 2U : (selector == 1 ? 1U : 0U));
        return true;
    }
    case 0x4299:
        (void)read_word(object);
        services_.on_log("ignoring script cmd: setting fmv");
        return true;
    case 0x429a:
    case 0x42a6: {
        if (opcode == 0x429a) {
            std::uint16_t node = read_word(object);
            if ((node & 0x2000U) != 0) {
                node = static_cast<std::uint16_t>(variable(object, node));
            }
            // The PC build decodes the node position and never uses it.
            (void)node;
        }
        const std::uint16_t type = read_word(object);
        if (type < 2) {
            return true;
        }
        if (type == 2) {
            (void)read_operand_word(object);
            object.cursor += 4U;
            return true;
        }
        assert_that(false, "Bad explosion type");
        return true;
    }
    case 0x429c: {
        const std::uint32_t at = object.cursor;
        const ScriptFlashEvent flash{
            static_cast<std::uint8_t>(peek_word(at)),
            static_cast<std::uint8_t>(peek_word(at + 2U)),
            static_cast<std::uint8_t>(peek_word(at + 4U)),
            peek_word(at + 6U),
            peek_word(at + 8U),
        };
        services_.on_flash(flash);
        object.cursor += 10U;
        return true;
    }
    case 0x429d:
        services_.on_log("ignoring script cmd: starting fr...");
        return true;
    case 0x429e:
        services_.on_camera_height(object.context, object.position[1]);
        return true;
    case 0x42a0: {
        std::uint16_t kind = read_word(object);
        if ((kind & 0x2000U) != 0) {
            kind = static_cast<std::uint16_t>(variable(object, kind));
        }
        const std::uint16_t word0 = read_word(object);
        const std::uint16_t word1 = read_word(object);
        const std::uint16_t lifetime = read_word(object);
        (void)services_.create_powerup(ScriptPowerupRequest{kind, object.position, word0, word1, lifetime});
        return true;
    }
    case 0x42a2:
        services_.on_global_word_42a2(peek_word(object.cursor));
        object.cursor += 2U;
        return true;
    case 0x42a3:
        assert_that(false, "C_PLAY_XA not supported");
        return true;
    case 0x42b1:
    case 0x42b2:
    case 0x42b3:
    case 0x42b4: {
        std::uint16_t node = read_word(object);
        if ((node & 0x2000U) != 0) {
            node = static_cast<std::uint16_t>(variable(object, node));
        }
        if (opcode == 0x42b1) {
            services_.signal_node_links(node);
        } else if (opcode == 0x42b2) {
            services_.execute_node_links(node);
        } else if (opcode == 0x42b3) {
            services_.signal_objects_at_node(node);
        } else {
            services_.send_pulse(node);
        }
        return true;
    }
    case 0x42c0: {
        const std::uint16_t argument = read_word(object);
        if (services_.game_mode_selector() != 1) {
            return true;
        }
        if (services_.career_got_goal_type(4) || services_.video_playing()) {
            return true;
        }
        services_.on_goal_event(argument);
        return true;
    }
    case 0x450a:
        assert_that(false, "C_PLAY_POSITIONAL_XA not supported");
        return true;
    default:
        unknown_command("Unknown script command");
        return true;
    }
}

ScriptRunStats ObjectScriptVm::run(ScriptObject& object, std::uint32_t cursor) {
    ScriptRunStats stats;
    const std::size_t assertions_before = assertions_;
    object.cursor = cursor;
    while (true) {
        if (peek_word(object.cursor) == kOpEnd) {
            object.active = 0;
            stats.finished = true;
            break;
        }
        const std::uint32_t opcode_address = object.cursor;
        const std::uint16_t opcode = read_word(object);
        if (trace_) {
            trace_(opcode_address, opcode);
        }
        assert_that((opcode & 0x6000U) != 0, "Bad script command");
        ++stats.commands;
        if (stats.commands > kMaxCommandsPerRun) {
            throw FormatError("script exceeded the per-frame command budget");
        }
        if ((opcode & 0x4000U) != 0) {
            if (!execute(object, opcode)) {
                stats.yielded = true;
                break;
            }
        } else if ((opcode & 0x2000U) != 0) {
            set_field(object, opcode);
        }
    }
    stats.assertions = assertions_ - assertions_before;
    return stats;
}

void ObjectScriptVm::die(ScriptObject& object) {
    if ((object.list_flags & 0x40U) != 0) {
        return;
    }
    // CBaddy__SendDeathPulse
    if (object.death_pulse_sent == 0 && object.node != 0xffff) {
        object.death_pulse_sent = 1;
        services_.execute_node_links(object.node);
    }
    object.list_flags = static_cast<std::uint16_t>(object.list_flags | 0x40U);
    object.state = static_cast<std::uint16_t>(object.state | 0x41U);
}

ScriptRunStats ObjectScriptVm::update(ScriptObject& object, std::uint32_t frame_delta) {
    ScriptRunStats stats;
    object.state = static_cast<std::uint16_t>(object.state | 0x41U);
    const std::uint16_t event = object.event_word;
    object.signal_latch = event;
    if ((event & 1U) != 0) {
        ++object.byte_170;
        object.event_word = static_cast<std::uint16_t>(event & 0xfffeU);
    }
    if (object.active == 0) {
        die(object);
        return stats;
    }
    if (object.wait != 0) {
        object.wait = object.wait > frame_delta ? object.wait - frame_delta : 0U;
        return stats;
    }
    return run(object, object.cursor);
}

} // namespace opentony::trg
