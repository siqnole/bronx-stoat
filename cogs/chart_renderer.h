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

// ── text helpers (cairo toy font) ──────────────────────────────
inline void set_font(cairo_t* cr, double size, bool bold = false) {
    cairo_select_font_face(cr, "sans-serif",
        CAIRO_FONT_SLANT_NORMAL,
        bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
}
inline double text_width(cairo_t* cr, const std::string& s) {
    cairo_text_extents_t ext;
    cairo_text_extents(cr, s.c_str(), &ext);
    return ext.x_advance;
}

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
        if (lbl.size() > 16) lbl = lbl.substr(0, 14) + "..";
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
        if (lbl.size() > 8) lbl = lbl.substr(0, 6) + "..";
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

} // namespace chart
} // namespace bronx
