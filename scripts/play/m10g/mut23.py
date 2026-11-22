"""mut23.py: mutants of Panel::apply_gap against hud-calls-gapA (each must make the replay report a mismatch)."""
import pathlib
import subprocess

SRC = pathlib.Path("src/runtime/hud_panel.cpp")
FIX = "re/evidence/fixtures/hud-calls-gapA.txt"
MUTANTS = [
    ("gate", "    if (get(skater + 0x3064) == 0) {\n        return;\n    }\n    if (get(kGameMode) == 7) {", "    if (get(skater + 0x3064) != 0) {\n        return;\n    }\n    if (get(kGameMode) == 7) {"),
    ("mode7", "if (get(kGameMode) == 7) {\n        put(skater + 0x2cc8, 0);\n    }\n    std::uint32_t positive", "if (get(kGameMode) == 6) {\n        put(skater + 0x2cc8, 0);\n    }\n    std::uint32_t positive"),
    ("mode7clear", "put(skater + 0x2cc8, 0);\n    }\n    std::uint32_t positive", "put(skater + 0x2cc8, 1);\n    }\n    std::uint32_t positive"),
    ("negedge", "    if (value < 0) {\n        value = wrap(", "    if (value <= 0) {\n        value = wrap("),
    ("negate", "value = wrap(0u - uns(value));\n        positive = 0;", "value = value;\n        positive = 0;"),
    ("positive", "        positive = 0;\n    }\n    if (get(skater + 0x2dd4) != 0) {", "        positive = 1;\n    }\n    if (get(skater + 0x2dd4) != 0) {"),
    ("pending", "    if (get(skater + 0x2dd4) != 0) {\n        // a trick left pending by a script: drop its script flags (as `land_pending` does, but the state stays set)", "    if (get(skater + 0x2dd4) == 0) {\n        // a trick left pending by a script: drop its script flags (as `land_pending` does, but the state stays set)"),
    ("fa0", "put(skater + 0x2f24, 0xfa0);", "put(skater + 0x2f24, 0xfa1);"),
    ("29e0", "put(skater + 0x2f24, 0xfa0);\n        put(skater + 0x29e0, 0);", "put(skater + 0x2f24, 0xfa0);\n        put(skater + 0x29e0, 1);"),
    ("count", "        for (std::uint32_t i = 0; i < 0x20; ++i) {\n            m_.set8(skater + 0x2a14 + 8 * i, 0);\n        }\n    }\n    push_entry", "        for (std::uint32_t i = 0; i < 0x1f; ++i) {\n            m_.set8(skater + 0x2a14 + 8 * i, 0);\n        }\n    }\n    push_entry"),
    ("stride", "        for (std::uint32_t i = 0; i < 0x20; ++i) {\n            m_.set8(skater + 0x2a14 + 8 * i, 0);\n        }\n    }\n    push_entry", "        for (std::uint32_t i = 0; i < 0x20; ++i) {\n            m_.set8(skater + 0x2a14 + 4 * i, 0);\n        }\n    }\n    push_entry"),
    ("flag", "push_entry(skater + kPanelInSkater, name, value, kind, positive, extra);", "push_entry(skater + kPanelInSkater, name, value, kind, 1, extra);"),
    ("kind", "push_entry(skater + kPanelInSkater, name, value, kind, positive, extra);", "push_entry(skater + kPanelInSkater, name, value, 0, positive, extra);"),
    ("extra", "push_entry(skater + kPanelInSkater, name, value, kind, positive, extra);", "push_entry(skater + kPanelInSkater, name, value, kind, positive, 0);"),
    ("panel", "push_entry(skater + kPanelInSkater, name, value, kind, positive, extra);", "push_entry(skater, name, value, kind, positive, extra);"),
]

original = SRC.read_text()
try:
    for name, old, new in MUTANTS:
        if original.count(old) != 1:
            print(f"{name}: pattern occurs {original.count(old)} times")
            continue
        SRC.write_text(original.replace(old, new))
        build = subprocess.run(["bash", "build/p410b/hud_build.sh"], capture_output=True, text=True)
        if build.returncode != 0:
            print(f"{name}: BUILD FAILED {build.stderr[:200]}")
            continue
        run = subprocess.run(["timeout", "120", "build/p410b/hud_test", FIX], capture_output=True, text=True)
        last = [line for line in (run.stdout + run.stderr).splitlines() if "cases replayed" in line]
        print(f"{name}: {'KILLED' if run.returncode != 0 or ' 0 mismatches' not in (last[0] if last else '') else 'SURVIVED'} {last[0] if last else run.returncode}")
finally:
    SRC.write_text(original)
    subprocess.run(["bash", "build/p410b/hud_build.sh"], capture_output=True)
print("MUT23DONE")
