#include "render_command_dispatch.hpp"
#include "../runtime/seam_addresses_generated.hpp"

namespace opentony::trg {
namespace {

struct HandlerDescription {
    std::uint32_t address;
    RenderPrimitiveKind primitive;
    std::uint32_t vertex_count;
    bool textured;
};

[[nodiscard]] HandlerDescription handler_for(std::uint8_t opcode_base) {
    switch (opcode_base) {
    case 0x20:
        return {seam::hal::D3D_DrawSolidTriangle, RenderPrimitiveKind::SolidTriangle, 3, false};
    case 0x24:
        return {seam::hal::D3D_DrawTexturedTriangle, RenderPrimitiveKind::TexturedTriangle, 3, true};
    case 0x28:
        return {seam::hal::D3D_DrawSolidQuad, RenderPrimitiveKind::SolidQuad, 4, false};
    case 0x2c:
        return {seam::hal::D3D_DrawTexturedQuad, RenderPrimitiveKind::TexturedQuad, 4, true};
    case 0x30:
        return {seam::hal::D3D_DrawGouraudTriangle, RenderPrimitiveKind::GouraudTriangle, 3, false};
    case 0x34:
        return {
            seam::hal::D3D_DrawTexturedGouraudTriangle, RenderPrimitiveKind::TexturedGouraudTriangle, 3, true};
    case 0x38:
        return {seam::hal::D3D_DrawGouraudQuad, RenderPrimitiveKind::GouraudQuad, 4, false};
    case 0x3c:
        return {seam::hal::D3D_DrawTexturedGouraudQuad, RenderPrimitiveKind::TexturedGouraudQuad, 4, true};
    case 0x40:
        return {seam::hal::D3D_DrawLine, RenderPrimitiveKind::Line, 2, false};
    case 0x48:
        return {seam::hal::D3D_DrawLineStrip, RenderPrimitiveKind::LineStrip, 4, false};
    case 0x4c:
        return {seam::hal::D3D_DrawClosedLineStrip, RenderPrimitiveKind::ClosedLineStrip, 5, false};
    case 0x50:
        return {seam::hal::D3D_DrawColoredLine, RenderPrimitiveKind::ColoredLine, 2, false};
    case 0x60:
        return {seam::hal::D3D_DrawSolidRectangle, RenderPrimitiveKind::SolidRectangle, 4, false};
    case 0x68:
        return {0x004d6090, RenderPrimitiveKind::UnitRectangle, 4, false};
    case 0x70:
        return {0x004d60c0, RenderPrimitiveKind::Rectangle8, 4, false};
    case 0x78:
        return {0x004d60f0, RenderPrimitiveKind::Rectangle16, 4, false};
    case 0xb0:
        return {seam::hal::D3D_DrawGeneralPolygon, RenderPrimitiveKind::GeneralPolygon, 0, false};
    default:
        return {
            kRetailNoOpPolygonHandler, RenderPrimitiveKind::NoOp, 0, false};
    }
}

} // namespace

RenderDispatchRecord RenderCommandDispatcher::dispatch_one(
    const RenderCommandRecord& command,
    std::size_t command_index) {
    const std::uint8_t opcode_base =
        static_cast<std::uint8_t>(command.opcode & 0xfcU);
    const HandlerDescription handler = handler_for(opcode_base);

    RenderDispatchRecord result{
        command_index,
        command.polygon_index,
        command.enabled_or_payload,
        command.opcode,
        opcode_base,
        command.render_flags,
        handler.address,
        handler.primitive,
        handler.vertex_count,
        handler.textured,
        command.enabled_or_payload != 0,
        {
            static_cast<std::uint8_t>(command.opcode & 0x01U),
            (command.opcode & 0x02U) != 0,
            (command.opcode & 0x04U) != 0,
            (command.opcode & 0x10U) != 0,
        },
    };

    // The dispatcher checks the record's +0x04 word before invoking a
    // handler.  Preserve a one-for-one record for trace comparisons, but
    // make disabled and uninstalled slots explicit no-ops.
    if (!result.active || result.primitive == RenderPrimitiveKind::NoOp) {
        result.handler_address = kRetailNoOpPolygonHandler;
        result.primitive = RenderPrimitiveKind::NoOp;
        result.vertex_count = 0;
        result.textured = false;
        return result;
    }

    // The 0xb0 handler walks the variable vertex list at +0x28.  Its count
    // and textured mode are packet data, unlike the fixed primitive handlers.
    if (result.primitive == RenderPrimitiveKind::GeneralPolygon) {
        result.vertex_count = command.vertex_count;
        result.textured = command.textured;
    }
    return result;
}

std::vector<RenderDispatchRecord> RenderCommandDispatcher::dispatch(
    std::span<const RenderCommandRecord> commands) {
    std::vector<RenderDispatchRecord> result;
    result.reserve(commands.size());
    for (std::size_t index = 0; index < commands.size(); ++index) {
        result.push_back(dispatch_one(commands[index], index));
    }
    return result;
}

} // namespace opentony::trg
