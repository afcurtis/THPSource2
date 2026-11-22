import subprocess
import sys

import pytest

from tony import hooks


@pytest.mark.skipif(not hooks._canonical_exe().is_file(), reason="canonical executable not extracted")
def test_trick_tip_table_matches_canonical_executable():
    result = subprocess.run(
        [sys.executable, "scripts/generate_trick_tip_table.py", "--check"],
        capture_output=True,
        text=True, check=False,
    )
    assert result.returncode == 0, result.stderr
