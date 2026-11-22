"""Cross-check OpenTony's ALL.PKR extraction against the independent thps2-tools extractor.

`extract-pkr.py` (JayFoxRox, MIT; pinned as `thps2_tools` in re/xref/config.yml) is a second
implementation of the PKR2 reader. Both must produce the same 3,771 files byte for byte.
Skips unless the disc extraction, the extracted tree and the pinned reference are present
(`tony xref fetch-references thps2_tools`).
"""

import hashlib
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
PKR = ROOT / "build/disc/files/SETUP/data/ALL.PKR"
EXTRACTED = ROOT / "build/assets/all-pkr/files"
REFERENCE = ROOT / "build/xref/references/thps2_tools"


def digests(root: Path) -> dict[str, str]:
    return {
        path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


@pytest.mark.skipif(
    not (PKR.is_file() and EXTRACTED.is_dir() and (REFERENCE / "extract-pkr.py").is_file()),
    reason="needs the extracted disc, the extracted PKR tree and the pinned thps2-tools reference",
)
def test_all_pkr_matches_thps2_tools_extractor(tmp_path):
    result = subprocess.run(
        [sys.executable, str(REFERENCE / "extract-pkr.py"), str(PKR)],
        cwd=tmp_path,
        capture_output=True,
        text=True,
        check=True,
    )
    assert "3771 / 3771 files known" in result.stdout

    independent = digests(tmp_path / "out")
    ours = digests(EXTRACTED)
    assert len(independent) == 3771
    assert independent.keys() == ours.keys()
    assert [name for name in independent if independent[name] != ours[name]] == []
