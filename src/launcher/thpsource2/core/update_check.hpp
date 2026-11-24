// THPSource2 launcher: the rules of the update notice (docs/LAUNCHER.md, "Updates"). Portable, no network and no Windows code, so it is tested natively.
//
// The launcher asks GitHub where `<repository>/releases/latest` redirects to (one HEAD request, the app does it with Windows' own curl.exe). GitHub answers with
// the page of the newest published release, `<repository>/releases/tag/<tag>`, and the tag is the version. This file turns that address into a version and says
// whether it is newer than the running launcher. Nothing here trusts the answer: an address of another repository, or a tag that is not a plain version number
// ("v0.8", "1.2.3"), is no answer, and the notice never opens an address taken from the network (the app opens `<repository>/releases/latest` itself).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace thpsource2::update {

// Up to four numeric parts, missing ones 0 ("v0.8" is 0.8.0.0, so it equals "0.8.0")
struct Version {
    std::array<int, 4> part = {};
    friend bool operator==(const Version&, const Version&) = default;
};

// "v0.8", "V1.2.3", "0.10": a leading v or V, then one to four numbers of at most six digits, separated by single dots. Anything else (a suffix such as "-rc1", a space, an
// empty part, a fifth number) is not a version: nullopt
std::optional<Version> parse_version(std::string_view text);

// -1, 0 or 1: numeric, part by part, so 0.10 is newer than 0.9
int compare(const Version& a, const Version& b);

// The tag of the release a redirect points at: `location` must be `<repository_url>/releases/tag/<tag>` (the repository's address compared without regard to case,
// as GitHub canonicalises it; a trailing newline or carriage return is dropped) and the tag a version. The tag as written ("v0.8"), or nullopt
std::optional<std::string> tag_from_redirect(std::string_view location, std::string_view repository_url);

// `latest_tag` is a version newer than `running` (both as written, "v0.8"); false when either is not a version
bool is_newer(std::string_view latest_tag, std::string_view running);

// ---- the release itself: its notes, its package, and the script that installs it (docs/LAUNCHER.md, "Updates") ----

// The release's zip and its checksum file, as `tony release package` and docs/BUILDING.md "Releasing" name them
inline constexpr const char* kPackageName = "thpsource2-win64.zip";
inline constexpr const char* kChecksumName = "thpsource2-win64.zip.sha256";

// What the launcher shows about a release. The notes are optional (the API may be out of reach: the update still installs)
struct Release {
    std::string tag;            // "v0.8"
    std::string notes;          // the release text with the Markdown marks taken out (plain_notes)
    std::uint64_t zip_size = 0; // bytes of kPackageName, 0 when not listed
};

// The GitHub API's answer for one release (`api_url`). It is data from the network: only the tag (which must be `expected_tag`), the text and the package's size are read,
// nothing in it is opened or downloaded. nullopt when it is not a release object of that tag
std::optional<Release> parse_release(std::string_view json, std::string_view expected_tag);

// Markdown to the plain text a launcher label can show: no `#`, `**`, backticks, quote marks or link addresses ("[text](url)" is "text"), "*" bullets as "-", curly
// quotes and dashes as plain ones, emoji dropped, runs of blank lines as one, at most `limit` bytes (cut at a line, "..." after)
std::string plain_notes(std::string_view markdown, std::size_t limit = 6000);

// The 64 hex digits (lower case) at the start of a `sha256sum` line ("<hash>  <file>"), when the line names `file`
std::optional<std::string> checksum_from_file(std::string_view text, std::string_view file);

// The addresses the launcher asks for, built from the repository's own address and a tag that is a version, never taken from an answer
std::string api_url(std::string_view repository_url, std::string_view tag);            // https://api.github.com/repos/<owner>/<repo>/releases/tags/<tag>
std::string asset_url(std::string_view repository_url, std::string_view tag, std::string_view file);   // <repository>/releases/download/<tag>/<file>

// The batch file that puts an unpacked release over the install: it sits in `<install>\Data\Update\` beside `unpack\thpsource2-win64\`, is run with the launcher's process id,
// waits for the launcher to close, then replaces Build\ and Licenses\ (mirrored: the release owns them), the launcher and README.md, and starts the launcher again.
// Config, Data, Saves, Mods, Screenshots and Logs are never touched. A failure writes `failed.txt` beside it and Logs\update.txt says why. Everything is
// relative to the script's own folder, so no path of the player's is in it; `tag` is only written into the log
std::string apply_script(std::string_view tag);

}  // namespace thpsource2::update
