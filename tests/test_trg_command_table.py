import subprocess
import sys
from pathlib import Path

import pytest

from tony.assets import inspect_trg

CORPUS = Path("build/assets/all-pkr/files/data")
# Dead PSX-format 0x8d leftovers that the PC dispatcher cannot tokenise.
KNOWN_DEAD_NODES = {
    ("SKATE_T.TRG", 48), ("SKATE_T.TRG", 49), ("SKATE_T.TRG", 50),
    ("SKPARK_T.TRG", 48), ("SKPARK_T.TRG", 49), ("SKPARK_T.TRG", 50),
}


def test_command_table_document_is_current():
    result = subprocess.run(
        [sys.executable, "scripts/generate_trg_command_table.py", "--check"],
        capture_output=True,
        text=True, check=False,
    )
    assert result.returncode == 0, result.stderr


@pytest.mark.skipif(
    not CORPUS.is_dir() or not any(CORPUS.glob("*.TRG")),
    reason="shipped TRG files not extracted",
)
def test_shipped_trg_streams_decode_except_known_dead_nodes():
    from tony import assets

    known = set(assets._TRG_COMMAND_NAMES) | assets.TRG_NO_OPERAND_OPCODES
    known |= assets.TRG_ONE_OPERAND_OPCODES | assets.TRG_TWO_OPERAND_OPCODES
    known |= assets.TRG_STRING_OPCODES | {0x68, 0x85, 0x8D, 0xAB, 0xC9}
    files = sorted(CORPUS.glob("*.TRG"))
    assert len(files) == 32
    undecodable = set()
    for path in files:
        for script in inspect_trg(path, include_scripts=True)["scripts"]:
            if not script["terminated"] or script.get("decode_error"):
                undecodable.add((path.name, script["node"]))
            for command in script["commands"]:
                if (path.name, script["node"]) in KNOWN_DEAD_NODES:
                    continue
                assert command["opcode"] in known, (path.name, script["node"], command)
    assert undecodable == KNOWN_DEAD_NODES
