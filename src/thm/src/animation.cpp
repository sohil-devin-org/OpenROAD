// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

// Animation export of the physics history.
//
// Frames are rasterised into an 8-bit indexed canvas and written as an
// animated GIF by the small encoder below (GIF89a, one global colour table,
// LZW per frame; see the GIF89a specification, CompuServe 1990, sections
// 18-23 and appendix F).  No external library is used.  MP4 output re-encodes
// the GIF with ffmpeg when it is found on PATH; ffmpeg is never a build
// dependency.

#include "thm/Animation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "thm/PhysicsState.h"
#include "utl/Logger.h"
#include "web/core.h"

namespace thm {

namespace {

// ---------------------------------------------------------------------------
// palette

// Global colour table: indices [0, kSpectrumColors) are the temperature
// spectrum, sampled from web::SpectrumGenerator so exported frames use the
// same colour mapping as the GUI heat maps; the remaining entries are the
// fixed drawing colours below.
constexpr int kSpectrumColors = 240;
constexpr uint8_t kWhite = 240;
constexpr uint8_t kBlack = 241;
constexpr uint8_t kGrey = 242;
constexpr uint8_t kDarkGrey = 243;
constexpr uint8_t kLightGrey = 244;
constexpr uint8_t kMissing = 245;
constexpr int kPaletteSize = 256;

struct Palette
{
  uint8_t rgb[kPaletteSize][3] = {};
};

Palette makePalette()
{
  Palette palette;
  const web::SpectrumGenerator spectrum(1.0);
  for (int i = 0; i < kSpectrumColors; ++i) {
    const web::Painter::Color color = spectrum.getColor(
        static_cast<double>(i) / (kSpectrumColors - 1), 255);
    palette.rgb[i][0] = static_cast<uint8_t>(color.r);
    palette.rgb[i][1] = static_cast<uint8_t>(color.g);
    palette.rgb[i][2] = static_cast<uint8_t>(color.b);
  }
  const struct
  {
    uint8_t index;
    uint8_t r;
    uint8_t g;
    uint8_t b;
  } fixed[] = {
      {kWhite, 255, 255, 255},
      {kBlack, 0, 0, 0},
      {kGrey, 128, 128, 128},
      {kDarkGrey, 48, 48, 48},
      {kLightGrey, 200, 200, 200},
      {kMissing, 230, 230, 230},
  };
  for (const auto& f : fixed) {
    palette.rgb[f.index][0] = f.r;
    palette.rgb[f.index][1] = f.g;
    palette.rgb[f.index][2] = f.b;
  }
  return palette;
}

// ---------------------------------------------------------------------------
// canvas

class Canvas
{
 public:
  Canvas(int width, int height)
      : width_(width), height_(height), pixels_(width * height, kWhite)
  {
  }

  int width() const { return width_; }
  int height() const { return height_; }
  const std::vector<uint8_t>& pixels() const { return pixels_; }

  void set(int x, int y, uint8_t color)
  {
    if (x >= 0 && x < width_ && y >= 0 && y < height_) {
      pixels_[y * width_ + x] = color;
    }
  }

  void fillRect(int x, int y, int w, int h, uint8_t color)
  {
    for (int py = y; py < y + h; ++py) {
      for (int px = x; px < x + w; ++px) {
        set(px, py, color);
      }
    }
  }

  void drawRect(int x, int y, int w, int h, uint8_t color)
  {
    for (int px = x; px < x + w; ++px) {
      set(px, y, color);
      set(px, y + h - 1, color);
    }
    for (int py = y; py < y + h; ++py) {
      set(x, py, color);
      set(x + w - 1, py, color);
    }
  }

 private:
  int width_;
  int height_;
  std::vector<uint8_t> pixels_;
};

// ---------------------------------------------------------------------------
// 5x7 bitmap font (upper case letters, digits and punctuation; lower case
// letters are drawn with the upper case glyphs).

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;

struct Glyph
{
  char ch;
  const char* rows[kGlyphHeight];
};

constexpr Glyph kGlyphs[] = {
    {'A', {" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
    {'B', {"#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "}},
    {'C', {" ####", "#    ", "#    ", "#    ", "#    ", "#    ", " ####"}},
    {'D', {"#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "}},
    {'E', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"}},
    {'F', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "}},
    {'G', {" ####", "#    ", "#    ", "# ###", "#   #", "#   #", " ####"}},
    {'H', {"#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
    {'I', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####"}},
    {'J', {"  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "}},
    {'K', {"#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"}},
    {'L', {"#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"}},
    {'M', {"#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"}},
    {'N', {"#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #"}},
    {'O', {" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
    {'P', {"#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "}},
    {'Q', {" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"}},
    {'R', {"#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"}},
    {'S', {" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "}},
    {'T', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'U', {"#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
    {'V', {"#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "}},
    {'W', {"#   #", "#   #", "#   #", "# # #", "# # #", "## ##", "#   #"}},
    {'X', {"#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"}},
    {'Y', {"#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'Z', {"#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"}},
    {'0', {" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "}},
    {'1', {"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
    {'2', {" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"}},
    {'3', {"#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### "}},
    {'4', {"   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "}},
    {'5', {"#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "}},
    {'6', {"  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "}},
    {'7', {"#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "}},
    {'8', {" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "}},
    {'9', {" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "}},
    {' ', {"     ", "     ", "     ", "     ", "     ", "     ", "     "}},
    {'.', {"     ", "     ", "     ", "     ", "     ", " ##  ", " ##  "}},
    {',', {"     ", "     ", "     ", "     ", " ##  ", " ##  ", " #   "}},
    {':', {"     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     "}},
    {'-', {"     ", "     ", "     ", "#####", "     ", "     ", "     "}},
    {'+', {"     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "}},
    {'=', {"     ", "     ", "#####", "     ", "#####", "     ", "     "}},
    {'/', {"    #", "    #", "   # ", "  #  ", " #   ", "#    ", "#    "}},
    {'(', {"  #  ", " #   ", "#    ", "#    ", "#    ", " #   ", "  #  "}},
    {')', {"  #  ", "   # ", "    #", "    #", "    #", "   # ", "  #  "}},
    {'%', {"##  #", "##  #", "   # ", "  #  ", " #   ", "#  ##", "#  ##"}},
    {'_', {"     ", "     ", "     ", "     ", "     ", "     ", "#####"}},
    {'#', {" # # ", " # # ", "#####", " # # ", "#####", " # # ", " # # "}},
    {'?', {" ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  "}},
};

const Glyph* findGlyph(char ch)
{
  const char upper
      = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  for (const Glyph& glyph : kGlyphs) {
    if (glyph.ch == upper) {
      return &glyph;
    }
  }
  return nullptr;
}

int textWidth(const std::string& text, int scale)
{
  return static_cast<int>(text.size()) * (kGlyphWidth + 1) * scale;
}

void drawText(Canvas& canvas,
              int x,
              int y,
              const std::string& text,
              uint8_t color,
              int scale = 1)
{
  for (const char ch : text) {
    const Glyph* glyph = findGlyph(ch);
    if (glyph != nullptr) {
      for (int row = 0; row < kGlyphHeight; ++row) {
        for (int col = 0; col < kGlyphWidth; ++col) {
          if (glyph->rows[row][col] == '#') {
            canvas.fillRect(
                x + col * scale, y + row * scale, scale, scale, color);
          }
        }
      }
    }
    x += (kGlyphWidth + 1) * scale;
  }
}

std::string format(const char* fmt, double value)
{
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), fmt, value);
  return buffer;
}

// ---------------------------------------------------------------------------
// GIF encoder

class GifEncoder
{
 public:
  GifEncoder(std::ostream& out,
             int width,
             int height,
             const Palette& palette,
             int delay_cs)
      : out_(out), width_(width), height_(height), delay_cs_(delay_cs)
  {
    out_ << "GIF89a";
    writeU16(width_);
    writeU16(height_);
    // Global colour table present, 8 bits per primary, 256 entries.
    out_.put(static_cast<char>(0xF7));
    out_.put(0);  // background colour index
    out_.put(0);  // pixel aspect ratio
    for (int i = 0; i < kPaletteSize; ++i) {
      out_.put(static_cast<char>(palette.rgb[i][0]));
      out_.put(static_cast<char>(palette.rgb[i][1]));
      out_.put(static_cast<char>(palette.rgb[i][2]));
    }
    // NETSCAPE2.0 application extension: loop forever.
    const char netscape[] = {0x21,
                             static_cast<char>(0xFF),
                             0x0B,
                             'N',
                             'E',
                             'T',
                             'S',
                             'C',
                             'A',
                             'P',
                             'E',
                             '2',
                             '.',
                             '0',
                             0x03,
                             0x01,
                             0x00,
                             0x00,
                             0x00};
    out_.write(netscape, sizeof(netscape));
  }

  void addFrame(const Canvas& canvas)
  {
    // Graphic control extension: keep the previous frame (disposal 1).
    out_.put(0x21);
    out_.put(static_cast<char>(0xF9));
    out_.put(0x04);
    out_.put(0x04);
    writeU16(delay_cs_);
    out_.put(0);
    out_.put(0);
    // Image descriptor covering the whole logical screen.
    out_.put(0x2C);
    writeU16(0);
    writeU16(0);
    writeU16(width_);
    writeU16(height_);
    out_.put(0);
    writeLzw(canvas.pixels());
  }

  void finish() { out_.put(0x3B); }

 private:
  static constexpr int kMinCodeSize = 8;
  static constexpr int kClearCode = 1 << kMinCodeSize;
  static constexpr int kEndCode = kClearCode + 1;
  static constexpr int kMaxCode = 4095;

  void writeU16(int value)
  {
    out_.put(static_cast<char>(value & 0xFF));
    out_.put(static_cast<char>((value >> 8) & 0xFF));
  }

  // Bit packer producing the LZW data sub-blocks (<= 255 bytes each).
  struct BitWriter
  {
    std::vector<uint8_t> bytes;
    uint32_t accumulator = 0;
    int bit_count = 0;

    void write(int code, int size)
    {
      accumulator |= static_cast<uint32_t>(code) << bit_count;
      bit_count += size;
      while (bit_count >= 8) {
        bytes.push_back(static_cast<uint8_t>(accumulator & 0xFF));
        accumulator >>= 8;
        bit_count -= 8;
      }
    }

    void flush()
    {
      if (bit_count > 0) {
        bytes.push_back(static_cast<uint8_t>(accumulator & 0xFF));
        accumulator = 0;
        bit_count = 0;
      }
    }
  };

  // Variable-length LZW (GIF flavour: clear/end codes, 9-12 bit codes, no
  // early change).  Each string is identified by (prefix code, next byte).
  void writeLzw(const std::vector<uint8_t>& pixels)
  {
    out_.put(static_cast<char>(kMinCodeSize));
    BitWriter bits;
    std::unordered_map<uint32_t, int> table;
    int code_size = kMinCodeSize + 1;
    int max_code = kEndCode;
    bits.write(kClearCode, code_size);
    if (!pixels.empty()) {
      int prefix = pixels[0];
      for (size_t i = 1; i < pixels.size(); ++i) {
        const uint32_t key = (static_cast<uint32_t>(prefix) << 8) | pixels[i];
        auto it = table.find(key);
        if (it != table.end()) {
          prefix = it->second;
          continue;
        }
        bits.write(prefix, code_size);
        table[key] = ++max_code;
        if (max_code >= (1 << code_size)) {
          ++code_size;
        }
        if (max_code == kMaxCode) {
          bits.write(kClearCode, code_size);
          code_size = kMinCodeSize + 1;
          max_code = kEndCode;
          table.clear();
        }
        prefix = pixels[i];
      }
      bits.write(prefix, code_size);
    }
    bits.write(kEndCode, code_size);
    bits.flush();
    for (size_t offset = 0; offset < bits.bytes.size(); offset += 255) {
      const size_t count = std::min<size_t>(255, bits.bytes.size() - offset);
      out_.put(static_cast<char>(count));
      out_.write(reinterpret_cast<const char*>(bits.bytes.data() + offset),
                 count);
    }
    out_.put(0);  // block terminator
  }

  std::ostream& out_;
  int width_;
  int height_;
  int delay_cs_;
};

// ---------------------------------------------------------------------------
// frame composition

// One map panel of a frame.  The map may be null (drawn as "N/A").
struct Panel
{
  const PhysicsSnapshot* snapshot = nullptr;
  const MapSnapshot* map = nullptr;
  std::string caption;
  bool draw_cells = false;
};

struct Frame
{
  std::vector<Panel> panels;
  std::string status;
};

struct Layout
{
  static constexpr int kMargin = 8;
  static constexpr int kGap = 12;
  static constexpr int kTitleScale = 2;
  static constexpr int kTitleHeight = kGlyphHeight * kTitleScale + 8;
  static constexpr int kCaptionHeight = kGlyphHeight + 6;
  static constexpr int kStatusHeight = kGlyphHeight + 6;
  static constexpr int kColorBarWidth = 14;
  static constexpr int kColorBarLabels = 64;

  int width = 0;
  int height = 0;
  int panels = 1;
  int map_width = 0;
  int map_height = 0;

  int mapX(int panel) const { return kMargin + panel * (map_width + kGap); }
  int mapY() const { return kMargin + kTitleHeight; }
};

Layout makeLayout(int width_px, int panels, double die_aspect)
{
  Layout layout;
  layout.panels = std::max(1, panels);
  layout.width = std::max(200, width_px) & ~1;
  const int usable = layout.width - 2 * Layout::kMargin - Layout::kColorBarWidth
                     - Layout::kColorBarLabels
                     - (layout.panels - 1) * Layout::kGap;
  layout.map_width = std::max(40, usable / layout.panels);
  const double aspect = std::isfinite(die_aspect) && die_aspect > 0
                            ? std::clamp(die_aspect, 0.4, 2.5)
                            : 1.0;
  layout.map_height = std::max(40, static_cast<int>(layout.map_width * aspect));
  int height = Layout::kMargin + Layout::kTitleHeight + layout.map_height
               + Layout::kCaptionHeight + Layout::kStatusHeight
               + Layout::kMargin;
  layout.height = (height + 1) & ~1;  // even, for yuv420p video encoding
  return layout;
}

uint8_t temperatureColor(double value, double min_c, double max_c)
{
  const double t = std::clamp((value - min_c) / (max_c - min_c), 0.0, 1.0);
  return static_cast<uint8_t>(std::lround(t * (kSpectrumColors - 1)));
}

void drawMap(Canvas& canvas,
             const Layout& layout,
             int panel_index,
             const Panel& panel,
             double min_c,
             double max_c)
{
  const int x0 = layout.mapX(panel_index);
  const int y0 = layout.mapY();
  const int w = layout.map_width;
  const int h = layout.map_height;
  const MapSnapshot* map = panel.map;
  if (map == nullptr || map->nx <= 0 || map->ny <= 0
      || map->values.size() != static_cast<size_t>(map->nx * map->ny)) {
    canvas.fillRect(x0, y0, w, h, kMissing);
    drawText(canvas,
             x0 + (w - textWidth("N/A", 2)) / 2,
             y0 + (h - kGlyphHeight * 2) / 2,
             "N/A",
             kGrey,
             2);
  } else {
    // Nearest-neighbour fill; row 0 of the map is the bottom of the die.
    for (int py = 0; py < h; ++py) {
      const int ty = std::min(map->ny - 1, (h - 1 - py) * map->ny / h);
      for (int px = 0; px < w; ++px) {
        const int tx = std::min(map->nx - 1, px * map->nx / w);
        canvas.set(
            x0 + px, y0 + py, temperatureColor(map->at(tx, ty), min_c, max_c));
      }
    }
  }
  if (panel.draw_cells && panel.snapshot != nullptr) {
    const PhysicsSnapshot& snap = *panel.snapshot;
    const double die_w = snap.die_xmax - snap.die_xmin;
    const double die_h = snap.die_ymax - snap.die_ymin;
    if (die_w > 0 && die_h > 0) {
      for (const InstancePosition& pos : snap.positions) {
        const int px
            = static_cast<int>((pos.x_dbu - snap.die_xmin) / die_w * (w - 1));
        const int py
            = static_cast<int>((snap.die_ymax - pos.y_dbu) / die_h * (h - 1));
        canvas.set(x0 + px, y0 + py, kDarkGrey);
      }
    }
  }
  canvas.drawRect(x0 - 1, y0 - 1, w + 2, h + 2, kBlack);
  drawText(canvas, x0, y0 + h + 4, panel.caption, kBlack);
}

void drawColorBar(Canvas& canvas,
                  const Layout& layout,
                  double min_c,
                  double max_c)
{
  const int x0 = layout.mapX(layout.panels) - Layout::kGap + 6;
  const int y0 = layout.mapY();
  const int h = layout.map_height;
  for (int py = 0; py < h; ++py) {
    const double value = max_c - (max_c - min_c) * py / std::max(1, h - 1);
    canvas.fillRect(x0,
                    y0 + py,
                    Layout::kColorBarWidth,
                    1,
                    temperatureColor(value, min_c, max_c));
  }
  canvas.drawRect(x0 - 1, y0 - 1, Layout::kColorBarWidth + 2, h + 2, kBlack);
  const int label_x = x0 + Layout::kColorBarWidth + 4;
  drawText(canvas, label_x, y0, format("%.1f C", max_c), kBlack);
  drawText(canvas,
           label_x,
           y0 + h / 2 - kGlyphHeight / 2,
           format("%.1f C", (min_c + max_c) / 2),
           kBlack);
  drawText(
      canvas, label_x, y0 + h - kGlyphHeight, format("%.1f C", min_c), kBlack);
}

Canvas renderFrame(const Layout& layout,
                   const std::string& title,
                   const Frame& frame,
                   double min_c,
                   double max_c)
{
  Canvas canvas(layout.width, layout.height);
  drawText(canvas,
           Layout::kMargin,
           Layout::kMargin,
           title,
           kBlack,
           Layout::kTitleScale);
  for (size_t i = 0; i < frame.panels.size(); ++i) {
    drawMap(canvas, layout, i, frame.panels[i], min_c, max_c);
  }
  drawColorBar(canvas, layout, min_c, max_c);
  drawText(canvas,
           Layout::kMargin,
           layout.height - Layout::kMargin - kGlyphHeight,
           frame.status,
           kDarkGrey);
  return canvas;
}

// ---------------------------------------------------------------------------
// frame selection

const char* typeName(AnimationType type)
{
  switch (type) {
    case AnimationType::kCooldown:
      return "cooldown";
    case AnimationType::kTransient:
      return "transient";
    case AnimationType::kElectrothermal:
      return "electrothermal";
    case AnimationType::kCompare:
      return "compare";
    case AnimationType::kStack:
      return "stack";
  }
  return "animation";
}

bool isTransient(const PhysicsSnapshot& snap)
{
  return snap.tag == "transient";
}

std::string statusLine(const PhysicsSnapshot& snap)
{
  const PhysicsMetrics& m = snap.metrics;
  std::ostringstream status;
  if (isTransient(snap)) {
    status << "T=" << format("%.4g", snap.time_s) << " S";
    if (!m.label.empty()) {
      status << " (" << m.label << ")";
    }
  } else {
    status << "ITER " << m.iteration;
  }
  status << "  PEAK " << format("%.1f", m.peakTemp()) << " C";
  if (!isTransient(snap)) {
    status << "  WNS " << format("%.3f", m.wns_derated_s * 1e9) << " NS";
    if (m.hpwl_um > 0) {
      status << "  HPWL " << format("%.0f", m.hpwl_um) << " UM";
    }
    if (m.electrothermal_iterations > 0) {
      status << "  LOOPS " << m.electrothermal_iterations;
    }
    if (m.runaway) {
      status << "  RUNAWAY";
    } else if (!m.converged) {
      status << "  NOT CONVERGED";
    }
  } else {
    status << "  POWER " << format("%.3g", m.total_power_w) << " W";
  }
  return status.str();
}

std::string panelCaption(const PhysicsSnapshot& snap, int die)
{
  std::ostringstream caption;
  caption << "DIE " << die;
  if (!snap.metrics.label.empty()) {
    caption << "  " << snap.metrics.label;
  }
  caption << "  PEAK " << format("%.1f", snap.metrics.peakTemp()) << " C";
  return caption.str();
}

Panel makePanel(const PhysicsSnapshot& snap, int die, bool draw_cells)
{
  Panel panel;
  panel.snapshot = &snap;
  panel.map = snap.findMap("temperature", die);
  panel.caption = panelCaption(snap, die);
  panel.draw_cells = draw_cells;
  return panel;
}

std::vector<const PhysicsSnapshot*> selectSnapshots(
    const PhysicsHistory& history,
    AnimationType type,
    utl::Logger* logger)
{
  std::vector<const PhysicsSnapshot*> steady;
  std::vector<const PhysicsSnapshot*> transient;
  for (const PhysicsSnapshot& snap : history.snapshots()) {
    (isTransient(snap) ? transient : steady).push_back(&snap);
  }
  std::vector<const PhysicsSnapshot*> selected;
  switch (type) {
    case AnimationType::kCooldown:
      // Steady-state checkpoints first, then the transient cooling frames.
      selected = steady;
      selected.insert(selected.end(), transient.begin(), transient.end());
      break;
    case AnimationType::kTransient:
      selected = transient;
      break;
    case AnimationType::kElectrothermal:
    case AnimationType::kCompare:
    case AnimationType::kStack:
      selected = steady;
      break;
  }
  if (selected.empty()) {
    logger->warn(
        utl::THM,
        166,
        "The physics history has no {} snapshots; animating all {} "
        "recorded snapshots instead.",
        type == AnimationType::kTransient ? "transient" : "steady-state",
        history.size());
    for (const PhysicsSnapshot& snap : history.snapshots()) {
      selected.push_back(&snap);
    }
  }
  return selected;
}

std::vector<Frame> buildFrames(const PhysicsHistory& history,
                               const PhysicsHistory* reference,
                               const AnimationOptions& options,
                               utl::Logger* logger)
{
  const int die = std::max(0, options.die);
  const std::vector<const PhysicsSnapshot*> snapshots
      = selectSnapshots(history, options.type, logger);
  std::vector<Frame> frames;
  switch (options.type) {
    case AnimationType::kCooldown:
    case AnimationType::kElectrothermal:
    case AnimationType::kTransient: {
      const bool cells = options.type != AnimationType::kTransient;
      for (const PhysicsSnapshot* snap : snapshots) {
        Frame frame;
        frame.panels.push_back(makePanel(*snap, die, cells));
        frame.status = statusLine(*snap);
        frames.push_back(std::move(frame));
      }
      break;
    }
    case AnimationType::kCompare: {
      if (reference == nullptr || reference->empty()) {
        logger->warn(utl::THM,
                     162,
                     "The compare animation needs a reference history "
                     "(load_physics_reference); nothing was written.");
        return frames;
      }
      const std::vector<const PhysicsSnapshot*> ref_snapshots
          = selectSnapshots(*reference, options.type, logger);
      const size_t count = std::max(snapshots.size(), ref_snapshots.size());
      for (size_t i = 0; i < count; ++i) {
        const PhysicsSnapshot& snap
            = *snapshots[std::min(i, snapshots.size() - 1)];
        const PhysicsSnapshot& ref
            = *ref_snapshots[std::min(i, ref_snapshots.size() - 1)];
        Frame frame;
        frame.panels.push_back(makePanel(snap, die, false));
        frame.panels.back().caption = "CURRENT: " + frame.panels.back().caption;
        frame.panels.push_back(makePanel(ref, die, false));
        frame.panels.back().caption
            = "REFERENCE: " + frame.panels.back().caption;
        frame.status = statusLine(snap) + "  REF PEAK "
                       + format("%.1f", ref.metrics.peakTemp()) + " C";
        frames.push_back(std::move(frame));
      }
      break;
    }
    case AnimationType::kStack: {
      if (snapshots.front()->findMap("temperature", 1) == nullptr) {
        logger->warn(utl::THM,
                     163,
                     "The stack animation needs a two-die history "
                     "(set_thermal_config -two_die); nothing was written.");
        return frames;
      }
      for (const PhysicsSnapshot* snap : snapshots) {
        Frame frame;
        frame.panels.push_back(makePanel(*snap, 0, false));
        frame.panels.push_back(makePanel(*snap, 1, false));
        frame.status = statusLine(*snap);
        frames.push_back(std::move(frame));
      }
      break;
    }
  }
  return frames;
}

// Shared colour scale: the requested fixed range, otherwise the min/max over
// every map of every frame so all frames are comparable.
void determineScale(const std::vector<Frame>& frames,
                    const AnimationOptions& options,
                    double& min_c,
                    double& max_c)
{
  if (options.scale_max_c > options.scale_min_c) {
    min_c = options.scale_min_c;
    max_c = options.scale_max_c;
    return;
  }
  min_c = std::numeric_limits<double>::max();
  max_c = std::numeric_limits<double>::lowest();
  for (const Frame& frame : frames) {
    for (const Panel& panel : frame.panels) {
      if (panel.map == nullptr) {
        continue;
      }
      for (const double value : panel.map->values) {
        min_c = std::min(min_c, value);
        max_c = std::max(max_c, value);
      }
    }
  }
  if (min_c > max_c) {
    min_c = 0.0;
    max_c = 1.0;
  }
  if (max_c - min_c < 1e-6) {
    // Flat field: widen by one degree so the colour bar stays readable.
    min_c -= 0.5;
    max_c += 0.5;
  }
}

double dieAspect(const std::vector<Frame>& frames)
{
  for (const Frame& frame : frames) {
    for (const Panel& panel : frame.panels) {
      if (panel.snapshot != nullptr) {
        const double w = panel.snapshot->die_xmax - panel.snapshot->die_xmin;
        const double h = panel.snapshot->die_ymax - panel.snapshot->die_ymin;
        if (w > 0 && h > 0) {
          return h / w;
        }
      }
    }
  }
  return 1.0;
}

std::string makeTitle(const PhysicsHistory& history,
                      const AnimationOptions& options)
{
  if (!options.title.empty()) {
    return options.title;
  }
  std::string title = typeName(options.type);
  if (!history.designName().empty()) {
    title += " - " + history.designName();
  }
  if (!history.tag().empty()) {
    title += " (" + history.tag() + ")";
  }
  return title;
}

// ---------------------------------------------------------------------------
// output

std::optional<std::filesystem::path> findOnPath(const std::string& program)
{
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return std::nullopt;
  }
  std::stringstream paths(path_env);
  std::string dir;
  while (std::getline(paths, dir, ':')) {
    if (dir.empty()) {
      continue;
    }
    std::error_code ec;
    const std::filesystem::path candidate
        = std::filesystem::path(dir) / program;
    if (std::filesystem::is_regular_file(candidate, ec)) {
      return candidate;
    }
  }
  return std::nullopt;
}

std::string shellQuote(const std::string& arg)
{
  std::string quoted = "'";
  for (const char ch : arg) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted += ch;
    }
  }
  return quoted + "'";
}

bool writeGif(const std::string& file,
              const Layout& layout,
              const std::string& title,
              const std::vector<Frame>& frames,
              double min_c,
              double max_c,
              int fps,
              utl::Logger* logger)
{
  std::ofstream out(file, std::ios::binary);
  if (!out) {
    logger->warn(utl::THM, 164, "Cannot open {} for writing.", file);
    return false;
  }
  const int delay_cs
      = std::max(1, static_cast<int>(std::lround(100.0 / std::max(1, fps))));
  GifEncoder encoder(out, layout.width, layout.height, makePalette(), delay_cs);
  for (const Frame& frame : frames) {
    encoder.addFrame(renderFrame(layout, title, frame, min_c, max_c));
  }
  encoder.finish();
  return static_cast<bool>(out);
}

}  // namespace

bool writeAnimation(const PhysicsHistory& history,
                    const PhysicsHistory* reference,
                    const AnimationOptions& options,
                    utl::Logger* logger)
{
  const std::vector<Frame> frames
      = buildFrames(history, reference, options, logger);
  if (frames.empty()) {
    return false;
  }
  double min_c;
  double max_c;
  determineScale(frames, options, min_c, max_c);
  const Layout layout = makeLayout(
      options.width_px, frames.front().panels.size(), dieAspect(frames));
  const std::string title = makeTitle(history, options);

  std::filesystem::path output(options.file);
  std::optional<std::filesystem::path> ffmpeg;
  bool mp4 = options.format == "mp4";
  if (mp4) {
    ffmpeg = findOnPath("ffmpeg");
    if (!ffmpeg) {
      output.replace_extension(".gif");
      logger->warn(
          utl::THM,
          161,
          "ffmpeg was not found on PATH; writing an animated GIF to {} "
          "instead of an MP4.",
          output.string());
      mp4 = false;
    }
  }
  const std::filesystem::path gif_file
      = mp4 ? std::filesystem::path(output.string() + ".frames.gif") : output;
  if (!writeGif(gif_file.string(),
                layout,
                title,
                frames,
                min_c,
                max_c,
                options.fps,
                logger)) {
    return false;
  }
  if (mp4) {
    // GIF frame delays carry the frame rate; yuv420p needs even dimensions,
    // which makeLayout guarantees.
    const std::string command = shellQuote(ffmpeg->string())
                                + " -y -loglevel error -i "
                                + shellQuote(gif_file.string())
                                + " -movflags +faststart -pix_fmt yuv420p "
                                + shellQuote(output.string());
    const int status = std::system(command.c_str());
    if (status != 0) {
      logger->warn(utl::THM,
                   165,
                   "ffmpeg failed (exit status {}); the frames are left in {}.",
                   status,
                   gif_file.string());
      return false;
    }
    std::error_code ec;
    std::filesystem::remove(gif_file, ec);
  }
  logger->info(utl::THM,
               160,
               "Wrote {} {} animation frames ({}x{}, {:.1f}-{:.1f} C) to {}.",
               frames.size(),
               typeName(options.type),
               layout.width,
               layout.height,
               min_c,
               max_c,
               output.filename().string());
  return true;
}

}  // namespace thm
