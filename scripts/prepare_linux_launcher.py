"""Prepare a local Linux launcher install from the player's PC game files."""

import os
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tony.assets import extract_pkr
from tony.static_data import build_image


def prepare(executable: Path, package: Path, output: Path) -> None:
    image = build_image(executable)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT / "build", prefix="linux-prepare-") as temporary:
        work = Path(temporary)
        extract_pkr(package, work / "package")
        staged = work / "thawk2_static.img"
        staged.write_bytes(image)
        with tempfile.TemporaryDirectory(dir=output, prefix=".prepare-") as staging:
            ready = Path(staging)
            shutil.move(str(work / "package/files"), ready / "data")
            shutil.copyfile(staged, ready / staged.name)
            existing = output / "data"
            previous = ready / "previous"
            if existing.exists():
                existing.rename(previous)
            try:
                (ready / "data").rename(existing)
                os.replace(ready / staged.name, output / staged.name)
            except OSError:
                if existing.exists():
                    shutil.rmtree(existing)
                if previous.exists():
                    previous.rename(existing)
                raise
    print(f"Prepared game data in {output}")


if __name__ == "__main__":
    prepare(*(Path(argument) for argument in sys.argv[1:]))
