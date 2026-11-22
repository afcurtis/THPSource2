import subprocess
import sys

import pytest

from tony import hooks


@pytest.mark.skipif(not hooks._canonical_exe().is_file(), reason="canonical executable not extracted")
def test_retail_gap_tables_match_canonical_executable():
    result = subprocess.run(
        [sys.executable, "scripts/generate_gap_tables.py", "--check"],
        capture_output=True,
        text=True, check=False,
    )
    assert result.returncode == 0, result.stderr
