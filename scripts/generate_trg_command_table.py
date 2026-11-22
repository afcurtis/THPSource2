#!/usr/bin/env python3
"""Regenerate the opcode tables in re/evidence/functions/trg-command-table.md.

Usage: python scripts/generate_trg_command_table.py [--check]

The document holds two generated blocks between HTML-comment markers: the
static opcode table (operand shapes come from tony/assets.py, which mirrors the
retail dispatcher at 0x004c5dc0) and the corpus counts (which need the
extracted shipped .TRG files). `--check` exits non-zero when a block is stale;
the corpus block is skipped when the assets are not extracted.
"""

from __future__ import annotations

import collections
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tony import assets  # noqa: E402

DOC = ROOT / "re" / "evidence" / "functions" / "trg-command-table.md"
CORPUS = ROOT / "build" / "assets" / "all-pkr" / "files" / "data"

# Names used by the independent thps2-tools TRG disassembler (MIT).
REFERENCE_NAMES = {
    0x02: "SetCheatRestarts", 0x03: "SendPulse", 0x04: "SendActivate",
    0x05: "SendSuspend", 0x0A: "SendSignal", 0x0B: "SendKill",
    0x0C: "SendKillLoudly", 0x0D: "SendVisible", 0x68: "SetFoggingParams",
    0x7E: "SpoolIn", 0x80: "SpoolIn", 0x82: "SetCamAngle",
    0x83: "BackgroundOn", 0x84: "BackgroundOff", 0x86: "SetInitialPulses",
    0x87: "SetCamDistXZ", 0x8C: "SetRestart", 0x8D: "SetVisibilityInBox",
    0x8E: "SetObjFile", 0x8F: "SetCamDistY", 0x90: "SetCamOffsetX",
    0x91: "SetCamOffsetY", 0x92: "SetCamOffsetZ", 0x93: "SetGameLevel",
    0x95: "Endif", 0x97: "SetDualBufferSize", 0x98: "KillBruce",
    0x99: "SetCamColijSide", 0x9B: "MIDIFadeIn", 0x9D: "SetReverbType",
    0x9E: "EndLevel", 0xA6: "SetOTPushback", 0xA7: "SetCamZoom",
    0xA9: "SetOTPushback2", 0xAB: "BackgroundCreate", 0xB2: "SetRestart2",
    0xC8: "SetFadeColor", 0xCA: "SetSkyColor", 0xCB: "SetCareerFlag",
    0xCC: "IfCareerFlag", 0xC9: "GapPolyHit",
}

# What the native TriggerRuntime::dispatch switch does with each opcode.
NATIVE = {
    0x02: "on_cheat_restart_strings",
    0x03: "pulse_links (budgeted by command-point state)",
    0x04: "on_suspend_activate (source links)",
    0x05: "on_suspend_activate (source links)",
    0x0A: "on_signal (source links)",
    0x0B: "on_kill (source links)",
    0x0C: "on_kill (source links)",
    0x0D: "on_visible",
    0x68: "pending fog, on_fog_range after the stream",
    0x69: "on_music",
    0x6A: "on_sound",
    0x73: "diagnostic: no longer supported",
    0x77: "string consumed, no service",
    0x79: "diagnostic: CamFollowPath unsupported",
    0x7A: "diagnostic: ClearOtherRegion unsupported",
    0x7E: "on_resource (mode 0)",
    0x7F: "on_resource (mode 2)",
    0x80: "on_resource (mode 1)",
    0x81: "on_flush_resources",
    0x83: "on_object_flag_by_id (clear)",
    0x84: "on_object_flag_by_id (set)",
    0x85: "on_fixed_path_records + on_legacy_command",
    0x86: "command-point initial pulse budget",
    0x8A: "diagnostic: SeekXA unsupported",
    0x8B: "diagnostic: PlayXA unsupported",
    0x8C: "on_restart_selected",
    0x8D: "on_fixed_path",
    0x8E: "on_competition_name",
    0x93: "on_initial_state",
    0x94: "conditional: pulse_count == operand",
    0x95: "endif",
    0x97: "on_timer (operand * 1000)",
    0x98: "on_kill_bruce_restart + restart application",
    0x99: "on_current_object_word",
    0x9A: "on_current_object_word",
    0x9D: "on_reverb_type",
    0x9E: "on_level_event_state",
    0xA0: "on_current_object_word",
    0xA2: "diagnostic: LoadAI unsupported + on_legacy_command",
    0xA3: "on_current_skater_word",
    0xA4: "on_current_object_word",
    0xA5: "on_current_object_word",
    0xA6: "on_global_word",
    0xA7: "on_current_object_pair",
    0xA8: "on_current_object_word",
    0xA9: "on_global_word",
    0xAA: "on_global_word",
    0xAB: "on_script_object",
    0xAC: "on_current_object_word",
    0xAD: "on_current_object_copy",
    0xB0: "on_restart_selected",
    0xB1: "on_current_skater_word",
    0xB2: "on_restart_selected (two-player mode only)",
    0xC8: "on_script_value",
    0xC9: "on_gap (checksum-checked) + optional source pulse",
    0xCA: "on_level_value",
    0xCB: "set_career_flag",
    0xCC: "conditional: career flag",
    0xCD: "conditional: goal complete",
}
LEGACY = "on_legacy_command (operands consumed, no state change)"

SPECIAL_SHAPES = {
    0x02: "NUL-terminated string list ending in an empty string",
    0x68: "3 x u16",
    0x85: "aligned 24-byte path records, u16 0x00ff terminator",
    0x8D: "2 x u16, then aligned 24-byte path records, u16 0x00ff terminator",
    0xAB: "aligned u32 key + 3 x u16",
    0xC9: "aligned u32 checksum + u16 divider",
}

STATIC_BEGIN = "<!-- BEGIN GENERATED: opcode-table -->"
STATIC_END = "<!-- END GENERATED: opcode-table -->"
CORPUS_BEGIN = "<!-- BEGIN GENERATED: corpus-counts -->"
CORPUS_END = "<!-- END GENERATED: corpus-counts -->"


def known_opcodes() -> list[int]:
    ops = set(assets._TRG_COMMAND_NAMES)
    ops |= assets.TRG_NO_OPERAND_OPCODES | assets.TRG_ONE_OPERAND_OPCODES
    ops |= assets.TRG_TWO_OPERAND_OPCODES | assets.TRG_STRING_OPCODES
    ops |= set(SPECIAL_SHAPES)
    return sorted(ops)


def shape(opcode: int) -> str:
    if opcode in SPECIAL_SHAPES:
        return SPECIAL_SHAPES[opcode]
    if opcode in assets.TRG_NO_OPERAND_OPCODES:
        return "none"
    if opcode in assets.TRG_ONE_OPERAND_OPCODES:
        return "u16"
    if opcode in assets.TRG_TWO_OPERAND_OPCODES:
        return "2 x u16"
    if opcode in assets.TRG_STRING_OPCODES:
        return "NUL-terminated string, 2-byte aligned"
    raise ValueError(f"no shape for {opcode:#x}")


def static_block() -> str:
    lines = [
        STATIC_BEGIN,
        "| opcode | OpenTony name | thps2-tools name | operand shape | native handling |",
        "| --- | --- | --- | --- | --- |",
    ]
    for opcode in known_opcodes():
        lines.append(
            "| 0x{:02x} | {} | {} | {} | {} |".format(
                opcode,
                assets._TRG_COMMAND_NAMES.get(opcode, "(unnamed)"),
                REFERENCE_NAMES.get(opcode, "-"),
                shape(opcode),
                NATIVE.get(opcode, LEGACY),
            )
        )
    lines.append(STATIC_END)
    return "\n".join(lines)


def corpus_available() -> bool:
    return CORPUS.is_dir() and any(CORPUS.glob("*.TRG"))


def corpus_block() -> str:
    occurrences: collections.Counter[int] = collections.Counter()
    nodes: collections.Counter[int] = collections.Counter()
    files: dict[int, set[str]] = collections.defaultdict(set)
    dead: list[str] = []
    streams = 0
    paths = sorted(CORPUS.glob("*.TRG"))
    for path in paths:
        for script in assets.inspect_trg(path, include_scripts=True)["scripts"]:
            streams += 1
            if not script["terminated"] or script.get("decode_error"):
                dead.append(f"{path.name} node {script['node']}")
            seen = set()
            for command in script["commands"]:
                opcode = command["opcode"]
                occurrences[opcode] += 1
                files[opcode].add(path.name)
                seen.add(opcode)
            nodes.update(seen)
    unknown = sorted(set(occurrences) - set(known_opcodes()))
    lines = [
        CORPUS_BEGIN,
        f"{len(paths)} shipped TRG files, {streams} command streams.",
        "",
        "| opcode | occurrences | streams | files |",
        "| --- | --- | --- | --- |",
    ]
    for opcode in sorted(occurrences):
        lines.append(
            f"| 0x{opcode:02x} | {occurrences[opcode]} | {nodes[opcode]} | {len(files[opcode])} |"
        )
    lines.append("")
    lines.append(
        "Opcodes seen in the corpus that the decoder does not know: "
        + (", ".join(f"0x{op:02x}" for op in unknown) or "none")
        + "."
    )
    lines.append(
        "Streams that do not decode to a `0xffff` terminator: "
        + (", ".join(dead) or "none")
        + "."
    )
    lines.append(CORPUS_END)
    return "\n".join(lines)


def replace_block(text: str, begin: str, end: str, block: str) -> str:
    start = text.index(begin)
    stop = text.index(end) + len(end)
    return text[:start] + block + text[stop:]


def main() -> int:
    check = "--check" in sys.argv[1:]
    text = DOC.read_text(encoding="utf-8")
    updated = replace_block(text, STATIC_BEGIN, STATIC_END, static_block())
    if corpus_available():
        updated = replace_block(updated, CORPUS_BEGIN, CORPUS_END, corpus_block())
    if check:
        if updated != text:
            print(f"{DOC} is stale; run scripts/generate_trg_command_table.py", file=sys.stderr)
            return 1
        return 0
    if updated != text:
        DOC.write_text(updated, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
