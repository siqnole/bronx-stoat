#pragma once
// ============================================================
//  captcha.h — Interactive OCR-resistant CAPTCHA generator
// ============================================================

#include <cairo/cairo.h>
#include <string>
#include <vector>
#include <random>
#include <cmath>
#include <chrono>
#include <algorithm>
#include "stoatpp/bot_module.h"
#include "chart_renderer.h"
#include "helpers.h"

namespace bronx::cogs {

inline std::string generate_captcha_text(int length) {
    // Exclude confusing characters like O, 0, I, 1, L
    const std::string chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, chars.length() - 1);
    std::string text = "";
    for (int i = 0; i < length; ++i) {
        text += chars[dis(gen)];
    }
    return text;
}

struct CaptchaPngBuffer {
    std::string data;
};

inline cairo_status_t captcha_png_write_cb(void* closure, const unsigned char* data, unsigned int length) {
    auto* buf = static_cast<CaptchaPngBuffer*>(closure);
    buf->data.append(reinterpret_cast<const char*>(data), length);
    return CAIRO_STATUS_SUCCESS;
}

inline std::string render_captcha_image(const std::string& text) {
    const int width = 350;
    const int height = 120;

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(surface);

    std::random_device rd;
    std::mt19937 gen(rd());

    // 1. Draw background color (light greyish/beige to reduce contrast of text slightly)
    std::uniform_real_distribution<double> bg_color_dist(0.85, 0.95);
    cairo_set_source_rgb(cr, bg_color_dist(gen), bg_color_dist(gen), bg_color_dist(gen));
    cairo_paint(cr);

    // 2. Draw background noise grids/lines
    std::uniform_real_distribution<double> noise_coord(0.0, 1.0);
    std::uniform_real_distribution<double> line_width_dist(1.0, 2.5);
    std::uniform_real_distribution<double> color_dist(0.3, 0.7);

    // Grid/random distractor lines
    int line_count = 25;
    for (int i = 0; i < line_count; ++i) {
        cairo_set_source_rgba(cr, color_dist(gen), color_dist(gen), color_dist(gen), 0.4);
        cairo_set_line_width(cr, line_width_dist(gen));
        cairo_move_to(cr, noise_coord(gen) * width, noise_coord(gen) * height);
        cairo_line_to(cr, noise_coord(gen) * width, noise_coord(gen) * height);
        cairo_stroke(cr);
    }

    // Draw some distractor circles
    int circle_count = 15;
    for (int i = 0; i < circle_count; ++i) {
        cairo_set_source_rgba(cr, color_dist(gen), color_dist(gen), color_dist(gen), 0.35);
        cairo_set_line_width(cr, line_width_dist(gen));
        double cx = noise_coord(gen) * width;
        double cy = noise_coord(gen) * height;
        double r = 5.0 + noise_coord(gen) * 20.0;
        cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
        if (i % 2 == 0) {
            cairo_fill(cr);
        } else {
            cairo_stroke(cr);
        }
    }

    // 3. Render captcha text character-by-character with random font, size, rotation, and offset
    std::uniform_real_distribution<double> font_size_dist(38.0, 48.0);
    std::uniform_real_distribution<double> rotation_dist(-0.45, 0.45); // ~ -25 to +25 degrees
    std::uniform_real_distribution<double> y_offset_dist(65.0, 85.0);
    std::vector<std::string> font_families = {"sans-serif", "serif", "monospace"};
    std::uniform_int_distribution<size_t> font_family_dist(0, font_families.size() - 1);

    double x_step = (double)width / (text.length() + 1);
    for (size_t i = 0; i < text.length(); ++i) {
        cairo_save(cr);

        // Select random font settings
        std::string font_family = font_families[font_family_dist(gen)];
        cairo_select_font_face(cr, font_family.c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, font_size_dist(gen));

        // Set random color (dark/medium shades to keep readable but distorted)
        std::uniform_real_distribution<double> text_color_dist(0.0, 0.4);
        cairo_set_source_rgb(cr, text_color_dist(gen), text_color_dist(gen), text_color_dist(gen));

        // Move and rotate
        double char_x = x_step * (i + 0.6) + (noise_coord(gen) - 0.5) * 10.0;
        double char_y = y_offset_dist(gen);
        cairo_translate(cr, char_x, char_y);
        cairo_rotate(cr, rotation_dist(gen));

        // Draw the character
        std::string ch = text.substr(i, 1);
        cairo_show_text(cr, ch.c_str());

        cairo_restore(cr);
    }

    // 4. Draw thick foreground noise lines over the text to cut through the characters
    int overlay_lines = 5;
    for (int i = 0; i < overlay_lines; ++i) {
        cairo_set_source_rgba(cr, color_dist(gen), color_dist(gen), color_dist(gen), 0.65);
        cairo_set_line_width(cr, 2.5 + noise_coord(gen) * 2.0);
        cairo_move_to(cr, noise_coord(gen) * (width / 4), noise_coord(gen) * height);
        cairo_line_to(cr, width - noise_coord(gen) * (width / 4), noise_coord(gen) * height);
        cairo_stroke(cr);
    }

    // Save to memory buffer
    CaptchaPngBuffer buf;
    cairo_surface_write_to_png_stream(surface, captcha_png_write_cb, &buf);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    return buf.data;
}

class CaptchaModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "captcha",
            {},
            "starts a CAPTCHA test to verify you are human.",
            "captcha",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> len_dist(5, 7);
                int length = len_dist(gen);

                std::string captcha_code = generate_captcha_text(length);
                std::string png_bytes = render_captcha_image(captcha_code);

                if (png_bytes.empty()) {
                    bot.send_message(msg.channel_id, "❌ Failed to render CAPTCHA image.");
                    return;
                }

                // Upload to Autumn CDN
                std::string file_id = chart::upload_chart(bot, png_bytes, "captcha.png");
                if (file_id.empty()) {
                    bot.send_message(msg.channel_id, "❌ Failed to upload CAPTCHA to CDN.");
                    return;
                }

                // Send embed with captcha attachment
                stoatpp::models::MessagePayload payload;
                payload.content = "⚠️ **Verification Required**\n"
                                  "Please type the letters and numbers shown in the image below to prove you are human.\n"
                                  "You have **45 seconds** to reply!";
                payload.add_attachment(file_id);

                bot.send_message(msg.channel_id, payload);

                // Wait for the correct response
                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [this, &bot, msg, captcha_code](std::optional<stoatpp::events::Message> reply) {
                    if (!reply) {
                        bot.send_message(msg.channel_id, "⏱️ CAPTCHA verification timed out. Action cancelled.");
                        return;
                    }

                    std::string user_input = reply->content;
                    // Remove whitespace and convert to uppercase for robust validation
                    user_input.erase(std::remove_if(user_input.begin(), user_input.end(), ::isspace), user_input.end());
                    std::transform(user_input.begin(), user_input.end(), user_input.begin(), ::toupper);

                    if (user_input == captcha_code) {
                        bot.send_message(msg.channel_id, "✅ **Verification Successful!** You have passed the CAPTCHA check.");
                    } else {
                        bot.send_message(msg.channel_id, "❌ **Verification Failed!** The text you entered did not match. Please run `.captcha` to try again.");
                    }
                });
            },
            "Utility"
        });
    }
};

} // namespace bronx::cogs
