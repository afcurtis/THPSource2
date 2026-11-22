// THPSource2 launcher: the texture upscaler's pipeline (texture-upscaling plan, T4; docs/LAUNCHER.md, "Upscaled textures"). Portable: the model is a function
// the caller passes (the app runs upscayl-bin over a folder, the native test a stub), PNGs go through png.hpp.
//
// Per texture of the dump (Data\TextureDump\<key>.png + index.tsv), by its class from the class table (re/config/texture_classes.tsv):
//   select (world textures under 16 a side are left out; 2D ones never are; keys already done under the same stamp.txt are skipped, so a stopped run resumes),
//   bleed (transparent texels take their neighbours' colours, so the model never sees the key colour beside an edge),
//   pad (16 texels a side, the texture tiled when it is drawn wrapped or the wrap is unknown, edge-repeated when it is clamped: no seams on tiled ground),
//   upscale (one model run a class and batch, colour as RGB; a world texture's alpha as a grey picture through the same model, or bicubic),
//   crop, 2x as the mean of 2 x 2 of the 4x result, colour match (each original texel's area keeps the original's mean: models drift in brightness and hue),
//   2D alpha by nearest (a glyph keeps its exact outline), and write Data\Upscaled\<key>.png (RGB when the original is opaque) and stamp.txt.
#pragma once

#include "png.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace thpsource2::upscale {

using png::Image;

enum class Class : std::uint8_t { World, TwoD };
enum class Wrap : std::uint8_t { Unknown, Wrap, Clamp, Both };
enum class Alpha : std::uint8_t { Opaque, Cutout, Smooth };
enum class AlphaMethod : std::uint8_t { Model, Bicubic };

inline constexpr const char* kNearest = "nearest";   // the built-in method: nearest neighbour, no tool (the original look, crisp at any size)
inline constexpr const char* kNone = "none";         // leave the class out
inline constexpr int kPipelineVersion = 1;

// ---- image steps, each on its own (tested one by one) ----
bool has_transparency(const Image& image);
Image bleed(const Image& image);                          // alpha 0 texels take the mean colour of their filled 3 x 3 neighbours, pass after pass; alpha kept
Image pad(const Image& image, int border, bool wrap);     // tiled (wrap) or edge-repeated border
Image crop(const Image& image, int border);
Image nearest(const Image& image, int scale);
Image bicubic(const Image& image, int scale);             // Catmull-Rom, edge-clamped (pad first for context)
Image box_down(const Image& image, int factor);           // the mean of each factor x factor block, all four channels
void match_colour(Image& upscaled, const Image& original, int scale);   // RGB: each scale x scale block's mean made the original texel's (twice, clamped)
Image alpha_as_grey(const Image& image);                  // alpha into R, G and B; alpha 255

// ---- the dump and the table ----
struct DumpRow {
    int width = 0, height = 0;
    Alpha alpha = Alpha::Opaque;
    Wrap wrap = Wrap::Unknown;
    bool is_volatile = false;
};
bool read_index(const std::filesystem::path& index, std::map<std::string, DumpRow>& rows, std::string& error);
bool read_classes(const std::filesystem::path& table, std::map<std::string, Class>& classes, std::uint64_t& table_hash, std::string& error);

struct Options {
    int scale = 4;                                   // 2 or 4 (models are 4x; 2x is the 4x result averaged down)
    // the person's pick (T4.5, 2026-11-19; re/config/upscaler.yml `chosen`): UltraSharp for the world, the original pixels for the HUD, the model's alpha
    std::string model_world = "ultrasharp-4x";       // a model name, kNearest or kNone
    std::string model_2d = kNearest;
    AlphaMethod alpha = AlphaMethod::Model;          // world textures with alpha
    int border = 16;
    int min_world = 16;                              // world textures with a side under this are left out (the backend draws the original)
    int batch = 400;                                 // textures a model run (a stop loses at most one batch)
    std::vector<std::string> keys;                   // only these keys (empty: every key of the table)
    // when set: a <key>.png here of the dump's size is the original instead of the dump's (graphics-2 G5: the PC port's 24-bit bitmaps in all 8 bits, Data\TrueColour),
    // so the model and the colour match start from the full colour, not the 15-bit surface. A stamp line says so: a set made from the dump alone is another set
    std::filesystem::path originals;
};

// Runs model `name` over every PNG of `input`, writing a 4x PNG of the same name into `output`. False with `error` when it failed.
using ModelRunner = std::function<bool(const std::string& name, const std::filesystem::path& input, const std::filesystem::path& output, std::string& error)>;

std::string stamp_text(const Options& options, const std::string& tool, std::uint64_t table_hash);

struct Report {
    int in_table = 0;
    int selected = 0;        // to upscale in this run
    int done_before = 0;     // already in the folder under the same stamp
    int small = 0;           // world, a side under min_world
    int not_dumped = 0;      // in the table, no PNG in the dump
    int left_out = 0;        // the class's method is kNone
    int written = 0;
    int failed = 0;
    std::uintmax_t bytes = 0;   // of the PNGs written in this run
    bool cancelled = false;
    std::vector<std::string> errors;   // the first few
};

// Upscales the dump into `out` by class. `log` gets progress lines; `cancel` (may be null) stops between textures and batches, keeping what is finished;
// `progress` (may be empty) is told after each batch how many of this run's textures are done (upscaled or failed) of how many.
Report run(const std::filesystem::path& dump, const std::filesystem::path& table, const std::filesystem::path& out, const Options& options,
           const std::string& tool, const ModelRunner& model, const std::function<void(const std::string&)>& log, const std::atomic<bool>* cancel,
           const std::function<void(int done, int total)>& progress = {});

}  // namespace thpsource2::upscale
