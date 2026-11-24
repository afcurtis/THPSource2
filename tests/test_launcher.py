"""Milestone 12: the player's launcher (src/launcher/opentony_launcher.cpp) and the release folder (tony/release.py)."""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

from tony import release, static_data
from tony.common import load_yaml, wine_env

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/launcher/opentony_launcher.cpp").read_text()
LAUNCHER = ROOT / "build/hooks/kits64/opentony.exe"
EXE = ROOT / "build/disc/files/SETUP/data/THawk2.exe"
PKR = ROOT / "build/disc/files/SETUP/data/ALL.PKR"
DATA = ROOT / "build/assets/all-pkr/files"
WINE = os.environ.get("OPENTONY_WINE_TESTS") == "1"


def test_launcher_pins_the_canonical_build() -> None:
    """The launcher refuses any THawk2.exe but the one re/config/native_hooks.yml names; its copies of the pins must not drift."""

    want = load_yaml("re/config/native_hooks.yml")["builds"]["canonical"]
    assert re.search(r'kCanonicalSha256 = "([0-9a-f]{64})"', SOURCE).group(1) == want["sha256"]
    assert int(re.search(r"kTimestamp = (0x[0-9a-f]+)u", SOURCE).group(1), 16) == int(want["pe_timestamp"])
    assert int(re.search(r"kSizeOfImage = (0x[0-9a-f]+)u", SOURCE).group(1), 16) == int(want["size_of_image"])
    assert int(re.search(r"kStaticVersion = (\d+)", SOURCE).group(1)) == static_data.VERSION


def test_release_checksum_matches_the_zip(tmp_path: Path) -> None:
    """`tony release package` writes `<zip>.sha256` in `sha256sum -c` format, replacing any older one."""

    archive = tmp_path / "thpsource2-win64.zip"
    archive.write_bytes(b"not really a zip")
    (tmp_path / "thpsource2-win64.zip.sha256").write_text("0" * 64 + "  thpsource2-win64.zip\n")
    sidecar = release.write_checksum(archive)
    assert sidecar.name == "thpsource2-win64.zip.sha256"
    assert sidecar.read_bytes() == f"{hashlib.sha256(b'not really a zip').hexdigest()}  thpsource2-win64.zip\n".encode()


def test_release_lists_only_what_a_player_needs() -> None:
    assert release.PROGRAMS == ("THPSource2.exe", "opentony.exe", "opentony_spike.exe", "opentony_standalone.dll", "opentony_spike_video.dll")
    assert release.README_SOURCE.is_file()
    # the spike is the standalone's own program (linked at the retail base); the launcher is a separate executable, in both architectures
    cmake = (ROOT / "src/hooks/kits/CMakeLists.txt").read_text()
    assert "add_executable(opentony ../../launcher/opentony_launcher.cpp" in cmake
    assert "add_executable(THPSource2 WIN32" in cmake
    assert (ROOT / "src/launcher/thpsource2/LICENSE-PARTYMOD.txt").read_text().count("Copyright (c) 2024 PARTYMANX") == 1


def test_thpsource2_launcher_pins_the_same_build_and_imgui() -> None:
    """THPSource2.exe checks THawk2.exe against the same digest as opentony.exe; its ImGui is the pinned release."""

    want = load_yaml("re/config/native_hooks.yml")["builds"]["canonical"]["sha256"]
    core = (ROOT / "src/launcher/thpsource2/core/game_files.hpp").read_text()
    assert re.search(r'kCanonicalSha256 = "([0-9a-f]{64})"', core).group(1) == want
    pinned = load_yaml("re/config/imgui.yml")["imgui"]
    assert pinned["url"].endswith(f"v{pinned['release']}.tar.gz") and pinned["root"] == f"imgui-{pinned['release']}"
    assert re.fullmatch(r"[0-9a-f]{64}", pinned["sha256"]) and int(pinned["size"]) > 1_000_000
    notices = (ROOT / "THIRD_PARTY_NOTICES.md").read_text()
    assert pinned["sha256"] in notices and "## PARTYMOD" in notices and "## Dear ImGui" in notices


def test_launcher_environment_is_read_by_the_standalone() -> None:
    """The display variables the launcher sets (core/settings.cpp launch_environment) are the ones the standalone reads."""

    settings = (ROOT / "src/launcher/thpsource2/core/settings.cpp").read_text()
    sources = (ROOT / "src/hooks/kits/standalone_video.cpp").read_text() + (ROOT / "src/hooks/kits/standalone_platform.cpp").read_text()
    for name in ("OT_WINDOW_MODE", "OT_WINDOW_SIZE", "OT_VSYNC", "OT_WINDOW_TITLE", "OT_PAD_STICK"):
        assert f'"{name}"' in settings and f'std::getenv("{name}")' in sources, name


def test_debug_mode_switch_reaches_opentony_exe() -> None:
    """THPSource2's Debug mode sets OPENTONY_DEBUG; opentony.exe reads it, and only then dumps textures and keeps every run."""

    settings = (ROOT / "src/launcher/thpsource2/core/settings.cpp").read_text()
    assert '"OPENTONY_DEBUG"' in settings and '"DebugMode"' in settings
    assert 'getenv("OPENTONY_DEBUG")' in SOURCE
    assert 'if (debug) {\n        set_default("OT_TEXTURE_DUMP"' in SOURCE.replace("\r\n", "\n")
    assert "debug ? std::ios::app : std::ios::trunc" in SOURCE


def test_launcher_links_and_update_check_agree_with_the_repository() -> None:
    """The bug report form, the release page and the update check all hang off one repository address; the issue template and the README name it too."""

    app = (ROOT / "src/launcher/thpsource2/app/main.cpp").read_text()
    repo = re.search(r'kRepoUrl = "([^"]+)"', app).group(1)
    assert re.search(r'kIssueUrl = "([^"]+)"', app).group(1) == f"{repo}/issues/new?template=bug.md"
    assert re.search(r'kLatestReleaseUrl = "([^"]+)"', app).group(1) == f"{repo}/releases/latest"
    assert (ROOT / ".github/ISSUE_TEMPLATE/bug.md").is_file()   # the form kIssueUrl names
    readme = (ROOT / "README.md").read_text()
    assert f"{repo}/issues/new?template=bug.md" in readme and f"{repo}/releases/latest" in readme
    # the notice's version is the one the release workflow tags: "v" and numbers only, as core/update_check.hpp reads it
    assert re.fullmatch(r"v\d{1,6}(\.\d{1,6}){0,3}", re.search(r'kVersion = "([^"]+)"', app).group(1))
    # the request: HEAD of <repo>/releases/latest, HTTPS only, not followed, with a time limit
    task = (ROOT / "src/launcher/thpsource2/app/update_task.cpp").read_text()
    for flag in ('"--head"', '"--proto"', '"=https"', '"--max-time"', '"%{redirect_url}"', '"/releases/latest"'):
        assert flag in task, flag
    check, install = task.split("// ---- the install ----")
    assert '"--location"' not in check and '"-L"' not in check   # the check reads the redirect, it never follows it
    # the install follows redirects (a release asset is served from another host) but only over https, builds its addresses from the repository and the tag, and
    # installs nothing before the package's size, its SHA-256 against the release's checksum file and its contents have been checked
    for needed in ('"--location"', '"--proto-redir"', '"=https"', "update::asset_url(repository_url, release.tag", "checksum_from_file", "sha256_file(zip) != expected",
                   "release.zip_size", '"THPSource2.exe"', "start_batch"):
        assert needed in install, needed
    assert install.index("sha256_file(zip) != expected") < install.index('"-xf"') < install.index("update::apply_script") < install.index("State::Ready")
    assert "browser_download_url" not in task   # an address from the API's answer is never followed


def test_release_reports_what_is_missing(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(release, "build_dir", lambda arch: tmp_path)
    with pytest.raises(release.ReleaseError, match="THPSource2.exe"):
        release.package(64, zip_it=False)


def _fake_release(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """layout_files with files made up, so the layout can be checked without a build."""

    names = ["THPSource2.exe", "README.md", "Build/opentony.exe", "Build/SDL3.dll", "Licenses/THIRD_PARTY_NOTICES.md", "Licenses/zlib-LICENSE"]
    files = {}
    for name in names:
        path = tmp_path / "src" / name.replace("/", "__")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(name)
        files[name] = path
    monkeypatch.setattr(release, "layout_files", lambda arch: files)


def test_install_folder_is_the_launcher_a_readme_and_folders(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """The root holds only the launcher and README.md; a second install replaces Build and Licenses and leaves the player's files alone."""

    _fake_release(tmp_path, monkeypatch)
    target = tmp_path / "install"
    release.install(64, target)
    assert sorted(p.name for p in target.iterdir() if p.is_file()) == ["README.md", "THPSource2.exe"]
    assert sorted(p.name for p in target.iterdir() if p.is_dir()) == ["Build", "Config", "Data", "Licenses", "Logs", "Mods", "Saves", "Screenshots"]
    assert (target / "Data" / "Game").is_dir() and not any((target / "Data" / "Game").iterdir())
    (target / "Saves" / "THPS2_A.SAV").write_text("save")
    (target / "Config" / "THPSource2.ini").write_text("[Window]\n")
    (target / "Data" / "Unpacked").mkdir()
    (target / "Build" / "stale.dll").write_text("old")
    (target / "THPSource2_Launcher.exe").write_text("the launcher's former name")
    release.install(64, target)
    assert (target / "Saves" / "THPS2_A.SAV").read_text() == "save" and (target / "Config" / "THPSource2.ini").is_file() and (target / "Data" / "Unpacked").is_dir()
    assert not (target / "Build" / "stale.dll").exists() and (target / "Build" / "opentony.exe").is_file()
    assert not (target / "THPSource2_Launcher.exe").exists() and (target / "THPSource2.exe").is_file()


def test_install_layout_names_match_the_programs(tmp_path: Path) -> None:
    """tony/release.py and src/launcher/layout.hpp (what the launcher and opentony.exe read) name the same folders."""

    header = (ROOT / "src/launcher/layout.hpp").read_text()
    folders = set(re.findall(r'root / "([A-Za-z]+)"', header)) | set(re.findall(r'data\(\) / "([A-Za-z]+)"', header))
    assert {"Build", "Config", "Data", "Saves", "Mods", "Screenshots", "Logs", "Licenses"} <= folders
    assert {f.split("/")[0] for f in release.PLAYER_FOLDERS} | set(release.OWNED_FOLDERS) == {"Build", "Config", "Data", "Saves", "Mods", "Screenshots", "Logs", "Licenses"}
    assert 'config() / "THPSource2.ini"' in header and 'data() / "Game"' in header and 'data() / "Unpacked"' in header


def test_game_files_are_copied_whatever_their_case(tmp_path: Path) -> None:
    source = tmp_path / "installed"
    (source / "music").mkdir(parents=True)
    (source / "MED" / "movies").mkdir(parents=True)
    (source / "thawk2.EXE").write_bytes(b"exe")
    (source / "all.pkr").write_bytes(b"pkr")
    (source / "music" / "LTIX00.dat").write_bytes(b"m")
    (source / "MED" / "movies" / "intro.bik").write_bytes(b"b")
    game = tmp_path / "Game"
    assert sorted(release.copy_game_files(source, game)) == ["ALL.PKR", "Music/LTIX00.dat", "THawk2.exe", "med/Movies/intro.bik"]
    assert (game / "THawk2.exe").read_bytes() == b"exe" and (game / "med" / "Movies" / "intro.bik").is_file()
    assert release.copy_game_files(source, game) == []   # already there: nothing is copied again
    with pytest.raises(release.ReleaseError, match="ALL.PKR"):
        (source / "all.pkr").unlink()
        release.copy_game_files(source, tmp_path / "other")


def test_players_files_are_answered_from_their_folders() -> None:
    """Every host call that names a file reaches the redirect of saves, parks and TH2_OPT.CFG, and opentony.exe sets the folders it redirects to."""

    userdata = (ROOT / "src/hooks/kits/standalone_userdata.hpp").read_text()
    assert "OPENTONY_SAVES_DIR" in userdata and "OPENTONY_CONFIG_DIR" in userdata
    assert "return user_path(path);" in (ROOT / "src/hooks/kits/standalone_host.cpp").read_text()
    assert "return user_path(path);" in (ROOT / "src/hooks/kits/standalone_files.cpp").read_text()
    for name in ("OPENTONY_SETTINGS", "OPENTONY_SAVES_DIR", "OPENTONY_CONFIG_DIR", "OPENTONY_MODS"):
        assert f'set_default("{name}"' in SOURCE


@pytest.mark.skipif(not (WINE and LAUNCHER.is_file() and EXE.is_file() and PKR.is_file() and DATA.is_dir()), reason="needs a built launcher, the disc's files and OPENTONY_WINE_TESTS=1")
def test_first_run_writes_the_same_static_data_and_files_as_the_python_tools(tmp_path: Path) -> None:
    install = tmp_path / "install"
    game = install / "Data" / "Game"
    game.mkdir(parents=True)
    (game / "THawk2.exe").symlink_to(EXE)
    (game / "ALL.PKR").symlink_to(PKR)
    root = subprocess.run(["winepath", "-w", str(install)], capture_output=True, text=True, check=True).stdout.strip()
    result = subprocess.run(["wine", str(LAUNCHER), "--root", root, "--prepare"], env=wine_env(), capture_output=True, text=True, timeout=600, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    unpacked = install / "Data" / "Unpacked"
    assert (unpacked / "thawk2_static.img").read_bytes() == static_data.build_image(EXE)
    for source in DATA.rglob("*"):
        if source.is_file():
            copy = unpacked / "data" / source.relative_to(DATA)
            assert copy.is_file() and copy.stat().st_size == source.stat().st_size, source
    shutil.rmtree(unpacked)
    # a THawk2.exe of no supported build is refused with the reason
    (game / "THawk2.exe").unlink()
    (game / "THawk2.exe").write_bytes(b"MZ" + bytes(1000))
    refused = subprocess.run(["wine", str(LAUNCHER), "--root", root, "--prepare"], env=wine_env(), capture_output=True, text=True, timeout=600, check=False)
    assert refused.returncode != 0 and "not" in (refused.stdout + refused.stderr)


VARIANT_EXE = ROOT / "game/variants/original-2000/THawk2.exe"
PROTECTED_EXE = ROOT / "build/variants/original-2000-disc/files/Setup/data/THawk2.exe"


@pytest.mark.skipif(not (WINE and LAUNCHER.is_file() and VARIANT_EXE.is_file() and PKR.is_file()), reason="needs a built launcher, the original 2000 THawk2.exe and OPENTONY_WINE_TESTS=1")
def test_first_run_of_the_original_2000_release_writes_the_rebuilt_static_data(tmp_path: Path) -> None:
    """opentony.exe rebuilds the same image tony/static_variant.py does (the canonical one, the CD check's key zeroed); the disc's protected exe is refused."""

    from tony import static_variant

    install = tmp_path / "install"
    game = install / "Data" / "Game"
    game.mkdir(parents=True)
    (game / "THawk2.exe").symlink_to(VARIANT_EXE)
    (game / "ALL.PKR").symlink_to(PKR)
    root = subprocess.run(["winepath", "-w", str(install)], capture_output=True, text=True, check=True).stdout.strip()
    result = subprocess.run(["wine", str(LAUNCHER), "--root", root, "--prepare"], env=wine_env(), capture_output=True, text=True, timeout=600, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    image = (install / "Data" / "Unpacked" / "thawk2_static.img").read_bytes()
    assert image == static_data.build_image(VARIANT_EXE)
    assert hashlib.sha256(image).hexdigest() == static_variant.load_recipe()["image_sha256"]
    if PROTECTED_EXE.is_file():
        (game / "THawk2.exe").unlink()
        (game / "THawk2.exe").symlink_to(PROTECTED_EXE)
        refused = subprocess.run(["wine", str(LAUNCHER), "--root", root, "--prepare", "--force"], env=wine_env(), capture_output=True, text=True, timeout=600, check=False)
        assert refused.returncode == 2 and "copy-protected" in refused.stdout + refused.stderr
