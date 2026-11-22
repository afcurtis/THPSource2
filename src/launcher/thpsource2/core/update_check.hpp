// THPSource2 launcher: the rules of the update notice (docs/LAUNCHER.md, "Updates"). Portable, no network and no Windows code, so it is tested natively.
//
// The launcher asks GitHub where `<repository>/releases/latest` redirects to (one HEAD request, the app does it with Windows' own curl.exe). GitHub answers with
// the page of the newest published release, `<repository>/releases/tag/<tag>`, and the tag is the version. This file turns that address into a version and says
// whether it is newer than the running launcher. Nothing here trusts the answer: an address of another repository, or a tag that is not a plain version number
// ("v0.8", "1.2.3"), is no answer, and the notice never opens an address taken from the network (the app opens `<repository>/releases/latest` itself).
#pragma once

#include <array>
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

}  // namespace thpsource2::update
