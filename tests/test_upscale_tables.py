"""The texture upscaler's data compiled into the launcher (src/launcher/thpsource2/core/upscale_tables.inc) is what re/config says (scripts/make_upscale_tables.py)."""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def test_upscale_tables_current():
    result = subprocess.run([sys.executable, str(ROOT / "scripts" / "make_upscale_tables.py"), "--check"], capture_output=True, text=True, cwd=ROOT, check=False)
    assert result.returncode == 0, result.stderr


def test_class_table_is_written_back_byte_for_byte():
    """The upscaled set's stamp holds the class table's hash: the lines compiled in must join back into the file exactly."""
    text = (ROOT / "re" / "config" / "texture_classes.tsv").read_text()
    inc = (ROOT / "src" / "launcher" / "thpsource2" / "core" / "upscale_tables.inc").read_text()
    rows = inc.split("constexpr const char* kClassRows[] = {\n", 1)[1].split("\n};", 1)[0].splitlines()
    lines = [row.strip()[1:-2].replace('\\"', '"').replace("\\\\", "\\") for row in rows]
    assert "\n".join(lines) + "\n" == text
