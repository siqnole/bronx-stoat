#pragma once
#include "helpers.h"
#include "stoatpp/bot_module.h"
#include "stoatpp/pulse_voice.h"
#include "stoatpp/voice_connection.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace bronx::cogs {

class VoiceModule : public stoatpp::bot_module {
public:
  void register_handlers(stoatpp::cluster &bot) override {
    // Hook PulseAudio to WebRTC voice stream
    pulse_.set_audio_monitor([this](const int16_t* samples, size_t count) {
      if (voice_.is_connected()) {
        voice_.write_pcm(samples, count);
      }
    });

    // 0. Listen to voice state updates to play join/leave chimes and log active status
    bot.on_voice_state_update([this, &bot](const stoatpp::events::VoiceStateUpdate &ev) {
      std::cout << "[bronx voice_state_update] User: " << ev.user_id 
                << " | Channel: " << ev.channel_id 
                << " | Joined: " << (ev.joined ? "TRUE" : "FALSE") << "\n";
      
      // If we are currently connected to this voice channel, play a chime locally
      if (!current_channel_id_.empty() && ev.channel_id == current_channel_id_) {
        std::cout << "[bronx voice_state_update] Playing " << (ev.joined ? "join" : "leave") << " chime.\n";
        pulse_.play_chime(ev.joined);
      }
    });

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
             bot.send_message(msg.channel_id, "[Voice] This command can only be used in a server.");
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

             std::cout << "[bronx join] Resolving channel: " << (input_arg.empty() ? "(none)" : input_arg) << "\n";

             if (input_arg.size() == 26) {
               std::cout << "[bronx join] Looking up direct channel ID via REST...\n";
               auto res = bot.rest().get_channel(input_arg);
               if (res.success()) {
                 auto c = stoatpp::models::Channel::from_json(res.body);
                 target_channel_id = c.id;
                 target_channel_name = c.name ? *c.name : "Channel";
                 std::cout << "[bronx join] Found channel: " << target_channel_name << " (" << target_channel_id << ")\n";
               } else {
                 std::cout << "[bronx join] REST lookup failed, using direct ID: " << input_arg << "\n";
                 target_channel_id = input_arg;
                 target_channel_name = "Channel (" + input_arg + ")";
               }
             } else {
               std::cout << "[bronx join] Searching in local cache...\n";
               std::vector<stoatpp::models::Channel> voice_channels;
               for (const auto &c : bot.get_channels()) {
                 if (c.server && *c.server == msg.server_id && 
                     (c.channel_type == "VoiceChannel" || c.channel_type == "TextChannel")) {
                   voice_channels.push_back(c);
                 }
               }

               std::cout << "[bronx join] Cached channels found for this server: " << voice_channels.size() << "\n";

               if (voice_channels.empty()) {
                 std::cout << "[bronx join] Error: local cache has no voice/text channels for this server.\n";
                 bot.send_message(msg.channel_id, "[Voice] No voice channels found in local cache.");
                 return;
               }

               if (args.empty()) {
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
               std::cout << "[bronx join] Error: could not resolve target channel.\n";
               bot.send_message(msg.channel_id, "[Voice] Could not find a matching channel.");
               return;
             }

             std::cout << "[bronx join] Joining channel ID: " << target_channel_id 
                       << " | Name: " << target_channel_name << "...\n";
             bot.send_message(msg.channel_id, "[Voice] Joining voice channel: " + target_channel_name);

             std::cout << "[bronx join] Sending POST join_call REST request...\n";
             auto join_res = bot.rest().join_voice_call(target_channel_id);
             std::cout << "[bronx join] REST join_call response status: " << join_res.status_code << "\n";
             std::cout << "[bronx join] REST join_call response body: " << join_res.body.dump() << "\n";
             if (!join_res.success()) {
               std::cout << "[bronx join] ERROR: failed to join voice call. Body: " << join_res.body << "\n";
               bot.send_message(msg.channel_id, "[Voice] Failed to join call: " + join_res.error_message());
               return;
             }

             std::string ws_url = join_res.body.value("url", "");
             std::string token = join_res.body.value("token", "");
             if (ws_url.empty() || token.empty()) {
               std::cout << "[bronx join] ERROR: missing url or token in join response.\n";
               bot.send_message(msg.channel_id, "[Voice] Invalid voice token payload returned by server.");
               return;
             }

             std::cout << "[bronx join] Successfully joined voice call on server. Initializing WebRTC client...\n";

             voice_.disconnect();
             if (!voice_.connect(ws_url, token)) {
               std::cout << "[bronx join] ERROR: failed to establish WebRTC connection to " << ws_url << "\n";
               bot.send_message(msg.channel_id, "[Voice] Failed to establish WebRTC connection to voice server.");
               return;
             }

             if (pulse_.is_initialized()) {
               pulse_.shutdown();
             }

             if (!pulse_.init("bronx-bot", "voice-stream")) {
               std::cout << "[bronx join] ERROR: failed to init PulseAudio.\n";
               bot.send_message(msg.channel_id, "[Voice] Joined on server, but failed to initialize PulseAudio playback.");
               return;
             }

             current_channel_id_ = target_channel_id;

             // Signal voice presence to the Stoat gateway — this is what
             // makes the bot actually show up in the voice channel UI.
             std::cout << "[bronx join] Sending ChannelVoice gateway signal...\n";
             bot.ws().join_voice_channel(target_channel_id);

             std::cout << "[bronx join] Playing local join chime...\n";
             pulse_.play_chime(true);

             bot.send_message(msg.channel_id, "[Voice] Successfully joined voice channel and initialized WebRTC audio!");
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
             bot.send_message(msg.channel_id, "" + bronx::emj_warning + " I am not currently connected to any voice channel.");
             return;
           }

           std::thread([this, &bot, msg]() {
             // Play a cool leave chime
             pulse_.play_chime(false);

             // Signal departure to the Stoat gateway
             bot.ws().leave_voice_channel(current_channel_id_);

             voice_.disconnect();
             pulse_.shutdown();
             current_channel_id_ = "";

             bot.send_message(msg.channel_id, "👋 **Disconnected from voice channel.** WebRTC connection and PulseAudio stream closed.");
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
             bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Usage: `.playtone <frequency_hz> [duration_seconds]`");
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
             bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid frequency or duration values.");
             return;
           }

           if (freq < 20.0 || freq > 20000.0) {
             bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Frequency must be between 20Hz and 20kHz.");
             return;
           }

           if (duration < 0.1 || duration > 10.0) {
             bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Duration must be between 0.1 and 10 seconds.");
             return;
           }

           std::thread([this, &bot, msg, freq, duration]() {
             if (!pulse_.is_initialized()) {
               pulse_.init("bronx-bot", "tone-stream");
             }

             bot.send_message(msg.channel_id, "🔈 Playing **" + std::to_string(static_cast<int>(freq)) + " Hz** tone for **" + std::to_string(duration) + "s**...");
             
             if (pulse_.play_tone(freq, duration)) {
               bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Finished playing tone.");
             } else {
               bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to play tone via PulseAudio.");
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
               bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid duration value.");
               return;
             }
           }

           if (duration < 0.5 || duration > 10.0) {
             bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Recording duration must be between 0.5 and 10 seconds.");
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
               bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to record audio from PulseAudio recording source.");
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

    // 5. Play YouTube audio locally via PulseAudio
    bot.register_command(
        {"playyt",
         {"yt", "youtube"},
         "stream and play audio from a YouTube video locally.",
         "playyt <youtube_url>",
         {"<youtube_url>"},
         [this](stoatpp::cluster &bot, const stoatpp::events::Message &msg,
            const std::vector<std::string> &args) {
           std::cout << "[bronx command] handling playyt...\n";
           if (args.empty()) {
             bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Usage: `.playyt <youtube_url>`");
             return;
           }

           std::string url = args[0];
           
           // Simple validation of URL to prevent command injection
           if (url.find('\'') != std::string::npos || url.find('"') != std::string::npos || 
               url.find(';') != std::string::npos || url.find('`') != std::string::npos ||
               url.find('|') != std::string::npos || url.find('&') != std::string::npos) {
             bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid character in URL.");
             return;
           }

           std::thread([this, &bot, msg, url]() {
             if (!pulse_.is_initialized()) {
               pulse_.init("bronx-bot", "voice-stream");
             }

             std::string stream_url = "";
             try {
               httplib::Client cli("http://127.0.0.1:7111");
               cli.set_connection_timeout(std::chrono::seconds(5));
               cli.set_read_timeout(std::chrono::seconds(10));
                
               httplib::Headers headers = {
                 {"Accept", "application/json"},
                 {"Content-Type", "application/json"}
               };
                
               nlohmann::json payload;
               payload["url"] = url;
               payload["downloadMode"] = "audio";
               payload["audioFormat"] = "mp3";
                
               auto res = cli.Post("/", headers, payload.dump(), "application/json");
               if (res && res->status == 200) {
                 auto resp_json = nlohmann::json::parse(res->body);
                 if (resp_json.contains("url") && resp_json["url"].is_string()) {
                   stream_url = resp_json["url"].get<std::string>();
                 }
               } else {
                 std::cout << "[bronx playyt] Cobalt API request failed. Status: " 
                           << (res ? std::to_string(res->status) : "connection error") << "\n";
                 if (res) {
                   std::cout << "[bronx playyt] Response body: " << res->body << "\n";
                 }
               }
             } catch (const std::exception& e) {
               std::cout << "[bronx playyt] Exception during Cobalt API call: " << e.what() << "\n";
             }

             if (stream_url.empty()) {
               std::cout << "[bronx playyt] ERROR: Failed to extract stream URL.\n";
               bot.send_message(msg.channel_id, "[Music] Failed to extract direct audio stream from Cobalt.");
               return;
             }

             std::cout << "[bronx playyt] Got direct stream URL: " << stream_url << "\n";

             // Build shell pipeline to read stream URL via ffmpeg and convert to mono, 48kHz, 16-bit PCM
             std::string cmd = "ffmpeg -i \"" + stream_url + "\" -f s16le -ac 1 -ar 48000 pipe:1";
             std::cout << "[bronx playyt] Pipeline command: " << cmd << "\n";
              
             FILE* pipe = popen(cmd.c_str(), "r");
             if (!pipe) {
               std::cout << "[bronx playyt] ERROR: popen failed to start the pipeline.\n";
               bot.send_message(msg.channel_id, "[Music] Failed to launch audio streaming pipeline.");
               return;
             }

             char buffer[4096];
             size_t bytes_read = 0;
             size_t total_bytes = 0;
             size_t last_log_bytes = 0;
             bool success = false;

             while ((bytes_read = fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
               success = true;
               total_bytes += bytes_read;
               pulse_.write_audio(buffer, bytes_read);
                
               // Log every 512KB of audio processed to avoid flooding but keep updated
               if (total_bytes - last_log_bytes >= 512 * 1024) {
                 std::cout << "[bronx playyt] Streamed " << (total_bytes / 1024) << " KB to PulseAudio...\n";
                 last_log_bytes = total_bytes;
               }
             }

             int exit_status = pclose(pipe);
             std::cout << "[bronx playyt] Pipeline finished. Total bytes read: " << total_bytes 
                       << " | Exit code: " << exit_status << "\n";
              
             if (success) {
               pulse_.drain();
               std::cout << "[bronx playyt] Finished playing audio successfully.\n";
               bot.send_message(msg.channel_id, "[Music] Finished playing YouTube audio.");
             } else {
               std::cout << "[bronx playyt] Failed to read any audio bytes from pipeline.\n";
               bot.send_message(msg.channel_id, "[Music] Failed to play audio. Make sure the video is valid and reachable.");
             }
           }).detach();
         },
         "Voice"});
  }

private:
  stoatpp::pulse_voice_client pulse_;
  stoatpp::voice_connection voice_;
  std::string current_channel_id_;
};

} // namespace bronx::cogs
