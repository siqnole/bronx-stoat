#pragma once
// ============================================================
//  chart_renderer.h — Cairo PNG chart generation for bronx
//  Renders bar / line / heatmap / pie / scatter charts into
//  in-memory PNG bytes, then uploads to Revolt's Autumn CDN.
//
//  Usage:
//    std::string png = chart::render_line_chart(...);
//    std::string autumn_id = chart::upload_chart(bot, png);
//    embed.set_image(autumn_id);
// ============================================================

#include <cairo/cairo.h>

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iostream>

// Forward-declare so we can reference the cluster without pulling in the full header
namespace stoatpp { class cluster; }

namespace bronx {
namespace chart {

// ── colour helpers ─────────────────────────────────────────────
struct RGBA { double r, g, b, a; };

// palette matching the embed / dashboard style
inline const RGBA COL_BG        = {0.04,  0.04,  0.04,  1.0};   // #0a0a0a
inline const RGBA COL_CARD      = {0.09,  0.09,  0.11,  1.0};   // #171719
inline const RGBA COL_BORDER    = {1.0,   1.0,   1.0,   0.06};
inline const RGBA COL_TEXT      = {1.0,   1.0,   1.0,   0.85};
inline const RGBA COL_DIM       = {1.0,   1.0,   1.0,   0.45};
inline const RGBA COL_GRID      = {1.0,   1.0,   1.0,   0.04};
inline const RGBA COL_ACCENT    = {0.706, 0.655, 0.839, 1.0};   // #b4a7d6
inline const RGBA COL_GREEN     = {0.063, 0.725, 0.506, 1.0};   // #10b981
inline const RGBA COL_RED       = {0.937, 0.408, 0.271, 1.0};   // #ef4444
inline const RGBA COL_BLUE      = {0.231, 0.510, 0.965, 1.0};   // #3b82f6
inline const RGBA COL_CYAN      = {0.024, 0.714, 0.831, 1.0};   // #06b6d4
inline const RGBA COL_YELLOW    = {0.961, 0.620, 0.043, 1.0};   // #f59e0b

inline const std::vector<RGBA> PALETTE = {
    COL_ACCENT, {0.545, 0.361, 0.965, 1.0}, COL_GREEN, COL_YELLOW,
    COL_RED, COL_CYAN, {0.925, 0.282, 0.600, 1.0}, {0.078, 0.722, 0.651, 1.0}
};

inline void set_colour(cairo_t* cr, const RGBA& c) {
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
}

// ── PNG write-to-memory callback ───────────────────────────────
struct PngBuffer { std::string data; };
inline cairo_status_t png_write_cb(void* closure, const unsigned char* data, unsigned int length) {
    static_cast<PngBuffer*>(closure)->data.append(reinterpret_cast<const char*>(data), length);
    return CAIRO_STATUS_SUCCESS;
}

inline std::vector<uint32_t> utf8_to_codepoints(const std::string& str) {
    std::vector<uint32_t> codepoints;
    for (size_t i = 0; i < str.length();) {
        unsigned char c = str[i];
        uint32_t cp = 0;
        int len = 0;
        if (c <= 0x7F) {
            cp = c;
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            len = 4;
        } else {
            i++;
            continue;
        }
        if (i + len > str.length()) {
            break;
        }
        for (int j = 1; j < len; j++) {
            cp = (cp << 6) | (str[i + j] & 0x3F);
        }

        // 1. Skip zero-width space, variation selectors, and direction marks
        if (cp == 0x200B || cp == 0x200C || 
            cp == 0xFE0F || cp == 0xFE0E || cp == 0xFEFF ||
            cp == 0x200E || cp == 0x200F) {
            i += len;
            continue;
        }
        
        // 2. Convert special spaces to standard ASCII space (0x20)
        if (cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200A) ||
            cp == 0x202F || cp == 0x205F || cp == 0x3000) {
            cp = 0x20;
        }

        codepoints.push_back(cp);
        i += len;
    }
    return codepoints;
}

inline std::string codepoints_to_utf8(const std::vector<uint32_t>& cps) {
    std::string out;
    for (uint32_t cp : cps) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0x10FFFF) {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

inline bool is_emoji_or_symbol(uint32_t cp) {
    if (cp >= 0x1F000 && cp <= 0x1FAFF) return true;
    if (cp >= 0x2600 && cp <= 0x27BF) return true;
    if (cp >= 0x2300 && cp <= 0x23FF) return true;
    if (cp >= 0x2B00 && cp <= 0x2BFF) return true;
    if (cp >= 0xFE00 && cp <= 0xFE0F) return true;
    if (cp == 0x200D) return true;
    if (cp == 0x20E3) return true;
    return false;
}

struct TextRun {
    std::string text;
    bool is_emoji;
};

inline std::vector<TextRun> split_mixed_text(const std::string& str) {
    auto cps = utf8_to_codepoints(str);
    std::vector<TextRun> runs;
    if (cps.empty()) return runs;

    bool current_is_emoji = is_emoji_or_symbol(cps[0]);
    std::vector<uint32_t> current_run;
    
    for (uint32_t cp : cps) {
        bool cp_is_emoji = is_emoji_or_symbol(cp);
        if (cp_is_emoji != current_is_emoji) {
            runs.push_back({codepoints_to_utf8(current_run), current_is_emoji});
            current_run.clear();
            current_is_emoji = cp_is_emoji;
        }
        current_run.push_back(cp);
    }
    if (!current_run.empty()) {
        runs.push_back({codepoints_to_utf8(current_run), current_is_emoji});
    }
    return runs;
}

inline std::string shorten_utf8_label(const std::string& str, size_t max_chars) {
    auto cps = utf8_to_codepoints(str);
    if (cps.size() <= max_chars) return str;
    std::vector<uint32_t> short_cps(cps.begin(), cps.begin() + max_chars - 2);
    return codepoints_to_utf8(short_cps) + "..";
}

inline void get_current_font_settings(cairo_t* cr, double& size, bool& bold) {
    cairo_matrix_t font_matrix;
    cairo_get_font_matrix(cr, &font_matrix);
    size = font_matrix.xx;
    
    auto* face = cairo_get_font_face(cr);
    if (face && cairo_font_face_get_type(face) == CAIRO_FONT_TYPE_TOY) {
        auto weight = cairo_toy_font_face_get_weight(face);
        bold = (weight == CAIRO_FONT_WEIGHT_BOLD);
    } else {
        bold = false;
    }
}

// ── text helpers ───────────────────────────────────────────────
inline void set_font(cairo_t* cr, double size, bool bold = false) {
    cairo_select_font_face(cr, "sans-serif",
        CAIRO_FONT_SLANT_NORMAL,
        bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
}

inline double text_width(cairo_t* cr, const std::string& s) {
    double size = 10.0;
    bool bold = false;
    get_current_font_settings(cr, size, bold);

    auto runs = split_mixed_text(s);
    double total_width = 0.0;
    
    cairo_save(cr);
    for (const auto& run : runs) {
        if (run.is_emoji) {
            cairo_select_font_face(cr, "Twemoji", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        } else {
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, 
                                   bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
        }
        cairo_set_font_size(cr, size);
        
        cairo_text_extents_t ext;
        cairo_text_extents(cr, run.text.c_str(), &ext);
        total_width += ext.x_advance;
    }
    cairo_restore(cr);
    return total_width;
}

inline void draw_text(cairo_t* cr, const std::string& s) {
    double size = 10.0;
    bool bold = false;
    get_current_font_settings(cr, size, bold);

    auto runs = split_mixed_text(s);
    
    cairo_save(cr);
    for (const auto& run : runs) {
        if (run.is_emoji) {
            cairo_select_font_face(cr, "Twemoji", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        } else {
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, 
                                   bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
        }
        cairo_set_font_size(cr, size);
        cairo_show_text(cr, run.text.c_str());
    }
    cairo_restore(cr);
}

#define cairo_show_text(cr, text) draw_text(cr, text)

// ── rounded rectangle ──────────────────────────────────────────
inline void round_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0,          M_PI / 2);
    cairo_arc(cr, x + r,     y + h - r, r, M_PI / 2,   M_PI);
    cairo_arc(cr, x + r,     y + r,     r, M_PI,        3 * M_PI / 2);
    cairo_close_path(cr);
}

// ── number formatting ──────────────────────────────────────────
inline std::string fmt_num(int64_t n) {
    if (n >= 1000000) return std::to_string(n / 1000000) + "." + std::to_string((n % 1000000) / 100000) + "M";
    if (n >= 1000)    return std::to_string(n / 1000)    + "." + std::to_string((n % 1000)    / 100)    + "K";
    return std::to_string(n);
}

// ── chart data types ───────────────────────────────────────────
struct BarItem    { std::string label; int64_t value; };
struct LineSeries { std::string name; RGBA colour; std::vector<double> values; int axis = 0; };
struct PieSlice   { std::string label; double value; RGBA colour; };
struct ScatterPoint  { double x; double y; };
struct ScatterSeries { std::string name; RGBA colour; std::vector<ScatterPoint> points; };
struct BarSeries     { std::string name; RGBA colour; std::vector<int64_t> values; };

// ── Autumn upload helper ───────────────────────────────────────
// Takes raw PNG bytes (from any render_* call) and uploads them to Autumn.
// Returns the Autumn file ID to use as Embed::media, or "" on failure.
// Must be called from a non-gateway thread (does HTTP I/O).
inline std::string upload_chart(stoatpp::cluster& bot, const std::string& png_bytes,
                                 const std::string& filename = "chart.png") {
    if (png_bytes.empty()) return "";
    std::vector<uint8_t> data(png_bytes.begin(), png_bytes.end());
    try {
        auto res = bot.rest().upload_file("/attachments", filename, data, "image/png");
        if (res.success() && res.body.contains("id") && res.body["id"].is_string()) {
            return res.body["id"].get<std::string>();
        }
        std::cerr << "[bronx chart] autumn upload failed: " << res.body.dump() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[bronx chart] autumn upload exception: " << e.what() << "\n";
    }
    return "";
}

// ========================================================================
//  render_horizontal_bar_chart
//  top-N style: horizontal bars with labels + values
// ========================================================================
inline std::string render_horizontal_bar_chart(
        const std::string& title,
        const std::vector<BarItem>& items,
        int width = 600, int height = 0)
{
    if (items.empty()) return {};

    const int pad      = 20;
    const int title_h  = title.empty() ? 0 : 36;
    const int bar_h    = 28;
    const int gap      = 10;
    const int label_w  = 120;
    if (height <= 0)
        height = pad * 2 + title_h + (int)items.size() * (bar_h + gap);

    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    auto* cr      = cairo_create(surface);

    round_rect(cr, 0, 0, width, height, 12);
    set_colour(cr, COL_CARD);
    cairo_fill(cr);

    double y = pad;
    if (!title.empty()) {
        set_font(cr, 14, true);
        set_colour(cr, COL_TEXT);
        cairo_move_to(cr, pad, y + 14);
        cairo_show_text(cr, title.c_str());
        y += title_h;
    }

    int64_t max_val      = 1;
    for (auto& item : items) max_val = std::max(max_val, item.value);
    const double area_w  = width - pad * 2 - label_w - 60;

    for (size_t i = 0; i < items.size(); ++i) {
        double by        = y + i * (bar_h + gap);
        const auto& col  = PALETTE[i % PALETTE.size()];

        set_font(cr, 11, false);
        set_colour(cr, COL_DIM);
        std::string lbl  = items[i].label;
        lbl = shorten_utf8_label(lbl, 16);
        cairo_move_to(cr, pad, by + bar_h * 0.65);
        cairo_show_text(cr, lbl.c_str());

        double bw = (static_cast<double>(items[i].value) / max_val) * area_w;
        if (bw < 4) bw = 4;
        double bx = pad + label_w;
        round_rect(cr, bx, by + 2, bw, bar_h - 4, 4);
        cairo_set_source_rgba(cr, col.r, col.g, col.b, 0.75);
        cairo_fill(cr);

        set_font(cr, 11, true);
        set_colour(cr, COL_TEXT);
        std::string val_str = fmt_num(items[i].value);
        cairo_move_to(cr, bx + bw + 8, by + bar_h * 0.65);
        cairo_show_text(cr, val_str.c_str());
    }

    PngBuffer buf;
    cairo_surface_write_to_png_stream(surface, png_write_cb, &buf);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return buf.data;
}

// ========================================================================
//  render_line_chart
//  multi-series line chart with optional dual y-axis
// ========================================================================
inline std::string render_line_chart(
        const std::string& title,
        const std::vector<std::string>& labels,
        const std::vector<LineSeries>& series,
        int width = 600, int height = 300)
{
    if (labels.empty() || series.empty()) return {};

    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    auto* cr      = cairo_create(surface);

    round_rect(cr, 0, 0, width, height, 12);
    set_colour(cr, COL_CARD);
    cairo_fill(cr);

    bool has_right = false;
    for (auto& s : series) if (s.axis == 1) { has_right = true; break; }

    const int pad_l = 58, pad_r = has_right ? 58 : 20, pad_t = 48, pad_b = 40;
    double chart_w  = width  - pad_l - pad_r;
    double chart_h  = height - pad_t - pad_b;

    if (!title.empty()) {
        set_font(cr, 14, true);
        set_colour(cr, COL_TEXT);
        cairo_move_to(cr, pad_l, 28);
        cairo_show_text(cr, title.c_str());
    }

    auto nice_max = [](double mx) -> double {
        if (mx < 5) return 5;
        double nice = std::pow(10, std::floor(std::log10(mx)));
        return std::ceil(mx / nice) * nice;
    };

    double max_left = 1, max_right = 1;
    for (auto& s : series)
        for (auto v : s.values) {
            if (s.axis == 0) max_left  = std::max(max_left,  v);
            else             max_right = std::max(max_right, v);
        }
    max_left  = nice_max(max_left);
    max_right = nice_max(max_right);

    RGBA right_col = COL_DIM;
    for (auto& s : series) if (s.axis == 1) { right_col = s.colour; break; }

    // grid + y-axis labels
    for (int i = 0; i <= 4; ++i) {
        double gy = pad_t + chart_h * (1.0 - i / 4.0);
        set_colour(cr, COL_GRID);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, pad_l, gy); cairo_line_to(cr, width - pad_r, gy);
        cairo_stroke(cr);

        set_font(cr, 10, false);
        set_colour(cr, COL_DIM);
        std::string yl = fmt_num(static_cast<int64_t>(max_left * i / 4.0));
        cairo_move_to(cr, pad_l - text_width(cr, yl) - 6, gy + 4);
        cairo_show_text(cr, yl.c_str());

        if (has_right) {
            std::string yr = fmt_num(static_cast<int64_t>(max_right * i / 4.0));
            cairo_set_source_rgba(cr, right_col.r, right_col.g, right_col.b, 0.6);
            cairo_move_to(cr, width - pad_r + 6, gy + 4);
            cairo_show_text(cr, yr.c_str());
        }
    }

    // x-axis labels
    int step = std::max(1, (int)labels.size() / 10);
    set_font(cr, 9, false);
    for (size_t i = 0; i < labels.size(); i += step) {
        double lx = (labels.size() == 1) ? (pad_l + chart_w / 2) : (pad_l + (double)i / (labels.size() - 1) * chart_w);
        set_colour(cr, COL_DIM);
        cairo_move_to(cr, lx - text_width(cr, labels[i]) / 2, height - pad_b + 16);
        cairo_show_text(cr, labels[i].c_str());
    }

    // series lines + fill
    for (auto& s : series) {
        if (s.values.empty()) continue;
        double s_max = (s.axis == 0) ? max_left : max_right;
        cairo_set_source_rgba(cr, s.colour.r, s.colour.g, s.colour.b, 0.9);
        cairo_set_line_width(cr, 2);

        if (s.values.size() == 1) {
            double px = pad_l + chart_w / 2;
            double py = pad_t + chart_h * (1.0 - s.values[0] / s_max);
            
            // Draw a flat horizontal line for context
            cairo_move_to(cr, pad_l, py);
            cairo_line_to(cr, pad_l + chart_w, py);
            cairo_stroke_preserve(cr);

            // Fill area under line
            cairo_line_to(cr, pad_l + chart_w, pad_t + chart_h);
            cairo_line_to(cr, pad_l,           pad_t + chart_h);
            cairo_close_path(cr);
            cairo_set_source_rgba(cr, s.colour.r, s.colour.g, s.colour.b, 0.12);
            cairo_fill(cr);

            // Centered dot
            cairo_set_source_rgba(cr, s.colour.r, s.colour.g, s.colour.b, 1.0);
            cairo_arc(cr, px, py, 4, 0, 2 * M_PI);
            cairo_fill(cr);
        } else {
            for (size_t i = 0; i < s.values.size(); ++i) {
                double px = pad_l + (double)i / (s.values.size() - 1) * chart_w;
                double py = pad_t + chart_h * (1.0 - s.values[i] / s_max);
                if (i == 0) cairo_move_to(cr, px, py);
                else        cairo_line_to(cr, px, py);
            }
            cairo_stroke_preserve(cr);

            cairo_line_to(cr, pad_l + chart_w, pad_t + chart_h);
            cairo_line_to(cr, pad_l,           pad_t + chart_h);
            cairo_close_path(cr);
            cairo_set_source_rgba(cr, s.colour.r, s.colour.g, s.colour.b, 0.12);
            cairo_fill(cr);

            // dots
            cairo_set_source_rgba(cr, s.colour.r, s.colour.g, s.colour.b, 1.0);
            for (size_t i = 0; i < s.values.size(); ++i) {
                double px = pad_l + (double)i / (s.values.size() - 1) * chart_w;
                double py = pad_t + chart_h * (1.0 - s.values[i] / s_max);
                cairo_arc(cr, px, py, 3, 0, 2 * M_PI);
                cairo_fill(cr);
            }
        }
    }

    // legend
    if (series.size() > 1) {
        double lx = pad_l, ly = height - 8;
        set_font(cr, 10, false);
        for (auto& s : series) {
            cairo_set_source_rgba(cr, s.colour.r, s.colour.g, s.colour.b, 1.0);
            cairo_arc(cr, lx + 5, ly - 4, 4, 0, 2 * M_PI);
            cairo_fill(cr);
            lx += 14;
            set_colour(cr, COL_DIM);
            cairo_move_to(cr, lx, ly);
            cairo_show_text(cr, s.name.c_str());
            lx += text_width(cr, s.name) + 16;
        }
    }

    PngBuffer buf;
    cairo_surface_write_to_png_stream(surface, png_write_cb, &buf);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return buf.data;
}

// ========================================================================
//  render_summary_card
//  wide card with N big stat numbers
// ========================================================================
inline std::string render_summary_card(
        const std::string& title,
        const std::vector<std::pair<std::string, std::string>>& stats,
        int width = 600, int height = 160)
{
    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    auto* cr      = cairo_create(surface);

    round_rect(cr, 0, 0, width, height, 12);
    set_colour(cr, COL_CARD);
    cairo_fill(cr);

    set_font(cr, 14, true);
    set_colour(cr, COL_TEXT);
    cairo_move_to(cr, 20, 28);
    cairo_show_text(cr, title.c_str());

    int n = (int)stats.size();
    if (!n) { cairo_destroy(cr); cairo_surface_destroy(surface); return {}; }
    double box_w = (width - 40.0) / n;

    for (int i = 0; i < n; ++i) {
        double bx = 20 + i * box_w, by = 48;
        if (i > 0) {
            set_colour(cr, COL_BORDER);
            cairo_set_line_width(cr, 1);
            cairo_move_to(cr, bx, by);
            cairo_line_to(cr, bx, by + 80);
            cairo_stroke(cr);
        }
        set_font(cr, 24, true);
        set_colour(cr, PALETTE[i % PALETTE.size()]);
        cairo_move_to(cr, bx + 12, by + 36);
        cairo_show_text(cr, stats[i].second.c_str());

        set_font(cr, 10, false);
        set_colour(cr, COL_DIM);
        cairo_move_to(cr, bx + 12, by + 58);
        cairo_show_text(cr, stats[i].first.c_str());
    }

    PngBuffer buf;
    cairo_surface_write_to_png_stream(surface, png_write_cb, &buf);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return buf.data;
}

// ========================================================================
//  render_pie_chart
//  circular pie with right-side legend
// ========================================================================
inline std::string render_pie_chart(
        const std::string& title,
        const std::vector<PieSlice>& slices,
        int width = 500, int height = 400)
{
    if (slices.empty()) return {};

    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    auto* cr      = cairo_create(surface);

    round_rect(cr, 0, 0, width, height, 12);
    set_colour(cr, COL_CARD);
    cairo_fill(cr);

    double y_start = 20;
    if (!title.empty()) {
        set_font(cr, 14, true);
        set_colour(cr, COL_TEXT);
        cairo_move_to(cr, 20, y_start + 14);
        cairo_show_text(cr, title.c_str());
        y_start += 36;
    }

    double total = 0;
    for (auto& s : slices) total += s.value;
    if (total <= 0) { cairo_destroy(cr); cairo_surface_destroy(surface); return {}; }

    double cx     = width * 0.35;
    double cy     = y_start + (height - y_start) * 0.5;
    double radius = std::min(cx - 30, (height - y_start) * 0.5 - 20);
    if (radius < 30) radius = 30;

    double angle = -M_PI / 2;
    for (size_t i = 0; i < slices.size(); ++i) {
        double sweep = (slices[i].value / total) * 2 * M_PI;
        RGBA col = slices[i].colour.a > 0 ? slices[i].colour : PALETTE[i % PALETTE.size()];
        cairo_set_source_rgba(cr, col.r, col.g, col.b, 0.85);
        cairo_move_to(cr, cx, cy);
        cairo_arc(cr, cx, cy, radius, angle, angle + sweep);
        cairo_close_path(cr);
        cairo_fill(cr);
        angle += sweep;
    }

    // legend
    double lx = width * 0.65, ly = y_start + 10;
    set_font(cr, 10, false);
    for (size_t i = 0; i < slices.size(); ++i) {
        RGBA col = slices[i].colour.a > 0 ? slices[i].colour : PALETTE[i % PALETTE.size()];
        cairo_set_source_rgba(cr, col.r, col.g, col.b, 1.0);
        round_rect(cr, lx, ly, 12, 12, 2);
        cairo_fill(cr);
        set_colour(cr, COL_TEXT);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f%%", (slices[i].value / total) * 100.0);
        std::string leg = slices[i].label + " " + buf;
        cairo_move_to(cr, lx + 18, ly + 10);
        cairo_show_text(cr, leg.c_str());
        ly += 20;
        if (ly > height - 20) break;
    }

    PngBuffer pbuf;
    cairo_surface_write_to_png_stream(surface, png_write_cb, &pbuf);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return pbuf.data;
}

// ========================================================================
//  render_stacked_bar_chart
//  vertical stacked bars with legend
// ========================================================================
inline std::string render_stacked_bar_chart(
        const std::string& title,
        const std::vector<std::string>& labels,
        const std::vector<BarSeries>& series,
        int width = 600, int height = 400)
{
    if (labels.empty() || series.empty()) return {};

    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    auto* cr      = cairo_create(surface);

    round_rect(cr, 0, 0, width, height, 12);
    set_colour(cr, COL_CARD);
    cairo_fill(cr);

    const int pad_l = 58, pad_r = 20, pad_t = 48, pad_b = 50;
    double chart_w = width  - pad_l - pad_r;
    double chart_h = height - pad_t - pad_b;

    if (!title.empty()) {
        set_font(cr, 14, true);
        set_colour(cr, COL_TEXT);
        cairo_move_to(cr, pad_l, 28);
        cairo_show_text(cr, title.c_str());
    }

    int64_t max_stack = 1;
    for (size_t c = 0; c < labels.size(); ++c) {
        int64_t stack = 0;
        for (auto& s : series) if (c < s.values.size()) stack += s.values[c];
        max_stack = std::max(max_stack, stack);
    }
    double nice  = std::pow(10, std::floor(std::log10((double)max_stack)));
    double max_v = std::ceil((double)max_stack / nice) * nice;
    if (max_v < 5) max_v = 5;

    for (int i = 0; i <= 4; ++i) {
        double gy = pad_t + chart_h * (1.0 - i / 4.0);
        set_colour(cr, COL_GRID);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, pad_l, gy); cairo_line_to(cr, width - pad_r, gy);
        cairo_stroke(cr);
        set_font(cr, 9, false);
        set_colour(cr, COL_DIM);
        std::string yl = fmt_num(static_cast<int64_t>(max_v * i / 4.0));
        cairo_move_to(cr, pad_l - text_width(cr, yl) - 6, gy + 4);
        cairo_show_text(cr, yl.c_str());
    }

    double bar_w = chart_w / labels.size() * 0.7;
    double gap_w = chart_w / labels.size() * 0.3;

    for (size_t c = 0; c < labels.size(); ++c) {
        double bx = pad_l + c * (bar_w + gap_w) + gap_w / 2;
        double sy = pad_t + chart_h;
        for (size_t si = 0; si < series.size(); ++si) {
            int64_t val = (c < series[si].values.size()) ? series[si].values[c] : 0;
            if (val <= 0) continue;
            double bh = (val / max_v) * chart_h;
            sy -= bh;
            cairo_set_source_rgba(cr, series[si].colour.r, series[si].colour.g, series[si].colour.b, 0.8);
            round_rect(cr, bx, sy, bar_w, bh, 2);
            cairo_fill(cr);
        }
        set_font(cr, 9, false);
        set_colour(cr, COL_DIM);
        std::string lbl = labels[c];
        lbl = shorten_utf8_label(lbl, 8);
        cairo_move_to(cr, bx + bar_w / 2 - text_width(cr, lbl) / 2, height - pad_b + 16);
        cairo_show_text(cr, lbl.c_str());
    }

    if (series.size() > 1) {
        double lx = pad_l, ly = height - 8;
        set_font(cr, 10, false);
        for (auto& s : series) {
            cairo_set_source_rgba(cr, s.colour.r, s.colour.g, s.colour.b, 1.0);
            round_rect(cr, lx, ly - 10, 12, 12, 2);
            cairo_fill(cr);
            lx += 16;
            set_colour(cr, COL_DIM);
            cairo_move_to(cr, lx, ly);
            cairo_show_text(cr, s.name.c_str());
            lx += text_width(cr, s.name) + 14;
        }
    }

    PngBuffer buf;
    cairo_surface_write_to_png_stream(surface, png_write_cb, &buf);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return buf.data;
}

// ========================================================================
//  render_heatmap
//  rows × cols coloured grid — hour-of-day × day-of-week activity
//  logarithmic scaling so small values don't disappear
// ========================================================================
inline std::string render_heatmap(
        const std::string& title,
        const std::vector<std::string>& row_labels,
        const std::vector<std::string>& col_labels,
        const std::vector<std::vector<double>>& matrix,
        int width = 700, int height = 0,
        bool use_log = true)
{
    int rows = (int)row_labels.size();
    int cols = (int)col_labels.size();
    if (!rows || !cols) return {};

    const int pad        = 20;
    const int title_h    = title.empty() ? 0 : 36;
    const int row_lbl_w  = 50;
    const int col_lbl_h  = 24;
    const int cell_w     = std::max(18, (width - pad * 2 - row_lbl_w) / cols);
    const int cell_h     = 22;
    if (height <= 0)
        height = pad * 2 + title_h + col_lbl_h + rows * cell_h + 10;

    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    auto* cr      = cairo_create(surface);

    round_rect(cr, 0, 0, width, height, 12);
    set_colour(cr, COL_CARD);
    cairo_fill(cr);

    double y = pad;
    if (!title.empty()) {
        set_font(cr, 14, true);
        set_colour(cr, COL_TEXT);
        cairo_move_to(cr, pad, y + 14);
        cairo_show_text(cr, title.c_str());
        y += title_h;
    }

    double max_val = 1;
    for (auto& row : matrix) for (auto v : row) max_val = std::max(max_val, v);
    double log_max = use_log ? std::log(max_val + 1) : max_val;

    // column labels
    set_font(cr, 8, false);
    for (int c = 0; c < cols; ++c) {
        double cx = pad + row_lbl_w + c * cell_w + cell_w / 2;
        set_colour(cr, COL_DIM);
        cairo_move_to(cr, cx - text_width(cr, col_labels[c]) / 2, y + 12);
        cairo_show_text(cr, col_labels[c].c_str());
    }
    y += col_lbl_h;

    for (int r = 0; r < rows; ++r) {
        set_font(cr, 9, false);
        set_colour(cr, COL_DIM);
        cairo_move_to(cr, pad, y + cell_h * 0.7);
        cairo_show_text(cr, row_labels[r].c_str());

        for (int c = 0; c < cols; ++c) {
            double val = (r < (int)matrix.size() && c < (int)matrix[r].size()) ? matrix[r][c] : 0;
            double intensity = use_log
                ? (log_max > 0 ? std::log(val + 1) / log_max : 0)
                : (max_val > 0 ? val / max_val : 0);

            double lr = COL_CARD.r + (COL_ACCENT.r - COL_CARD.r) * intensity;
            double lg = COL_CARD.g + (COL_ACCENT.g - COL_CARD.g) * intensity;
            double lb = COL_CARD.b + (COL_ACCENT.b - COL_CARD.b) * intensity;
            double la = use_log ? 0.3 + 0.7 * intensity : 0.5 + 0.5 * intensity;
            cairo_set_source_rgba(cr, lr, lg, lb, la);

            double cx = pad + row_lbl_w + c * cell_w;
            round_rect(cr, cx + 1, y + 1, cell_w - 2, cell_h - 2, 3);
            cairo_fill(cr);

            if (intensity > (use_log ? 0.2 : 0.4) && val > 0) {
                set_font(cr, 7, false);
                set_colour(cr, COL_TEXT);
                std::string vs = std::to_string((int)val);
                cairo_move_to(cr, cx + cell_w / 2 - text_width(cr, vs) / 2, y + cell_h * 0.7);
                cairo_show_text(cr, vs.c_str());
            }
        }
        y += cell_h;
    }

    PngBuffer buf;
    cairo_surface_write_to_png_stream(surface, png_write_cb, &buf);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return buf.data;
}

// ========================================================================
//  render_scatter_chart
//  scatter plot with multiple named series
// ========================================================================
inline std::string render_scatter_chart(
        const std::string& title,
        const std::vector<ScatterSeries>& series,
        const std::string& x_label = "",
        const std::string& y_label = "",
        int width = 600, int height = 400)
{
    if (series.empty()) return {};

    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    auto* cr      = cairo_create(surface);

    round_rect(cr, 0, 0, width, height, 12);
    set_colour(cr, COL_CARD);
    cairo_fill(cr);

    const int pad_l = 58, pad_r = 20, pad_t = 48, pad_b = 50;
    double chart_w = width  - pad_l - pad_r;
    double chart_h = height - pad_t - pad_b;

    if (!title.empty()) {
        set_font(cr, 14, true);
        set_colour(cr, COL_TEXT);
        cairo_move_to(cr, pad_l, 28);
        cairo_show_text(cr, title.c_str());
    }

    double x_min = 1e18, x_max = -1e18, y_min = 1e18, y_max = -1e18;
    for (auto& s : series)
        for (auto& p : s.points) {
            x_min = std::min(x_min, p.x); x_max = std::max(x_max, p.x);
            y_min = std::min(y_min, p.y); y_max = std::max(y_max, p.y);
        }
    if (x_min == x_max) x_max = x_min + 1;
    if (y_min == y_max) y_max = y_min + 1;
    double xr = x_max - x_min, yr = y_max - y_min;
    x_min -= xr * 0.05; x_max += xr * 0.05;
    y_min -= yr * 0.05; y_max += yr * 0.05;
    if (y_min > 0) y_min = 0;

    for (int i = 0; i <= 4; ++i) {
        double gy = pad_t + chart_h * (1.0 - i / 4.0);
        set_colour(cr, COL_GRID);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, pad_l, gy); cairo_line_to(cr, width - pad_r, gy);
        cairo_stroke(cr);
        set_font(cr, 9, false);
        set_colour(cr, COL_DIM);
        double yv = y_min + (y_max - y_min) * i / 4.0;
        std::string yl = fmt_num(static_cast<int64_t>(yv));
        cairo_move_to(cr, pad_l - text_width(cr, yl) - 6, gy + 4);
        cairo_show_text(cr, yl.c_str());
    }

    if (!x_label.empty()) {
        set_font(cr, 10, false); set_colour(cr, COL_DIM);
        cairo_move_to(cr, pad_l + chart_w / 2 - text_width(cr, x_label) / 2, height - 8);
        cairo_show_text(cr, x_label.c_str());
    }
    if (!y_label.empty()) {
        set_font(cr, 10, false); set_colour(cr, COL_DIM);
        cairo_save(cr);
        cairo_move_to(cr, 14, pad_t + chart_h / 2);
        cairo_rotate(cr, -M_PI / 2);
        cairo_show_text(cr, y_label.c_str());
        cairo_restore(cr);
    }

    for (auto& s : series)
        for (auto& p : s.points) {
            double px = pad_l + (p.x - x_min) / (x_max - x_min) * chart_w;
            double py = pad_t + chart_h * (1.0 - (p.y - y_min) / (y_max - y_min));
            cairo_set_source_rgba(cr, s.colour.r, s.colour.g, s.colour.b, 0.8);
            cairo_arc(cr, px, py, 4, 0, 2 * M_PI);
            cairo_fill(cr);
        }

    if (series.size() > 1) {
        double lx = pad_l, ly = height - 8;
        set_font(cr, 10, false);
        for (auto& s : series) {
            cairo_set_source_rgba(cr, s.colour.r, s.colour.g, s.colour.b, 1.0);
            cairo_arc(cr, lx + 5, ly - 4, 4, 0, 2 * M_PI);
            cairo_fill(cr);
            lx += 14;
            set_colour(cr, COL_DIM);
            cairo_move_to(cr, lx, ly);
            cairo_show_text(cr, s.name.c_str());
            lx += text_width(cr, s.name) + 16;
        }
    }

    PngBuffer buf;
    cairo_surface_write_to_png_stream(surface, png_write_cb, &buf);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return buf.data;
}

// ========================================================================
//  render_rank_card
//  Sleek image rank card containing: avatar (circular), username, level,
//  rank, and progress bar showing progress to next level.
// ========================================================================
inline std::string render_rank_card(
        const std::string& username,
        uint32_t level,
        int rank,
        int total_users,
        uint64_t current_xp,
        uint64_t next_xp,
        uint64_t base_xp,
        const std::string& avatar_path)
{
    const int width = 800;
    const int height = 220;

    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    auto* cr      = cairo_create(surface);

    // Draw dark sleek background with rounded corners
    round_rect(cr, 0, 0, width, height, 16);
    set_colour(cr, COL_CARD);
    cairo_fill_preserve(cr);
    
    // Draw card border
    set_colour(cr, COL_BORDER);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    // Background subtle glow in top-right
    cairo_pattern_t* glow = cairo_pattern_create_radial(width - 50, 50, 10, width - 50, 50, 180);
    cairo_pattern_add_color_stop_rgba(glow, 0.0, COL_ACCENT.r, COL_ACCENT.g, COL_ACCENT.b, 0.15);
    cairo_pattern_add_color_stop_rgba(glow, 1.0, COL_ACCENT.r, COL_ACCENT.g, COL_ACCENT.b, 0.0);
    cairo_set_source(cr, glow);
    cairo_arc(cr, width - 50, 50, 180, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(glow);

    // CX, CY, Radius for circular avatar
    const double avatar_cx = 80.0;
    const double avatar_cy = 110.0;
    const double avatar_r  = 50.0;

    cairo_surface_t* avatar_surface = nullptr;
    if (!avatar_path.empty()) {
        avatar_surface = cairo_image_surface_create_from_png(avatar_path.c_str());
        if (cairo_surface_status(avatar_surface) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(avatar_surface);
            avatar_surface = nullptr;
        }
    }

    if (avatar_surface) {
        cairo_save(cr);
        cairo_arc(cr, avatar_cx, avatar_cy, avatar_r, 0, 2 * M_PI);
        cairo_clip(cr);
        
        int w = cairo_image_surface_get_width(avatar_surface);
        int h = cairo_image_surface_get_height(avatar_surface);
        
        cairo_translate(cr, avatar_cx - avatar_r, avatar_cy - avatar_r);
        cairo_scale(cr, (avatar_r * 2.0) / w, (avatar_r * 2.0) / h);
        cairo_set_source_surface(cr, avatar_surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
        
        cairo_surface_destroy(avatar_surface);
    } else {
        // Draw elegant gradient circle for fallback avatar
        cairo_save(cr);
        cairo_arc(cr, avatar_cx, avatar_cy, avatar_r, 0, 2 * M_PI);
        cairo_clip(cr);
        
        cairo_pattern_t* grad = cairo_pattern_create_linear(avatar_cx - avatar_r, avatar_cy - avatar_r, 
                                                            avatar_cx + avatar_r, avatar_cy + avatar_r);
        cairo_pattern_add_color_stop_rgba(grad, 0.0, COL_ACCENT.r, COL_ACCENT.g, COL_ACCENT.b, 1.0);
        cairo_pattern_add_color_stop_rgba(grad, 1.0, COL_BLUE.r, COL_BLUE.g, COL_BLUE.b, 1.0);
        cairo_set_source(cr, grad);
        cairo_paint(cr);
        cairo_pattern_destroy(grad);
        cairo_restore(cr);

        // Render first letter of username
        std::string initial = username.empty() ? "?" : username.substr(0, 1);
        std::transform(initial.begin(), initial.end(), initial.begin(), ::toupper);
        set_font(cr, 42, true);
        set_colour(cr, {1.0, 1.0, 1.0, 1.0});
        double iw = text_width(cr, initial);
        cairo_move_to(cr, avatar_cx - iw / 2, avatar_cy + 15);
        cairo_show_text(cr, initial.c_str());
    }

    // Border around avatar
    cairo_set_line_width(cr, 2.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.15);
    cairo_arc(cr, avatar_cx, avatar_cy, avatar_r + 1.0, 0, 2 * M_PI);
    cairo_stroke(cr);

    // Draw user name
    std::string name_display = shorten_utf8_label(username, 24);
    set_font(cr, 28, true);
    set_colour(cr, {1.0, 1.0, 1.0, 0.95});
    cairo_move_to(cr, 160, 75);
    cairo_show_text(cr, name_display.c_str());

    // Progress percentage
    double pct = 0.0;
    uint64_t xp_in_level = 0;
    uint64_t xp_required_for_level = 0;
    if (next_xp > base_xp && current_xp >= base_xp) {
        xp_in_level = current_xp - base_xp;
        xp_required_for_level = next_xp - base_xp;
        pct = (double)xp_in_level / xp_required_for_level;
    }
    if (pct > 1.0) pct = 1.0;
    if (pct < 0.0) pct = 0.0;

    // Draw Level Info
    set_font(cr, 18, true);
    set_colour(cr, COL_ACCENT);
    cairo_move_to(cr, 160, 115);
    std::string lvl_str = "LEVEL " + std::to_string(level);
    cairo_show_text(cr, lvl_str.c_str());

    // Draw Rank Info
    set_font(cr, 15, false);
    set_colour(cr, COL_DIM);
    cairo_move_to(cr, 160 + text_width(cr, lvl_str) + 20, 115);
    std::string rnk_str = "RANK #" + std::to_string(rank) + " OF " + std::to_string(total_users);
    cairo_show_text(cr, rnk_str.c_str());

    // Draw XP text (right-aligned)
    set_font(cr, 14, false);
    set_colour(cr, COL_DIM);
    std::string xp_str = fmt_num(xp_in_level) + " / " + fmt_num(xp_required_for_level) + " XP (" + std::to_string((int)(pct * 100)) + "%)";
    double xw = text_width(cr, xp_str);
    cairo_move_to(cr, 760 - xw, 115);
    cairo_show_text(cr, xp_str.c_str());

    // Progress Bar Coordinates
    const double bar_x = 160.0;
    const double bar_y = 135.0;
    const double bar_w = 600.0;
    const double bar_h = 18.0;
    const double bar_r = 9.0;

    // Background capsule of progress bar
    round_rect(cr, bar_x, bar_y, bar_w, bar_h, bar_r);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
    cairo_fill(cr);

    // Progress bar fill (gradient)
    if (pct > 0.0) {
        cairo_save(cr);
        // Clip to the fill region
        round_rect(cr, bar_x, bar_y, pct * bar_w, bar_h, bar_r);
        cairo_clip(cr);

        cairo_pattern_t* pat = cairo_pattern_create_linear(bar_x, 0, bar_x + bar_w, 0);
        cairo_pattern_add_color_stop_rgba(pat, 0.0, COL_ACCENT.r, COL_ACCENT.g, COL_ACCENT.b, 1.0);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, 0.486, 0.227, 0.929, 1.0);
        cairo_set_source(cr, pat);
        
        round_rect(cr, bar_x, bar_y, pct * bar_w, bar_h, bar_r);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
        cairo_restore(cr);
    }

    PngBuffer buf;
    cairo_surface_write_to_png_stream(surface, png_write_cb, &buf);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return buf.data;
}

} // namespace chart
} // namespace bronx

#undef cairo_show_text
