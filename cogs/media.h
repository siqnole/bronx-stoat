#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <random>
#include <chrono>
#include <thread>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

namespace bronx::cogs {

// ---------------------------------------------------------------------------
// Shell escape helper
// ---------------------------------------------------------------------------
inline std::string shell_escape(const std::string& str) {
    std::string res;
    res += "'";
    for (char c : str) {
        if (c == '\'') {
            res += "'\\''";
        } else {
            res += c;
        }
    }
    res += "'";
    return res;
}

// ---------------------------------------------------------------------------
// URL helper
// ---------------------------------------------------------------------------
inline std::string find_url(const std::string& text) {
    size_t pos = text.find("http://");
    if (pos == std::string::npos) {
        pos = text.find("https://");
    }
    if (pos == std::string::npos) return "";
    
    size_t end = text.find_first_of(" \n\r\t<>[]()", pos);
    if (end == std::string::npos) {
        return text.substr(pos);
    }
    return text.substr(pos, end - pos);
}

inline std::string resolve_message_media_url(stoatpp::cluster& bot, const stoatpp::events::Message& msg) {
    if (msg.raw.contains("attachments") && msg.raw["attachments"].is_array() && !msg.raw["attachments"].empty()) {
        auto first_attach = msg.raw["attachments"][0];
        std::string tag = first_attach.value("tag", "");
        std::string id = first_attach.value("id", "");
        if (id.empty()) id = first_attach.value("_id", "");
        std::string filename = first_attach.value("filename", "");
        
        if (!tag.empty() && !id.empty()) {
            return bot.config().cdn_url + "/" + tag + "/" + id + "/" + filename;
        }
    }
    
    std::string url = find_url(msg.content);
    if (!url.empty()) return url;
    
    return "";
}

inline std::string resolve_message_media_url(stoatpp::cluster& bot, const stoatpp::models::Message& msg) {
    if (msg.attachments.is_array() && !msg.attachments.empty()) {
        auto first_attach = msg.attachments[0];
        std::string tag = first_attach.value("tag", "");
        std::string id = first_attach.value("id", "");
        if (id.empty()) id = first_attach.value("_id", "");
        std::string filename = first_attach.value("filename", "");
        
        if (!tag.empty() && !id.empty()) {
            return bot.config().cdn_url + "/" + tag + "/" + id + "/" + filename;
        }
    }
    
    if (msg.content) {
        std::string url = find_url(*msg.content);
        if (!url.empty()) return url;
    }
    
    return "";
}

// ---------------------------------------------------------------------------
// Catbox uploader
// ---------------------------------------------------------------------------
inline std::string upload_to_catbox(const std::string& file_path) {
    std::string temp_res = "/tmp/catbox_res_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".txt";
    std::string escaped_file = shell_escape(file_path);
    std::string escaped_res = shell_escape(temp_res);
    std::string cmd = "curl -s -F \"reqtype=fileupload\" -F \"fileToUpload=@" + escaped_file + "\" https://catbox.moe/user/api.php > " + escaped_res + " 2>&1";
    
    int status = std::system(cmd.c_str());
    std::string url = "";
    
    if (status == 0 && std::filesystem::exists(temp_res)) {
        std::ifstream ifs(temp_res);
        std::getline(ifs, url);
        url.erase(url.find_last_not_of(" \n\r\t") + 1);
    }
    
    if (std::filesystem::exists(temp_res)) std::filesystem::remove(temp_res);
    return url;
}

// ---------------------------------------------------------------------------
// Metadata fetcher
// ---------------------------------------------------------------------------
struct MediaMetadata {
    std::string title = "Unknown Title";
    std::string description = "";
    std::string views = "0";
    std::string likes = "0";
    std::string comments = "0";
    std::string thumbnail = "";
    std::string uploader = "Unknown";
    bool success = false;
};

inline MediaMetadata fetch_metadata(const std::string& url) {
    MediaMetadata meta;
    std::string format_esc = shell_escape("%(title)s|%(view_count)s|%(like_count)s|%(comment_count)s|%(thumbnail)s|%(uploader)s|%(description).1000s");
    std::string url_esc = shell_escape(url);
    std::string cmd = "yt-dlp --cache-dir /tmp/yt-dlp-cache --no-check-certificates --print " + format_esc + " " + url_esc + " 2>/dev/null";
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return meta;
    
    char buffer[2048];
    std::string result = "";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    pclose(pipe);
    
    if (result.empty()) return meta;
    
    std::stringstream ss(result);
    std::string segment;
    std::vector<std::string> parts;
    while (std::getline(ss, segment, '|')) {
        parts.push_back(segment);
    }
    
    if (parts.size() >= 6) {
        meta.title = parts[0];
        meta.views = (parts[1] == "NA" || parts[1].empty()) ? "0" : parts[1];
        meta.likes = (parts[2] == "NA" || parts[2].empty()) ? "0" : parts[2];
        meta.comments = (parts[3] == "NA" || parts[3].empty()) ? "0" : parts[3];
        meta.thumbnail = parts[4];
        meta.uploader = parts[5];
        if (parts.size() >= 7) meta.description = parts[6];
        meta.success = true;
    }
    
    return meta;
}

// ---------------------------------------------------------------------------
// File downloader helper
// ---------------------------------------------------------------------------
inline bool download_file_system(const std::string& url, const std::string& output_path) {
    std::string cmd = "curl -L -s -o " + shell_escape(output_path) + " " + shell_escape(url);
    int status = std::system(cmd.c_str());
    return (status == 0 && std::filesystem::exists(output_path) && std::filesystem::file_size(output_path) > 0);
}

// ---------------------------------------------------------------------------
// OCR Implementation
// ---------------------------------------------------------------------------
inline void process_ocr_request(stoatpp::cluster& bot, const std::string& url, uint64_t size, std::function<void(const std::string& error_msg, const std::vector<std::string>& pages)> callback) {
    if (size > 8 * 1024 * 1024) {
        callback("❌ image is too large (max 8MB).", {});
        return;
    }

    std::string scheme, host, path;
    std::string temp_url = url;
    if (temp_url.rfind("https://", 0) == 0) { scheme = "https://"; temp_url = temp_url.substr(8); }
    else if (temp_url.rfind("http://", 0) == 0) { scheme = "http://"; temp_url = temp_url.substr(7); }
    else {
        callback("❌ invalid image URL.", {});
        return;
    }
    
    auto slash = temp_url.find('/');
    if (slash == std::string::npos) {
        host = temp_url;
        path = "/";
    } else {
        host = temp_url.substr(0, slash);
        path = temp_url.substr(slash);
    }

    httplib::Client cli(scheme + host);
    cli.set_follow_location(true);
    cli.set_connection_timeout(std::chrono::milliseconds(10000));
    cli.set_read_timeout(std::chrono::milliseconds(10000));
    
    auto res = cli.Get(path);
    if (!res || res->status != 200) {
        callback("❌ failed to download image.", {});
        return;
    }

    tesseract::TessBaseAPI* api = new tesseract::TessBaseAPI();
    if (api->Init(nullptr, "eng")) {
        delete api;
        callback("❌ failed to initialize OCR engine.", {});
        return;
    }

    Pix* pix = pixReadMem((const unsigned char*)res->body.data(), res->body.size());
    if (!pix) {
        api->End();
        delete api;
        callback("❌ failed to process image format.", {});
        return;
    }

    api->SetImage(pix);
    char* outText = api->GetUTF8Text();
    std::string text(outText ? outText : "");
    delete[] outText;
    pixDestroy(&pix);
    api->End();
    delete api;

    text.erase(std::remove(text.begin(), text.end(), '\0'), text.end());
    
    size_t start = text.find_first_not_of(" \n\r\t");
    if (start != std::string::npos) {
        text = text.substr(start);
    } else {
        text.clear();
    }
    size_t end_idx = text.find_last_not_of(" \n\r\t");
    if (end_idx != std::string::npos) {
        text = text.substr(0, end_idx + 1);
    }

    if (text.empty()) {
        callback("❌ no text detected in the image.", {});
        return;
    }

    std::vector<std::string> pages;
    for (size_t i = 0; i < text.size(); i += 1900) {
        pages.push_back(text.substr(i, 1900));
    }
    callback("", pages);
}

// ---------------------------------------------------------------------------
// Media Module
// ---------------------------------------------------------------------------
class MediaModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        
        bot.register_command({
            "download",
            {"dl"},
            "Download media from social platforms (YouTube, TikTok, Instagram, Twitter/X, Reddit, etc.)",
            "",
            {"[url]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling download...\n";
                
                std::string target_url = "";
                if (!args.empty()) {
                    target_url = args[0];
                }

                // Shared state to pass the ack message ID into the worker thread
                auto ack_id = std::make_shared<std::string>("");
                auto ack_mutex = std::make_shared<std::mutex>();
                auto ack_cv = std::make_shared<std::condition_variable>();
                auto ack_ready = std::make_shared<bool>(false);

                bot.send_message(msg.channel_id, "⏳ handling video...", [ack_id, ack_mutex, ack_cv, ack_ready](stoatpp::models::Message sent, bool ok) {
                    std::lock_guard<std::mutex> lk(*ack_mutex);
                    if (ok) *ack_id = sent.id;
                    *ack_ready = true;
                    ack_cv->notify_one();
                });

                auto run_dl = [&bot, msg, target_url, ack_id, ack_mutex, ack_cv, ack_ready]() {
                    // Wait up to 3s for the ack message ID
                    {
                        std::unique_lock<std::mutex> lk(*ack_mutex);
                        ack_cv->wait_for(lk, std::chrono::seconds(3), [&]{ return *ack_ready; });
                    }

                    // Helper: send or edit — if we have an ack message, edit it; otherwise send fresh
                    auto reply_or_edit = [&](const std::string& content) {
                        std::string id;
                        { std::lock_guard<std::mutex> lk(*ack_mutex); id = *ack_id; }
                        if (!id.empty()) {
                            bot.edit_message(msg.channel_id, id, content);
                        } else {
                            bot.send_message(msg.channel_id, content);
                        }
                    };
                    auto reply_or_edit_payload = [&](const stoatpp::models::MessagePayload& payload) {
                        std::string id;
                        { std::lock_guard<std::mutex> lk(*ack_mutex); id = *ack_id; }
                        if (!id.empty()) {
                            bot.delete_message(msg.channel_id, id);
                        }
                        bot.send_message(msg.channel_id, payload);
                    };

                    std::string final_url = target_url;
                    
                    if (final_url.empty()) {
                        // If we replied to a message, get its media URL
                        if (!msg.replies.empty()) {
                            auto res = bot.rest().get_message(msg.channel_id, msg.replies[0]);
                            if (res.success()) {
                                auto replied_msg = stoatpp::models::Message::from_json(res.body);
                                final_url = resolve_message_media_url(bot, replied_msg);
                            }
                        }
                    }
                    
                    if (final_url.empty()) {
                        // Check current message attachments/content
                        final_url = resolve_message_media_url(bot, msg);
                    }
                    
                    if (final_url.empty() || final_url.find("http") == std::string::npos) {
                        reply_or_edit("❌ please provide a valid media URL or reply to a message containing one.");
                        return;
                    }
                    
                    bot.begin_typing(msg.channel_id);
                    
                    // Temp working directory
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<> dis(100000, 999999);
                    std::string id = std::to_string(dis(gen));
                    std::string temp_dir = "/tmp/bronx_dl_" + id;
                    std::filesystem::create_directories(temp_dir);
                    std::string out_path = temp_dir + "/video.mp4";
                    
                    // Fetch metadata
                    MediaMetadata meta = fetch_metadata(final_url);
                    
                    // Cobalt API Check
                    bool cobalt_success = false;
                    std::string cobalt_url = "";
                    bool use_cobalt = (final_url.find("tiktok.com") != std::string::npos || 
                                      final_url.find("instagram.com") != std::string::npos ||
                                      final_url.find("twitter.com") != std::string::npos ||
                                      final_url.find("x.com") != std::string::npos ||
                                      final_url.find("reddit.com") != std::string::npos);
                    
                    if (use_cobalt) {
                        std::vector<std::string> cobalt_instances = {
                            "https://api.cobalt.liubquanti.click",
                            "https://cobaltapi.kittycat.boo",
                            "https://cobalt.omega.wolfy.love",
                            "https://cobalt.alpha.wolfy.love",
                            "https://subito-c.meowing.de",
                            "https://nuko-c.meowing.de",
                            "https://lime.clxxped.lol",
                            "https://melon.clxxped.lol",
                            "https://grapefruit.clxxped.lol"
                        };
                        
                        for (const auto& instance : cobalt_instances) {
                            try {
                                httplib::Client cli(instance);
                                cli.set_follow_location(true);
                                cli.set_connection_timeout(std::chrono::milliseconds(5000));
                                cli.set_read_timeout(std::chrono::milliseconds(8000));
                                
                                httplib::Headers headers = {
                                    {"Accept", "application/json"},
                                    {"Content-Type", "application/json"}
                                };
                                
                                nlohmann::json body = {
                                    {"url", final_url},
                                    {"videoQuality", "720"},
                                    {"downloadMode", "auto"}
                                };
                                
                                auto res = cli.Post("/", headers, body.dump(), "application/json");
                                if (res && res->status == 200) {
                                    auto j = nlohmann::json::parse(res->body);
                                    if (j.contains("url") && j["url"].is_string()) {
                                        cobalt_url = j["url"];
                                        cobalt_success = true;
                                        break;
                                    } else if (j.contains("status") && j["status"] == "redirect" && j.contains("url")) {
                                        cobalt_url = j["url"];
                                        cobalt_success = true;
                                        break;
                                    } else if (j.contains("picker") && j["picker"].is_array() && !j["picker"].empty()) {
                                        auto first_item = j["picker"][0];
                                        if (first_item.contains("url") && first_item["url"].is_string()) {
                                            cobalt_url = first_item["url"];
                                            cobalt_success = true;
                                            break;
                                        }
                                    }
                                }
                            } catch (...) {}
                        }
                    }
                    
                    bool download_ok = false;
                    if (cobalt_success && !cobalt_url.empty()) {
                        download_ok = download_file_system(cobalt_url, out_path);
                    }
                    
                    if (!download_ok) {
                        // Fallback to yt-dlp
                        std::string format_sel = "\"(bv*[filesize<18M]+ba*[filesize<2M])/(bv*+ba*)[filesize<20M]/b[filesize<20M]/best\"";
                        std::string sort_pref = "-S \"filesize~20M,res,ext\"";
                        std::string cmd = "yt-dlp " + sort_pref + " --cache-dir /tmp/yt-dlp-cache -f " + format_sel + " --no-playlist --merge-output-format mp4 -o " + shell_escape(out_path) + " " + shell_escape(final_url) + " >/dev/null 2>&1";
                        int yt_status = std::system(cmd.c_str());
                        
                        if (yt_status != 0 || !std::filesystem::exists(out_path) || std::filesystem::file_size(out_path) == 0) {
                            // Secondary fallback (without size constraint)
                            cmd = "yt-dlp --cache-dir /tmp/yt-dlp-cache -f best --no-playlist --merge-output-format mp4 -o " + shell_escape(out_path) + " " + shell_escape(final_url) + " >/dev/null 2>&1";
                            std::system(cmd.c_str());
                        }
                        
                        download_ok = (std::filesystem::exists(out_path) && std::filesystem::file_size(out_path) > 0);
                    }
                    
                    if (!download_ok) {
                        reply_or_edit("❌ failed to download media. URL may be invalid or platform is unsupported.");
                        std::filesystem::remove_all(temp_dir);
                        return;
                    }
                    
                    uint64_t file_size = std::filesystem::file_size(out_path);
                    if (file_size <= 20 * 1024 * 1024) {
                        // Direct upload
                        std::ifstream f(out_path, std::ios::binary);
                        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                        f.close();
                        
                        auto up_res = bot.rest().upload_file("/attachments", "video.mp4", data, "video/mp4");
                        if (up_res.success() && up_res.body.contains("id") && up_res.body["id"].is_string()) {
                            std::string file_id = up_res.body["id"].get<std::string>();
                            
                            stoatpp::models::MessagePayload payload;
                            payload.add_attachment(file_id);
                            
                            if (meta.success) {
                                std::string title_line = "🎥 **" + meta.title + "**";
                                if (!meta.uploader.empty() && meta.uploader != "Unknown") {
                                    title_line += " by *" + meta.uploader + "*";
                                }
                                payload.set_content(title_line);
                            }
                            
                            reply_or_edit_payload(payload);
                        } else {
                            reply_or_edit("❌ failed to upload media to server.");
                        }
                    } else {
                        // Upload to Catbox
                        std::string hosted_url = upload_to_catbox(out_path);
                        if (hosted_url.empty() || hosted_url.find("http") == std::string::npos) {
                            reply_or_edit("❌ file size too large and failover host upload failed.");
                        } else {
                            stoatpp::models::MessagePayload payload;
                            payload.set_content("🚀 **video is too large for direct upload!**\nview/download here: " + hosted_url);
                            
                            if (meta.success) {
                                stoatpp::models::Embed embed;
                                embed.set_title(meta.title);
                                if (!meta.description.empty()) {
                                    std::string desc = meta.description;
                                    if (desc.size() > 250) desc = desc.substr(0, 247) + "...";
                                    embed.set_description(desc);
                                }
                                embed.set_url(hosted_url);
                                if (!meta.thumbnail.empty()) {
                                    std::string thumb_id = bot.rest().upload_from_url(meta.thumbnail, "thumb.png", "image/png");
                                    if (!thumb_id.empty()) {
                                        embed.set_image(thumb_id);
                                    }
                                }
                                embed.set_colour("#ff007f");
                                payload.add_embed(embed);
                            }
                            reply_or_edit_payload(payload);
                        }
                    }
                    
                    std::filesystem::remove_all(temp_dir);
                };
                
                std::thread(run_dl).detach();
            },
            "Utility"
        });

        bot.register_command({
            "gif",
            {"to-gif"},
            "Convert a video or image attachment to a GIF",
            "",
            {"[url]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling gif conversion...\n";
                
                std::string target_url = "";
                if (!args.empty()) {
                    target_url = args[0];
                }
                
                auto run_gif = [&bot, msg, target_url]() {
                    std::string final_url = target_url;
                    
                    if (final_url.empty()) {
                        if (!msg.replies.empty()) {
                            auto res = bot.rest().get_message(msg.channel_id, msg.replies[0]);
                            if (res.success()) {
                                auto replied_msg = stoatpp::models::Message::from_json(res.body);
                                final_url = resolve_message_media_url(bot, replied_msg);
                            }
                        }
                    }
                    
                    if (final_url.empty()) {
                        final_url = resolve_message_media_url(bot, msg);
                    }
                    
                    if (final_url.empty() || final_url.find("http") == std::string::npos) {
                        bot.send_message(msg.channel_id, "❌ please provide a valid media URL or reply to a message containing one.");
                        return;
                    }
                    
                    bot.begin_typing(msg.channel_id);
                    
                    // Temp working directory
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<> dis(100000, 999999);
                    std::string id = std::to_string(dis(gen));
                    std::string temp_dir = "/tmp/bronx_gif_" + id;
                    std::filesystem::create_directories(temp_dir);
                    
                    std::string in_path = temp_dir + "/input.mp4";
                    std::string out_path = temp_dir + "/output.gif";
                    
                    if (!download_file_system(final_url, in_path)) {
                        bot.send_message(msg.channel_id, "❌ failed to download input file.");
                        std::filesystem::remove_all(temp_dir);
                        return;
                    }
                    
                    std::string escaped_in = shell_escape(in_path);
                    std::string escaped_out = shell_escape(out_path);
                    
                    // Run FFMPEG to convert video to GIF (10s limit, 320p width, 12fps, bayer dithering)
                    std::string cmd = "/usr/bin/ffmpeg -y -i " + escaped_in + " -t 10 -vf \"fps=12,scale=320:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse=dither=bayer:bayer_scale=1\" " + escaped_out + " >/dev/null 2>&1";
                    int status = std::system(cmd.c_str());
                    
                    if (status != 0 || !std::filesystem::exists(out_path) || std::filesystem::file_size(out_path) == 0) {
                        bot.send_message(msg.channel_id, "❌ failed to convert media to GIF. Check if it's a valid video/image.");
                        std::filesystem::remove_all(temp_dir);
                        return;
                    }
                    
                    // Upload converted GIF
                    std::ifstream f(out_path, std::ios::binary);
                    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    f.close();
                    
                    auto up_res = bot.rest().upload_file("/attachments", "converted.gif", data, "image/gif");
                    if (up_res.success() && up_res.body.contains("id") && up_res.body["id"].is_string()) {
                        std::string file_id = up_res.body["id"].get<std::string>();
                        
                        stoatpp::models::MessagePayload payload;
                        payload.add_attachment(file_id);
                        bot.send_message(msg.channel_id, payload);
                    } else {
                        bot.send_message(msg.channel_id, "❌ failed to upload converted GIF to server.");
                    }
                    
                    std::filesystem::remove_all(temp_dir);
                };
                
                std::thread(run_gif).detach();
            },
            "Utility"
        });

        bot.register_command({
            "ocr",
            {},
            "Extract text from an image (supports attached, replied, or linked images)",
            "",
            {"[url]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling ocr...\n";
                
                std::string target_url = "";
                if (!args.empty()) {
                    target_url = args[0];
                }
                
                auto run_ocr = [&bot, msg, target_url]() {
                    std::string final_url = target_url;
                    uint64_t size_check = 0;
                    
                    if (final_url.empty()) {
                        if (!msg.replies.empty()) {
                            auto res = bot.rest().get_message(msg.channel_id, msg.replies[0]);
                            if (res.success()) {
                                auto replied_msg = stoatpp::models::Message::from_json(res.body);
                                final_url = resolve_message_media_url(bot, replied_msg);
                                if (replied_msg.attachments.is_array() && !replied_msg.attachments.empty()) {
                                    size_check = replied_msg.attachments[0].value("size", int64_t(0));
                                }
                            }
                        }
                    }
                    
                    if (final_url.empty()) {
                        final_url = resolve_message_media_url(bot, msg);
                        if (msg.raw.contains("attachments") && msg.raw["attachments"].is_array() && !msg.raw["attachments"].empty()) {
                            size_check = msg.raw["attachments"][0].value("size", int64_t(0));
                        }
                    }
                    
                    if (final_url.empty() || final_url.find("http") == std::string::npos) {
                        bot.send_message(msg.channel_id, "❌ please provide a valid image URL or reply to an image.");
                        return;
                    }
                    
                    bot.begin_typing(msg.channel_id);
                    
                    process_ocr_request(bot, final_url, size_check, [&bot, msg](const std::string& error_msg, const std::vector<std::string>& pages) {
                        if (!error_msg.empty()) {
                            bot.send_message(msg.channel_id, error_msg);
                            return;
                        }
                        
                        for (size_t i = 0; i < pages.size(); ++i) {
                            stoatpp::models::Embed embed;
                            embed.set_title("OCR Result - Page " + std::to_string(i + 1) + "/" + std::to_string(pages.size()));
                            embed.set_description("```\n" + pages[i] + "\n```");
                            embed.set_colour("#ff007f");
                            
                            stoatpp::models::MessagePayload payload;
                            payload.add_embed(embed);
                            bot.send_message(msg.channel_id, payload);
                        }
                    });
                };
                
                std::thread(run_ocr).detach();
            },
            "Utility"
        });

    }
};

} // namespace bronx::cogs
