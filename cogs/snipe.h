#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bronx::cogs {

struct SnipedMessage {
    std::string msg_id;
    std::string author_id;
    std::string author_name;
    std::string content;
    std::vector<std::string> attachments;
    uint64_t timestamp_ms = 0;
};

struct SnipedReaction {
    std::string message_id;
    std::string user_id;
    std::string emoji_id;
    uint64_t timestamp_ms = 0;
};

class SnipeModule : public stoatpp::bot_module {
private:
    std::unordered_map<std::string, stoatpp::events::Message> msg_cache_;
    std::mutex msg_cache_mutex_;

    std::unordered_map<std::string, std::deque<SnipedMessage>> snipe_cache_;
    std::unordered_map<std::string, std::deque<SnipedReaction>> reaction_snipe_cache_;
    std::mutex snipe_mutex_;

    static constexpr size_t MAX_SNIPED_PER_CHANNEL = 25;

public:
    void register_handlers(stoatpp::cluster& bot) override {

        // --- Cache every incoming message so we have content when it's deleted ---
        bot.on_message([this](const stoatpp::events::Message& msg) {
            if (msg.author.bot) return;
            std::lock_guard<std::mutex> lock(msg_cache_mutex_);
            msg_cache_[msg.id] = msg;
            if (msg_cache_.size() > 10000) {
                auto it = msg_cache_.begin();
                std::advance(it, 1000);
                msg_cache_.erase(msg_cache_.begin(), it);
            }
        });

        // --- On message delete: move cached message into snipe cache ---
        bot.on_message_delete([this, &bot](const stoatpp::events::MessageDelete& e) {
            stoatpp::events::Message cached;
            {
                std::lock_guard<std::mutex> lock(msg_cache_mutex_);
                auto it = msg_cache_.find(e.id);
                if (it == msg_cache_.end()) return;
                cached = it->second;
                msg_cache_.erase(it);
            }

            if (cached.author.bot) return;

            SnipedMessage sniped;
            sniped.msg_id = cached.id;
            sniped.author_id = cached.author.id;
            sniped.author_name = cached.author.username;
            sniped.content = cached.content;
            sniped.attachments = cached.attachments;
            sniped.timestamp_ms = stoatpp::utils::decode_ulid_timestamp(cached.id);

            {
                std::lock_guard<std::mutex> lock(snipe_mutex_);
                auto& dq = snipe_cache_[e.channel_id];
                dq.push_front(sniped);
                while (dq.size() > MAX_SNIPED_PER_CHANNEL) {
                    dq.pop_back();
                }
            }
        });

        // --- On reaction remove: move into reaction snipe cache ---
        bot.on_message_unreact([this, &bot](const stoatpp::events::MessageUnreact& e) {
            if (e.user_id == bot.current_user().id) return;
            if (stoatpp::event_dispatcher::should_ignore_user && stoatpp::event_dispatcher::should_ignore_user(e.user_id)) return;

            SnipedReaction sniped;
            sniped.message_id = e.id;
            sniped.user_id = e.user_id;
            sniped.emoji_id = e.emoji_id;
            sniped.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

            {
                std::lock_guard<std::mutex> lock(snipe_mutex_);
                auto& dq = reaction_snipe_cache_[e.channel_id];
                dq.push_front(sniped);
                while (dq.size() > MAX_SNIPED_PER_CHANNEL) {
                    dq.pop_back();
                }
            }
        });

        // --- .snipe [n] ---
        bot.register_command({
            "snipe",
            {},
            "show the last deleted message in this channel.",
            "snipe [n]",
            {"[n]"},
            [this, &bot](stoatpp::cluster&, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                int index = 1;
                if (!args.empty()) {
                    try { index = std::stoi(args[0]); } catch (...) {
                        bot.send_message(msg.channel_id, "⚠️ Invalid number.");
                        return;
                    }
                }
                if (index < 1) index = 1;

                std::lock_guard<std::mutex> lock(snipe_mutex_);
                auto it = snipe_cache_.find(msg.channel_id);
                if (it == snipe_cache_.end() || it->second.empty()) {
                    bot.send_message(msg.channel_id, "📭 Nothing to snipe.");
                    return;
                }
                auto& dq = it->second;
                if ((size_t)index > dq.size()) {
                    bot.send_message(msg.channel_id, "📭 Only " + std::to_string(dq.size()) + " sniped message(s) available.");
                    return;
                }
                const auto& s = dq[(size_t)(index - 1)];

                stoatpp::models::MessagePayload payload;
                stoatpp::models::Embed embed;
                embed.set_title("Deleted Message" + (index > 1 ? " #" + std::to_string(index) : ""));
                embed.set_colour("#ED4245");

                std::string desc = "**Author:** <@" + s.author_id + ">\n";
                if (!s.content.empty()) {
                    std::string trunc = s.content.size() > 1800 ? s.content.substr(0, 1800) + "..." : s.content;
                    desc += "**Content:** [" + trunc + "](https://app.revolt.chat/channel/" + msg.channel_id + "/" + s.msg_id + ")\n";
                } else {
                    desc += "**Message:** [Jump to message](https://app.revolt.chat/channel/" + msg.channel_id + "/" + s.msg_id + ")\n";
                }
                if (!s.attachments.empty()) {
                    desc += "**Attachments:** " + std::to_string(s.attachments.size()) + " file(s)\n";
                }
                if (s.timestamp_ms > 0) {
                    desc += "**Sent:** " + stoatpp::utils::format_timestamp(static_cast<int64_t>(s.timestamp_ms / 1000), "R");
                }
                embed.set_description(desc);
                payload.add_embed(embed);

                bot.send_message(msg.channel_id, payload);
            },
            "General"
        });

        // --- .rs [n] ---
        bot.register_command({
            "reactionsnipe",
            {"rs"},
            "show recently removed reactions in this channel.",
            "reactionsnipe [n]",
            {"[n]"},
            [this, &bot](stoatpp::cluster&, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                int index = 1;
                if (!args.empty()) {
                    try { index = std::stoi(args[0]); } catch (...) {
                        bot.send_message(msg.channel_id, "⚠️ Invalid number.");
                        return;
                    }
                }
                if (index < 1) index = 1;

                std::lock_guard<std::mutex> lock(snipe_mutex_);
                auto it = reaction_snipe_cache_.find(msg.channel_id);
                if (it == reaction_snipe_cache_.end() || it->second.empty()) {
                    bot.send_message(msg.channel_id, "📭 Nothing to snipe.");
                    return;
                }
                auto& dq = it->second;
                if ((size_t)index > dq.size()) {
                    bot.send_message(msg.channel_id, "📭 Only " + std::to_string(dq.size()) + " sniped reaction(s) available.");
                    return;
                }
                const auto& s = dq[(size_t)(index - 1)];

                auto is_custom_emoji_id = [](const std::string& str) {
                    if (str.size() != 26) return false;
                    for (char c : str) {
                        if (!std::isalnum(c)) return false;
                    }
                    return true;
                };

                std::string formatted_emoji = s.emoji_id;
                if (is_custom_emoji_id(s.emoji_id)) {
                    formatted_emoji = ":" + s.emoji_id + ":";
                }

                stoatpp::models::MessagePayload payload;
                stoatpp::models::Embed embed;
                embed.set_title("Reaction Sniped" + (index > 1 ? " #" + std::to_string(index) : ""));
                embed.set_colour("#E67E22");

                std::string desc = "**User:** <@" + s.user_id + ">\n"
                                   "**Reaction:** " + formatted_emoji + "\n"
                                   "**Message:** [Jump to message](https://app.revolt.chat/channel/" + msg.channel_id + "/" + s.message_id + ")\n";
                if (s.timestamp_ms > 0) {
                    desc += "**Removed:** " + stoatpp::utils::format_timestamp(static_cast<int64_t>(s.timestamp_ms / 1000), "R");
                }
                embed.set_description(desc);
                payload.add_embed(embed);

                bot.send_message(msg.channel_id, payload);
            },
            "General"
        });

        // --- .clearsnipe (requires Manage Messages) ---
        bot.register_command({
            "clearsnipe",
            {},
            "clear the snipe cache for this channel.",
            "",
            {},
            [this, &bot](stoatpp::cluster&, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::lock_guard<std::mutex> lock(snipe_mutex_);
                bool cleared = false;

                auto it = snipe_cache_.find(msg.channel_id);
                if (it != snipe_cache_.end() && !it->second.empty()) {
                    it->second.clear();
                    cleared = true;
                }

                auto it_re = reaction_snipe_cache_.find(msg.channel_id);
                if (it_re != reaction_snipe_cache_.end() && !it_re->second.empty()) {
                    it_re->second.clear();
                    cleared = true;
                }

                if (!cleared) {
                    bot.send_message(msg.channel_id, "📭 Nothing to clear.");
                    return;
                }
                bot.send_message(msg.channel_id, "✅ Snipe caches cleared for this channel.");
            },
            "Moderation",
            stoatpp::permissions::ManageMessages
        });
    }
};

} // namespace bronx::cogs
