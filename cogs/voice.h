#pragma once
#include "helpers.h"
#include "stoatpp/bot_module.h"
#include "stoatpp/pulse_voice.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace bronx::cogs {

class VoiceModule : public stoatpp::bot_module {
public:
  void register_handlers(stoatpp::cluster &bot) override {
    // 1. Join voice channel command
    bot.register_command(
        {"join",
         {},
         "join a voice channel on the server.",
         "join [channel_id_or_name]",
         {"[channel_id_or_name]"},
         [this](stoatpp::cluster &bot, const stoatpp::events::Message &msg,
            const std::vector<std::string> &args) {
           std::cout << "[bronx command] handling join...\n";
           if (msg.server_id.empty()) {
             bot.send_message(msg.channel_id, "⚠️ This command can only be used in a server.");
             return;
           }

           std::string input_arg = "";
           if (!args.empty()) {
             input_arg = args[0];
             if (input_arg.rfind("<#", 0) == 0 && input_arg.back() == '>') {
               input_arg = input_arg.substr(2, input_arg.size() - 3);
             }
           }

           std::thread([this, &bot, msg, args, input_arg]() {
             std::string target_channel_id = "";
             std::string target_channel_name = "";

             if (input_arg.size() == 26) {
               // 1. Direct ID/mention lookup
               auto res = bot.rest().get_channel(input_arg);
               if (res.success()) {
                 auto c = stoatpp::models::Channel::from_json(res.body);
                 if (c.channel_type != "VoiceChannel") {
                   bot.send_message(msg.channel_id, "⚠️ Channel `" + input_arg + "` is not a voice channel (Type: " + c.channel_type + ").");
                   return;
                 }
                 target_channel_id = c.id;
                 target_channel_name = c.name ? *c.name : "Voice Channel";
               } else {
                 // Fallback: try to join directly in case it's a voice channel and REST get_channel failed
                 target_channel_id = input_arg;
                 target_channel_name = "Voice Channel (" + input_arg + ")";
               }
             } else {
               // 2. Search in local cache
               std::vector<stoatpp::models::Channel> voice_channels;
               for (const auto &c : bot.get_channels()) {
                 if (c.server && *c.server == msg.server_id && c.channel_type == "VoiceChannel") {
                   voice_channels.push_back(c);
                 }
               }

               if (voice_channels.empty()) {
                 std::stringstream dbg;
                 dbg << "⚠️ No voice channels found in local cache for this server.\n";
                 dbg << "**Channels cached for this server:**\n";
                 int count = 0;
                 int total_global = 0;
                 for (const auto &c : bot.get_channels()) {
                   total_global++;
                   if (c.server && *c.server == msg.server_id) {
                     count++;
                     dbg << "• ID: `" << c.id << "` | Name: `" << (c.name ? *c.name : "no-name") 
                         << "` | Type: `" << c.channel_type << "`\n";
                   }
                 }
                 if (count == 0) {
                   dbg << "(No channels at all are cached for this server!)\n";
                 }
                 dbg << "\nTotal server channels in cache: " << count << " | Total channels globally in cache: " << total_global << "\n";
                 bot.send_message(msg.channel_id, dbg.str());
                 return;
               }

               if (args.empty()) {
                 // Default to the first voice channel found
                 target_channel_id = voice_channels[0].id;
                 target_channel_name = voice_channels[0].name ? *voice_channels[0].name : "Voice Channel";
               } else {
                 std::string search = args[0];
                 std::transform(search.begin(), search.end(), search.begin(), ::tolower);

                 for (const auto &vc : voice_channels) {
                   std::string vc_name = vc.name ? *vc.name : "";
                   std::string vc_name_lower = vc_name;
                   std::transform(vc_name_lower.begin(), vc_name_lower.end(), vc_name_lower.begin(), ::tolower);

                   if (vc_name_lower.find(search) != std::string::npos) {
                     target_channel_id = vc.id;
                     target_channel_name = vc_name;
                     break;
                   }
                 }
               }
             }

             if (target_channel_id.empty()) {
               bot.send_message(msg.channel_id, "⚠️ Could not find a voice channel matching: `" + (args.empty() ? "" : args[0]) + "`");
               return;
             }

             bot.send_message(msg.channel_id, "🔊 Joining voice channel **" + target_channel_name + "**...");

             auto join_res = bot.rest().join_voice_call(target_channel_id);
             if (!join_res.success()) {
               bot.send_message(msg.channel_id, "⚠️ Failed to join voice channel via API: " + join_res.error_message());
               return;
             }

             // Initialize PulseAudio simple stream
             if (pulse_.is_initialized()) {
               pulse_.shutdown();
             }

             if (!pulse_.init("bronx-bot", "voice-stream")) {
               bot.send_message(msg.channel_id, "⚠️ Joined voice channel on server, but failed to initialize PulseAudio playback device.");
               return;
             }

             current_channel_id_ = target_channel_id;

             // Play a cool join chime
             pulse_.play_chime(true);

             bot.send_message(msg.channel_id, "✅ **Successfully joined voice channel!** Local PulseAudio stream initialized and join chime played.");
           }).detach();
         },
         "Voice"});

    // 2. Leave voice channel command
    bot.register_command(
        {"leave",
         {"part", "disconnect"},
         "leave the current voice channel.",
         "leave",
         {},
         [this](stoatpp::cluster &bot, const stoatpp::events::Message &msg,
            const std::vector<std::string> &args) {
           std::cout << "[bronx command] handling leave...\n";
           if (current_channel_id_.empty()) {
             bot.send_message(msg.channel_id, "⚠️ I am not currently connected to any voice channel.");
             return;
           }

           std::thread([this, &bot, msg]() {
             // Play a cool leave chime
             pulse_.play_chime(false);

             // Call REST API to leave the voice channel
             auto leave_res = bot.rest().end_voice_ring(current_channel_id_, bot.current_user().id);
             
             pulse_.shutdown();
             current_channel_id_ = "";

             bot.send_message(msg.channel_id, "👋 **Disconnected from voice channel.** PulseAudio stream closed.");
           }).detach();
         },
         "Voice"});

    // 3. Play custom tone command
    bot.register_command(
        {"playtone",
         {"tone"},
         "play a custom frequency sine wave locally via PulseAudio.",
         "playtone <frequency_hz> [duration_seconds]",
         {"<frequency_hz>", "[duration_seconds]"},
         [this](stoatpp::cluster &bot, const stoatpp::events::Message &msg,
            const std::vector<std::string> &args) {
           std::cout << "[bronx command] handling playtone...\n";
           if (args.empty()) {
             bot.send_message(msg.channel_id, "⚠️ Usage: `.playtone <frequency_hz> [duration_seconds]`");
             return;
           }

           double freq = 440.0;
           double duration = 1.0;

           try {
             freq = std::stod(args[0]);
             if (args.size() > 1) {
               duration = std::stod(args[1]);
             }
           } catch (...) {
             bot.send_message(msg.channel_id, "⚠️ Invalid frequency or duration values.");
             return;
           }

           if (freq < 20.0 || freq > 20000.0) {
             bot.send_message(msg.channel_id, "⚠️ Frequency must be between 20Hz and 20kHz.");
             return;
           }

           if (duration < 0.1 || duration > 10.0) {
             bot.send_message(msg.channel_id, "⚠️ Duration must be between 0.1 and 10 seconds.");
             return;
           }

           std::thread([this, &bot, msg, freq, duration]() {
             if (!pulse_.is_initialized()) {
               pulse_.init("bronx-bot", "tone-stream");
             }

             bot.send_message(msg.channel_id, "🔈 Playing **" + std::to_string(static_cast<int>(freq)) + " Hz** tone for **" + std::to_string(duration) + "s**...");
             
             if (pulse_.play_tone(freq, duration)) {
               bot.send_message(msg.channel_id, "✅ Finished playing tone.");
             } else {
               bot.send_message(msg.channel_id, "⚠️ Failed to play tone via PulseAudio.");
             }
           }).detach();
         },
         "Voice"});

    // 4. Record and measure mic level command
    bot.register_command(
        {"speak",
         {"listen", "miclevel"},
         "record mic audio locally via PulseAudio and display volume levels.",
         "speak [duration_seconds]",
         {"[duration_seconds]"},
         [this](stoatpp::cluster &bot, const stoatpp::events::Message &msg,
            const std::vector<std::string> &args) {
           std::cout << "[bronx command] handling speak...\n";
           double duration = 3.0;

           if (!args.empty()) {
             try {
               duration = std::stod(args[0]);
             } catch (...) {
               bot.send_message(msg.channel_id, "⚠️ Invalid duration value.");
               return;
             }
           }

           if (duration < 0.5 || duration > 10.0) {
             bot.send_message(msg.channel_id, "⚠️ Recording duration must be between 0.5 and 10 seconds.");
             return;
           }

           std::thread([this, &bot, msg, duration]() {
             if (!pulse_.is_initialized()) {
               pulse_.init("bronx-bot", "mic-stream");
             }

             bot.send_message(msg.channel_id, "🎤 **Recording microphone input for " + std::to_string(duration) + " seconds...** Speak now!");

             double average_rms = 0.0;
             auto samples = pulse_.record_mic(duration, average_rms);

             if (samples.empty()) {
               bot.send_message(msg.channel_id, "⚠️ Failed to record audio from PulseAudio recording source.");
               return;
             }

             // Render a cool volume meter
             int bars = static_cast<int>(average_rms * 25);
             if (bars > 25) bars = 25;
             std::string meter = "[" + std::string(bars, '#') + std::string(25 - bars, '-') + "]";

             std::stringstream ss;
             ss << "🎙️ **Audio Measurement Complete!**\n";
             ss << "Average RMS level: `" << average_rms << "`\n";
             ss << "Volume Meter: `" << meter << "`\n";
             
             if (average_rms < 0.005) {
               ss << "📢 *Status: Very quiet or no microphone input detected.*";
             } else if (average_rms < 0.05) {
               ss << "📢 *Status: Quiet / background noise detected.*";
             } else if (average_rms < 0.2) {
               ss << "📢 *Status: Normal speech level detected!*";
             } else {
               ss << "📢 *Status: Loud / audio clipping detected!*";
             }

             bot.send_message(msg.channel_id, ss.str());
           }).detach();
         },
         "Voice"});
  }

private:
  stoatpp::pulse_voice_client pulse_;
  std::string current_channel_id_;
};

} // namespace bronx::cogs
