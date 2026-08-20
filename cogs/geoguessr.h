#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "../database/database.h"
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <sstream>
#include <thread>
#include <cctype>

namespace bronx::cogs {

struct GeoLocation {
    std::string title;
    std::string city;
    std::string country;
    std::vector<std::string> country_aliases;
    std::vector<std::string> city_aliases;
    std::string continent;
    std::string hemisphere;
    std::string driving_side;
    std::string currency;
    std::string capital;
    std::string image_url;
    std::string fun_fact;
};

class GeoGuessrModule : public stoatpp::bot_module {
public:
    static GeoGuessrModule* instance;

    struct GeoSession {
        std::string channel_id;
        std::string host_id;
        GeoLocation location;
        int hints_revealed = 0;
        std::chrono::system_clock::time_point expires_at;
        bool active = false;
        std::string message_id;
    };

private:
    std::unordered_map<std::string, GeoSession> active_sessions_; // channel_id -> session
    std::mutex session_mutex_;

    std::vector<GeoLocation> location_pool_ = {
        {
            "Mount Fuji & Chureito Pagoda", "Fujiyoshida", "Japan",
            {"Japan", "Nippon", "Nihon", "JP"},
            {"Fujiyoshida", "Mount Fuji", "Mt Fuji", "Fuji"},
            "Asia", "Northern Hemisphere", "Left", "Japanese Yen (JPY)", "Tokyo",
            "https://images.unsplash.com/photo-1493976040374-85c8e12f0c0e?w=800&auto=format&fit=crop",
            "Mount Fuji is an active stratovolcano that last erupted in 1707–1708."
        },
        {
            "Eiffel Tower", "Paris", "France",
            {"France", "French Republic", "FR"},
            {"Paris", "Eiffel Tower", "Eiffel"},
            "Europe", "Northern Hemisphere", "Right", "Euro (EUR)", "Paris",
            "https://images.unsplash.com/photo-1511739001486-6bfe10ce785f?w=800&auto=format&fit=crop",
            "The Eiffel Tower was originally built as a temporary entrance arch for the 1889 World's Fair."
        },
        {
            "Statue of Liberty", "New York City", "United States",
            {"United States", "USA", "US", "America", "United States of America"},
            {"New York", "NYC", "New York City", "Statue of Liberty", "Manhattan"},
            "North America", "Northern Hemisphere", "Right", "US Dollar (USD)", "Washington D.C.",
            "https://images.unsplash.com/photo-1605130284535-11dd9eedc58a?w=800&auto=format&fit=crop",
            "The Statue of Liberty was a gift from the people of France to the United States in 1886."
        },
        {
            "Taj Mahal", "Agra", "India",
            {"India", "Bharat", "IN"},
            {"Agra", "Taj Mahal", "Uttar Pradesh"},
            "Asia", "Northern Hemisphere", "Left", "Indian Rupee (INR)", "New Delhi",
            "https://images.unsplash.com/photo-1564507592333-c60657eea523?w=800&auto=format&fit=crop",
            "The Taj Mahal was commissioned by Emperor Shah Jahan in 1631 in memory of his wife Mumtaz Mahal."
        },
        {
            "Colosseum", "Rome", "Italy",
            {"Italy", "Italia", "IT"},
            {"Rome", "Roma", "Colosseum"},
            "Europe", "Northern Hemisphere", "Right", "Euro (EUR)", "Rome",
            "https://images.unsplash.com/photo-1552832230-c0197dd311b5?w=800&auto=format&fit=crop",
            "The Colosseum is the largest ancient amphitheatre ever built and held up to 80,000 spectators."
        },
        {
            "Christ the Redeemer", "Rio de Janeiro", "Brazil",
            {"Brazil", "Brasil", "BR"},
            {"Rio de Janeiro", "Rio", "Christ the Redeemer", "Corcovado"},
            "South America", "Southern Hemisphere", "Right", "Brazilian Real (BRL)", "Brasília",
            "https://images.unsplash.com/photo-1483729558449-99ef09a8c325?w=800&auto=format&fit=crop",
            "The statue stands 30 meters tall atop Mount Corcovado overlooking Guanabara Bay."
        },
        {
            "Sydney Opera House", "Sydney", "Australia",
            {"Australia", "AU", "Oz"},
            {"Sydney", "Sydney Opera House", "New South Wales"},
            "Oceania", "Southern Hemisphere", "Left", "Australian Dollar (AUD)", "Canberra",
            "https://images.unsplash.com/photo-1624138784614-87fd1b6528f8?w=800&auto=format&fit=crop",
            "The Sydney Opera House was designed by Danish architect Jørn Utzon and opened in 1973."
        },
        {
            "Pyramids of Giza", "Cairo", "Egypt",
            {"Egypt", "EG"},
            {"Cairo", "Giza", "Pyramids", "Giza Plateau"},
            "Africa", "Northern Hemisphere", "Right", "Egyptian Pound (EGP)", "Cairo",
            "https://images.unsplash.com/photo-1503177119275-0aa32b3a9368?w=800&auto=format&fit=crop",
            "The Great Pyramid of Giza is the oldest of the Seven Wonders of the Ancient World."
        },
        {
            "Machu Picchu", "Cusco Region", "Peru",
            {"Peru", "PE"},
            {"Cusco", "Cuzco", "Machu Picchu", "Urubamba"},
            "South America", "Southern Hemisphere", "Right", "Peruvian Sol (PEN)", "Lima",
            "https://images.unsplash.com/photo-1526392060635-9d6019884377?w=800&auto=format&fit=crop",
            "Machu Picchu was built in the 15th century as an estate for the Inca emperor Pachacuti."
        },
        {
            "Big Ben & Palace of Westminster", "London", "United Kingdom",
            {"United Kingdom", "UK", "Britain", "Great Britain", "England", "GB"},
            {"London", "Big Ben", "Westminster"},
            "Europe", "Northern Hemisphere", "Left", "Pound Sterling (GBP)", "London",
            "https://images.unsplash.com/photo-1513635269975-59663e0ac1ad?w=800&auto=format&fit=crop",
            "'Big Ben' is actually the nickname for the Great Bell of the striking clock at the north end of the palace."
        },
        {
            "Santorini Caldera", "Santorini", "Greece",
            {"Greece", "Hellas", "GR"},
            {"Santorini", "Thira", "Oia", "Fira"},
            "Europe", "Northern Hemisphere", "Right", "Euro (EUR)", "Athens",
            "https://images.unsplash.com/photo-1570077188670-e3a8d69ac5ff?w=800&auto=format&fit=crop",
            "Santorini is what remains after an enormous volcanic eruption that destroyed early settlements."
        },
        {
            "Marina Bay Sands", "Singapore", "Singapore",
            {"Singapore", "SG"},
            {"Singapore", "Marina Bay"},
            "Asia", "Northern Hemisphere", "Left", "Singapore Dollar (SGD)", "Singapore",
            "https://images.unsplash.com/photo-1525625293386-3f8f99389edd?w=800&auto=format&fit=crop",
            "The hotel features the world's longest elevated infinity swimming pool located on the SkyPark."
        },
        {
            "Petra Treasury", "Ma'an", "Jordan",
            {"Jordan", "JO"},
            {"Petra", "Ma'an", "Wadi Musa"},
            "Asia", "Northern Hemisphere", "Right", "Jordanian Dinar (JOD)", "Amman",
            "https://images.unsplash.com/photo-1579606032822-83b38c232f32?w=800&auto=format&fit=crop",
            "Petra is famous for its rock-cut architecture, carved into the rose-red desert sandstone."
        },
        {
            "Brandenburg Gate", "Berlin", "Germany",
            {"Germany", "Deutschland", "DE"},
            {"Berlin", "Brandenburg Gate"},
            "Europe", "Northern Hemisphere", "Right", "Euro (EUR)", "Berlin",
            "https://images.unsplash.com/photo-1560969184-10fe8719e047?w=800&auto=format&fit=crop",
            "The Brandenburg Gate was built on the orders of Prussian King Frederick William II in 1791."
        },
        {
            "Sagrada Familia", "Barcelona", "Spain",
            {"Spain", "España", "ES"},
            {"Barcelona", "Sagrada Familia", "Catalonia"},
            "Europe", "Northern Hemisphere", "Right", "Euro (EUR)", "Madrid",
            "https://images.unsplash.com/photo-1583422409516-2895a77efded?w=800&auto=format&fit=crop",
            "Designed by Antoni Gaudí, construction began in 1882 and remains in progress."
        },
        {
            "Burj Khalifa", "Dubai", "United Arab Emirates",
            {"United Arab Emirates", "UAE", "Emirates"},
            {"Dubai", "Burj Khalifa"},
            "Asia", "Northern Hemisphere", "Right", "UAE Dirham (AED)", "Abu Dhabi",
            "https://images.unsplash.com/photo-1512453979798-5ea266f8880c?w=800&auto=format&fit=crop",
            "At 828 meters (2,717 feet), the Burj Khalifa is the tallest building in the world."
        },
        {
            "Niagara Falls", "Ontario / New York", "Canada",
            {"Canada", "CA"},
            {"Niagara Falls", "Ontario", "Niagara"},
            "North America", "Northern Hemisphere", "Right", "Canadian Dollar (CAD)", "Ottawa",
            "https://images.unsplash.com/photo-1534447677768-be436bb09401?w=800&auto=format&fit=crop",
            "Niagara Falls consists of three waterfalls that span the international border between Canada and the US."
        },
        {
            "Chichen Itza El Castillo", "Yucatan", "Mexico",
            {"Mexico", "MX"},
            {"Chichen Itza", "Yucatan", "Tinum"},
            "North America", "Northern Hemisphere", "Right", "Mexican Peso (MXN)", "Mexico City",
            "https://images.unsplash.com/photo-1518638150340-f706e86654de?w=800&auto=format&fit=crop",
            "During the equinoxes, the setting sun casts a shadow that resembles a feathered serpent crawling down the temple steps."
        },
        {
            "Table Mountain", "Cape Town", "South Africa",
            {"South Africa", "ZA"},
            {"Cape Town", "Table Mountain", "Western Cape"},
            "Africa", "Southern Hemisphere", "Right", "South African Rand (ZAR)", "Pretoria",
            "https://images.unsplash.com/photo-1580619305218-8423a7ef79b4?w=800&auto=format&fit=crop",
            "Table Mountain's flat top forms a prominent landmark overlooking the city of Cape Town."
        },
        {
            "Seoul Namsan Tower", "Seoul", "South Korea",
            {"South Korea", "Korea", "KR", "ROK"},
            {"Seoul", "Namsan", "Seoul Tower"},
            "Asia", "Northern Hemisphere", "Right", "South Korean Won (KRW)", "Seoul",
            "https://images.unsplash.com/photo-1538485399081-7191377e8241?w=800&auto=format&fit=crop",
            "N Seoul Tower marks the highest point in Seoul and is famous for its padlocks of love."
        },
        {
            "Saint Basil's Cathedral", "Moscow", "Russia",
            {"Russia", "RU", "Russian Federation"},
            {"Moscow", "Red Square", "Saint Basils"},
            "Europe", "Northern Hemisphere", "Right", "Russian Ruble (RUB)", "Moscow",
            "https://images.unsplash.com/photo-1513326718677-b964603b136b?w=800&auto=format&fit=crop",
            "Built between 1555 and 1561 on Red Square under Ivan the Terrible to commemorate the capture of Kazan."
        },
        {
            "Angkor Wat", "Siem Reap", "Cambodia",
            {"Cambodia", "KH"},
            {"Siem Reap", "Angkor", "Angkor Wat"},
            "Asia", "Northern Hemisphere", "Right", "Cambodian Riel (KHR)", "Phnom Penh",
            "https://images.unsplash.com/photo-1600585154340-be6161a56a0c?w=800&auto=format&fit=crop",
            "Angkor Wat is the largest religious monument in the world, originally constructed as a Hindu temple."
        },
        {
            "Auckland Sky Tower", "Auckland", "New Zealand",
            {"New Zealand", "NZ", "Aotearoa"},
            {"Auckland", "Sky Tower"},
            "Oceania", "Southern Hemisphere", "Left", "New Zealand Dollar (NZD)", "Wellington",
            "https://images.unsplash.com/photo-1507699622108-4be3abd695ad?w=800&auto=format&fit=crop",
            "The Sky Tower stands at 328 meters and is the tallest freestanding structure in the Southern Hemisphere."
        },
        {
            "Dubrovnik Old Town Walls", "Dubrovnik", "Croatia",
            {"Croatia", "HR"},
            {"Dubrovnik", "Ragusa"},
            "Europe", "Northern Hemisphere", "Right", "Euro (EUR)", "Zagreb",
            "https://images.unsplash.com/photo-1533105079780-92b9be482077?w=800&auto=format&fit=crop",
            "Dubrovnik is known for its distinctive Old Town, encircled by massive stone walls completed in the 16th century."
        },
        {
            "Amsterdam Canals & Gables", "Amsterdam", "Netherlands",
            {"Netherlands", "Holland", "NL"},
            {"Amsterdam", "Canals"},
            "Europe", "Northern Hemisphere", "Right", "Euro (EUR)", "Amsterdam",
            "https://images.unsplash.com/photo-1512470876302-972faa2aa9a4?w=800&auto=format&fit=crop",
            "Amsterdam has over 100 kilometers of canals, about 90 islands, and 1,500 bridges."
        },
        {
            "Blue Lagoon & Volcanic Landscape", "Reykjavik", "Iceland",
            {"Iceland", "IS"},
            {"Reykjavik", "Grindavik", "Blue Lagoon"},
            "Europe", "Northern Hemisphere", "Right", "Icelandic Króna (ISK)", "Reykjavik",
            "https://images.unsplash.com/photo-1504893524553-b855bce32c67?w=800&auto=format&fit=crop",
            "The geothermal spa is located in a lava field in Grindavík on the Reykjanes Peninsula."
        },
        {
            "Great Wall of China", "Beijing", "China",
            {"China", "CN", "PRC"},
            {"Beijing", "Badaling", "Mutianyu", "Great Wall"},
            "Asia", "Northern Hemisphere", "Right", "Chinese Yuan (CNY)", "Beijing",
            "https://images.unsplash.com/photo-1508804185872-d7badad00f7d?w=800&auto=format&fit=crop",
            "The total length of all sections of the Great Wall built over various dynasties measures over 21,000 km."
        },
        {
            "Stonehenge", "Wiltshire", "United Kingdom",
            {"United Kingdom", "UK", "England", "GB"},
            {"Salisbury", "Wiltshire", "Stonehenge"},
            "Europe", "Northern Hemisphere", "Left", "Pound Sterling (GBP)", "London",
            "https://images.unsplash.com/photo-1599833975787-5c143f373c30?w=800&auto=format&fit=crop",
            "Stonehenge was constructed in several phases from roughly 3000 BC to 2000 BC."
        },
        {
            "Moai Statues of Rapa Nui", "Easter Island", "Chile",
            {"Chile", "CL"},
            {"Easter Island", "Rapa Nui", "Hanga Roa"},
            "South America", "Southern Hemisphere", "Right", "Chilean Peso (CLP)", "Santiago",
            "https://images.unsplash.com/photo-1510097467424-192d713be8b2?w=800&auto=format&fit=crop",
            "The iconic monolithic human figures were carved by the Rapa Nui people between 1250 and 1500."
        },
        {
            "Venice Grand Canal", "Venice", "Italy",
            {"Italy", "Italia", "IT"},
            {"Venice", "Venezia"},
            "Europe", "Northern Hemisphere", "Right", "Euro (EUR)", "Rome",
            "https://images.unsplash.com/photo-1514896856000-91cb6de818e0?w=800&auto=format&fit=crop",
            "Venice is built on a group of 118 small islands separated by canals and linked by over 400 bridges."
        },
        {
            "Golden Gate Bridge", "San Francisco", "United States",
            {"United States", "USA", "US", "America"},
            {"San Francisco", "SF", "Golden Gate"},
            "North America", "Northern Hemisphere", "Right", "US Dollar (USD)", "Washington D.C.",
            "https://images.unsplash.com/photo-1501594907352-04cda38ebc29?w=800&auto=format&fit=crop",
            "The suspension bridge spans the Golden Gate strait connecting San Francisco Bay and the Pacific Ocean."
        },
        {
            "Matterhorn Peak", "Zermatt", "Switzerland",
            {"Switzerland", "Swiss", "CH"},
            {"Zermatt", "Matterhorn"},
            "Europe", "Northern Hemisphere", "Right", "Swiss Franc (CHF)", "Bern",
            "https://images.unsplash.com/photo-1530122037265-a5f1f91d3b99?w=800&auto=format&fit=crop",
            "The pyramidal peak in the Pennine Alps sits on the border between Switzerland and Italy."
        },
        {
            "Taipei 101", "Taipei", "Taiwan",
            {"Taiwan", "TW", "ROC"},
            {"Taipei", "Taipei 101"},
            "Asia", "Northern Hemisphere", "Right", "New Taiwan Dollar (TWD)", "Taipei",
            "https://images.unsplash.com/photo-1541872703-74c5e44368f9?w=800&auto=format&fit=crop",
            "Taipei 101 was officially classified as the world's tallest building from 2004 until 2010."
        },
        {
            "Uluru (Ayers Rock)", "Northern Territory", "Australia",
            {"Australia", "AU"},
            {"Uluru", "Ayers Rock", "Alice Springs"},
            "Oceania", "Southern Hemisphere", "Left", "Australian Dollar (AUD)", "Canberra",
            "https://images.unsplash.com/photo-1529108190281-9a4f620bc2d8?w=800&auto=format&fit=crop",
            "Uluru is sacred to the Anangu, the Aboriginal people of the area."
        },
        {
            "Mont-Saint-Michel", "Normandy", "France",
            {"France", "FR"},
            {"Mont Saint Michel", "Normandy"},
            "Europe", "Northern Hemisphere", "Right", "Euro (EUR)", "Paris",
            "https://images.unsplash.com/photo-1568605117036-5fe5e7bab0b7?w=800&auto=format&fit=crop",
            "The tidal island and abbey in Normandy has been held as a strategic fortification since ancient times."
        },
        {
            "Petronas Twin Towers", "Kuala Lumpur", "Malaysia",
            {"Malaysia", "MY"},
            {"Kuala Lumpur", "KL", "Petronas Towers"},
            "Asia", "Northern Hemisphere", "Right", "Malaysian Ringgit (MYR)", "Kuala Lumpur",
            "https://images.unsplash.com/photo-1596422846543-75c6fc197f07?w=800&auto=format&fit=crop",
            "The twin skyscrapers were the tallest buildings in the world from 1998 to 2004."
        }
    };

    std::string normalize_text(const std::string& input) {
        std::string out;
        for (char c : input) {
            if (std::isalnum((unsigned char)c) || c == ' ') {
                out.push_back(std::tolower((unsigned char)c));
            }
        }
        // collapse spaces
        std::string result;
        bool in_space = false;
        for (char c : out) {
            if (c == ' ') {
                if (!in_space && !result.empty()) {
                    result.push_back(' ');
                    in_space = true;
                }
            } else {
                result.push_back(c);
                in_space = false;
            }
        }
        if (!result.empty() && result.back() == ' ') result.pop_back();
        return result;
    }

    bool matches_any(const std::string& user_guess, const std::vector<std::string>& aliases) {
        std::string norm_guess = normalize_text(user_guess);
        if (norm_guess.empty()) return false;
        for (const auto& alias : aliases) {
            std::string norm_alias = normalize_text(alias);
            if (norm_guess == norm_alias) return true;
            // Substring match for longer strings
            if (norm_alias.size() >= 4 && norm_guess.size() >= 4) {
                if (norm_alias.find(norm_guess) != std::string::npos || norm_guess.find(norm_alias) != std::string::npos) {
                    return true;
                }
            }
        }
        return false;
    }

public:
    GeoGuessrModule() {
        instance = this;
    }

    ~GeoGuessrModule() {
        if (instance == this) {
            instance = nullptr;
        }
    }

    void register_handlers(stoatpp::cluster& bot) override {
        // Chat guess listener
        bot.on_message([this, &bot](const stoatpp::events::Message& msg) {
            if (msg.author.bot) return;

            GeoSession session;
            bool found = false;
            {
                std::lock_guard<std::mutex> lock(session_mutex_);
                auto it = active_sessions_.find(msg.channel_id);
                if (it != active_sessions_.end() && it->second.active) {
                    if (std::chrono::system_clock::now() > it->second.expires_at) {
                        // Game timed out
                        bot.send_message(msg.channel_id, "⏱️ **GeoGuessr Round Expired!** The correct location was **" + it->second.location.title + "** in **" + it->second.location.city + ", " + it->second.location.country + "** (" + it->second.location.continent + ").");
                        active_sessions_.erase(it);
                        return;
                    }
                    session = it->second;
                    found = true;
                }
            }

            if (!found) return;

            std::string guess_text = msg.content;
            std::transform(guess_text.begin(), guess_text.end(), guess_text.begin(), ::tolower);
            if (guess_text.rfind(".geo", 0) == 0 || guess_text.rfind("!", 0) == 0 || guess_text.rfind("/", 0) == 0) {
                return; // ignore bot commands
            }

            check_and_process_guess(bot, msg, session, msg.content);
        });

        // Command handler `.geo` / `.geoguessr`
        bot.register_command({
            "geo",
            {"geoguessr", "geoguess"},
            "interactive location guessing game (subcommands: hint, skip, stats, leaderboard)",
            "geo [subcommand/guess]",
            {"[subcommand]", "[args...]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling geo...\n";
                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;

                if (!args.empty()) {
                    std::string sub = args[0];
                    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

                    if (sub == "hint") {
                        handle_hint(bot, msg);
                        return;
                    }
                    if (sub == "skip" || sub == "giveup" || sub == "forfeit") {
                        handle_skip(bot, msg);
                        return;
                    }
                    if (sub == "stats" || sub == "profile" || sub == "score") {
                        handle_stats(bot, msg, args);
                        return;
                    }
                    if (sub == "lb" || sub == "leaderboard" || sub == "top") {
                        handle_leaderboard(bot, msg);
                        return;
                    }
                    if (sub == "guess" && args.size() >= 2) {
                        std::string user_guess = "";
                        for (size_t i = 1; i < args.size(); ++i) {
                            if (i > 1) user_guess += " ";
                            user_guess += args[i];
                        }
                        GeoSession session;
                        bool found = false;
                        {
                            std::lock_guard<std::mutex> lock(session_mutex_);
                            auto it = active_sessions_.find(msg.channel_id);
                            if (it != active_sessions_.end() && it->second.active) {
                                session = it->second;
                                found = true;
                            }
                        }
                        if (found) {
                            check_and_process_guess(bot, msg, session, user_guess);
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " No active GeoGuessr game in this channel! Start one with `" + pfx + "geo`.");
                        }
                        return;
                    }
                }

                // If args are provided but not matched as subcommand, treat full args as guess if game active, or start game
                {
                    GeoSession sess;
                    bool active_and_valid = false;
                    {
                        std::lock_guard<std::mutex> lock(session_mutex_);
                        auto it = active_sessions_.find(msg.channel_id);
                        if (it != active_sessions_.end() && it->second.active) {
                            if (std::chrono::system_clock::now() < it->second.expires_at) {
                                sess = it->second;
                                active_and_valid = true;
                            } else {
                                active_sessions_.erase(it);
                            }
                        }
                    }

                    if (active_and_valid) {
                        if (!args.empty()) {
                            std::string full_guess = "";
                            for (size_t i = 0; i < args.size(); ++i) {
                                if (i > 0) full_guess += " ";
                                full_guess += args[i];
                            }
                            check_and_process_guess(bot, msg, sess, full_guess);
                            return;
                        } else {
                            show_active_game_info(bot, msg, sess);
                            return;
                        }
                    }
                }

                // Start new GeoGuessr round
                start_new_round(bot, msg);
            },
            "Games"
        });
    }

private:
    std::unordered_map<std::string, std::string> autumn_cache_;
    std::mutex autumn_cache_mutex_;

    std::string get_or_upload_image(stoatpp::cluster& bot, const std::string& image_url) {
        if (image_url.empty()) return "";
        {
            std::lock_guard<std::mutex> lock(autumn_cache_mutex_);
            auto it = autumn_cache_.find(image_url);
            if (it != autumn_cache_.end() && !it->second.empty()) {
                return it->second;
            }
        }
        std::string media_id = bot.rest().upload_from_url(image_url);
        if (!media_id.empty()) {
            std::lock_guard<std::mutex> lock(autumn_cache_mutex_);
            autumn_cache_[image_url] = media_id;
        }
        return media_id;
    }

    void start_new_round(stoatpp::cluster& bot, const stoatpp::events::Message& msg) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, location_pool_.size() - 1);
        GeoLocation loc = location_pool_[dis(gen)];

        GeoSession session;
        session.channel_id = msg.channel_id;
        session.host_id = msg.author.id;
        session.location = loc;
        session.hints_revealed = 0;
        session.expires_at = std::chrono::system_clock::now() + std::chrono::seconds(60);
        session.active = true;

        std::thread([this, &bot, msg, session, loc]() mutable {
            std::string media_id = get_or_upload_image(bot, loc.image_url);

            stoatpp::models::MessagePayload payload;

            stoatpp::models::Embed embed;
            embed.set_colour(0x3498DB); // Sleek blue
            embed.set_title("🌍 GeoGuessr — Where in the world is this?");
            embed.set_description(
                "Look at the photo below and guess the location!\n"
                "• **City/Landmark**: 300 pts (Master Guess)\n"
                "• **Country**: 100 pts\n"
                "• **Continent**: 25 pts\n\n"
                "💬 *Type your guess directly in chat or use `.geo guess <name>`.*\n"
                "💡 *Use `.geo hint` for clues or `.geo skip` to reveal solution.*"
            );

            if (!media_id.empty()) {
                embed.set_image(media_id);
            } else {
                payload.content = loc.image_url;
            }

            payload.add_embed(embed);

            bot.send_message(msg.channel_id, payload, [this, msg, session](stoatpp::models::Message res, bool success) mutable {
                if (success) {
                    session.message_id = res.id;
                    std::lock_guard<std::mutex> lock(session_mutex_);
                    active_sessions_[msg.channel_id] = session;
                }
            });
        }).detach();
    }

    void show_active_game_info(stoatpp::cluster& bot, const stoatpp::events::Message& msg, const GeoSession& session) {
        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
        auto remaining_sec = std::chrono::duration_cast<std::chrono::seconds>(session.expires_at - std::chrono::system_clock::now()).count();

        std::thread([this, &bot, msg, session, pfx, remaining_sec]() {
            std::string media_id = get_or_upload_image(bot, session.location.image_url);

            stoatpp::models::MessagePayload payload;

            stoatpp::models::Embed embed;
            embed.set_colour(0x3498DB);
            embed.set_title("🌍 GeoGuessr Round Active!");
            embed.set_description(
                "A round is already running in this channel!\n\n"
                "⏱️ **Time Remaining**: " + std::to_string(std::max(0L, (long)remaining_sec)) + " seconds\n"
                "💡 **Hints Used**: " + std::to_string(session.hints_revealed) + "/3\n\n"
                "Type your guess directly in chat (e.g. `Japan`, `Paris`, `Europe`) or run `" + pfx + "geo hint`!"
            );

            if (!media_id.empty()) {
                embed.set_image(media_id);
            } else {
                payload.content = session.location.image_url;
            }

            payload.add_embed(embed);
            bot.send_message(msg.channel_id, payload);
        }).detach();
    }

    void check_and_process_guess(stoatpp::cluster& bot, const stoatpp::events::Message& msg, const GeoSession& session, const std::string& raw_guess) {
        std::string norm_guess = normalize_text(raw_guess);
        if (norm_guess.empty()) return;

        bool is_city = matches_any(raw_guess, session.location.city_aliases) || (normalize_text(session.location.city) == norm_guess);
        bool is_country = matches_any(raw_guess, session.location.country_aliases) || (normalize_text(session.location.country) == norm_guess);
        bool is_continent = (normalize_text(session.location.continent) == norm_guess);

        if (!is_city && !is_country && !is_continent) {
            return; // Not a matching guess, let normal chat flow continue
        }

        // Remove active game session
        {
            std::lock_guard<std::mutex> lock(session_mutex_);
            active_sessions_.erase(msg.channel_id);
        }

        int base_pts = 0;
        std::string match_type = "";

        if (is_city) {
            base_pts = 300;
            match_type = "🎯 Master Guess (Exact City / Landmark)!";
        } else if (is_country) {
            base_pts = 100;
            match_type = "🚩 Correct Country!";
        } else if (is_continent) {
            base_pts = 25;
            match_type = "🌐 Correct Continent!";
        }

        // Calculate hint deduction
        double hint_mult = 1.0 - (session.hints_revealed * 0.20);
        if (hint_mult < 0.40) hint_mult = 0.40;
        int final_pts = std::max(10, (int)(base_pts * hint_mult));

        // Update database stats
        if (bronx::g_db) {
            bronx::g_db->update_geo_stats(msg.author.id, final_pts, is_country || is_city, is_city, true);
        }

        auto user_stats = bronx::g_db ? bronx::g_db->get_geo_stats(msg.author.id) : std::nullopt;
        int streak = user_stats ? user_stats->current_streak : 1;

        std::thread([this, &bot, msg, session, raw_guess, match_type, final_pts, streak]() {
            std::string media_id = get_or_upload_image(bot, session.location.image_url);

            stoatpp::models::MessagePayload payload;

            stoatpp::models::Embed embed;
            embed.set_colour(0x2ECC71); // Green success
            embed.set_title("🎉 GeoGuessr Winner — " + match_type);
            embed.set_description(
                "<@" + msg.author.id + "> correctly guessed **" + raw_guess + "**!\n\n"
                "📍 **Location**: **" + session.location.title + "**\n"
                "🏙️ **City/Region**: " + session.location.city + "\n"
                "🏳️ **Country**: " + session.location.country + "\n"
                "🌐 **Continent**: " + session.location.continent + "\n\n"
                "🏆 **Points Earned**: +" + std::to_string(final_pts) + " pts\n"
                "🔥 **Current Streak**: " + std::to_string(streak) + " win" + (streak == 1 ? "" : "s") + "\n\n"
                "💡 *Fun Fact*: " + session.location.fun_fact
            );

            if (!media_id.empty()) {
                embed.set_image(media_id);
            } else {
                payload.content = session.location.image_url;
            }

            payload.add_embed(embed);
            bot.send_message(msg.channel_id, payload);
        }).detach();
    }

    void handle_hint(stoatpp::cluster& bot, const stoatpp::events::Message& msg) {
        std::lock_guard<std::mutex> lock(session_mutex_);
        auto it = active_sessions_.find(msg.channel_id);
        if (it == active_sessions_.end() || !it->second.active) {
            std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " No active GeoGuessr game in this channel! Start one with `" + pfx + "geo`.");
            return;
        }

        auto& session = it->second;
        if (session.hints_revealed >= 3) {
            bot.send_message(msg.channel_id, "💡 All 3 available hints have already been revealed for this round!");
            return;
        }

        session.hints_revealed++;
        std::string hint_text = "";
        if (session.hints_revealed == 1) {
            hint_text = "🌐 **Hint 1/3**: Located in **" + session.location.continent + "** (" + session.location.hemisphere + ").";
        } else if (session.hints_revealed == 2) {
            hint_text = "🚗 **Hint 2/3**: Driving side is **" + session.location.driving_side + "** | Currency: **" + session.location.currency + "**.";
        } else if (session.hints_revealed == 3) {
            std::string country = session.location.country;
            char first_char = country.empty() ? '?' : std::toupper((unsigned char)country[0]);
            hint_text = "🏛️ **Hint 3/3**: Country starts with **" + std::string(1, first_char) + "...** | Capital: **" + session.location.capital + "**.";
        }

        bot.send_message(msg.channel_id, hint_text);
    }

    void handle_skip(stoatpp::cluster& bot, const stoatpp::events::Message& msg) {
        GeoSession session;
        {
            std::lock_guard<std::mutex> lock(session_mutex_);
            auto it = active_sessions_.find(msg.channel_id);
            if (it == active_sessions_.end() || !it->second.active) {
                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " No active GeoGuessr game to skip!");
                return;
            }
            session = it->second;
            active_sessions_.erase(it);
        }

        // Reset streak on forfeit
        if (bronx::g_db) {
            bronx::g_db->update_geo_stats(msg.author.id, 0, false, false, false);
        }

        std::thread([this, &bot, msg, session]() {
            std::string media_id = get_or_upload_image(bot, session.location.image_url);

            stoatpp::models::MessagePayload payload;

            stoatpp::models::Embed embed;
            embed.set_colour(0xE74C3C); // Red
            embed.set_title("🏳️ GeoGuessr — Round Skipped");
            embed.set_description(
                "<@" + msg.author.id + "> forfeited the round!\n\n"
                "📍 **Location**: **" + session.location.title + "**\n"
                "🏙️ **City/Region**: " + session.location.city + "\n"
                "🏳️ **Country**: " + session.location.country + "\n"
                "🌐 **Continent**: " + session.location.continent + "\n\n"
                "💡 *Fun Fact*: " + session.location.fun_fact
            );

            if (!media_id.empty()) {
                embed.set_image(media_id);
            } else {
                payload.content = session.location.image_url;
            }

            payload.add_embed(embed);
            bot.send_message(msg.channel_id, payload);
        }).detach();
    }

    void handle_stats(stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
        std::string target_user = msg.author.id;
        if (args.size() >= 2) {
            std::string raw = args[1];
            if (raw.size() > 3 && raw.front() == '<' && raw[1] == '@' && raw.back() == '>') {
                target_user = raw.substr(2, raw.size() - 3);
            } else {
                target_user = raw;
            }
        }

        auto stats_opt = bronx::g_db ? bronx::g_db->get_geo_stats(target_user) : std::nullopt;
        if (!stats_opt) {
            bot.send_message(msg.channel_id, "📊 No GeoGuessr stats found for <@" + target_user + ">. Play a game with `.geo`!");
            return;
        }

        auto st = *stats_opt;
        double win_rate = st.games_played > 0 ? ((double)st.correct_countries / st.games_played) * 100.0 : 0.0;

        stoatpp::models::MessagePayload payload;
        payload.content = " ";

        stoatpp::models::Embed embed;
        embed.set_colour(0xF1C40F); // Gold
        embed.set_title("🌍 GeoGuessr Player Stats — <@" + target_user + ">");
        
        std::stringstream ss;
        ss << "🏆 **Total Points**: " << st.points << "\n"
           << "🎮 **Games Played**: " << st.games_played << "\n"
           << "🚩 **Correct Countries**: " << st.correct_countries << " (" << (int)win_rate << "% success)\n"
           << "🎯 **Exact City Guesses**: " << st.exact_cities << "\n"
           << "🔥 **Current Streak**: " << st.current_streak << " win" << (st.current_streak == 1 ? "" : "s") << "\n"
           << "⚡ **Best Streak**: " << st.max_streak << " win" << (st.max_streak == 1 ? "" : "s") << "\n";
        
        embed.set_description(ss.str());
        payload.add_embed(embed);

        bot.send_message(msg.channel_id, payload);
    }

    void handle_leaderboard(stoatpp::cluster& bot, const stoatpp::events::Message& msg) {
        auto top_list = bronx::g_db ? bronx::g_db->get_top_geo_leaderboard(10) : std::vector<bronx::GeoStats>();
        if (top_list.empty()) {
            bot.send_message(msg.channel_id, "🏆 No GeoGuessr scores recorded yet! Be the first by running `.geo`.");
            return;
        }

        stoatpp::models::MessagePayload payload;
        payload.content = " ";

        stoatpp::models::Embed embed;
        embed.set_colour(0xF1C40F);
        embed.set_title("🏆 GeoGuessr Global Leaderboard");

        std::string desc = "";
        int rank = 1;
        for (const auto& entry : top_list) {
            std::string medal = "";
            if (rank == 1) medal = "🥇 ";
            else if (rank == 2) medal = "🥈 ";
            else if (rank == 3) medal = "🥉 ";
            else medal = std::to_string(rank) + ". ";

            desc += medal + "<@" + entry.user_id + "> — **" + std::to_string(entry.points) + "** pts (" + std::to_string(entry.correct_countries) + " wins, 🔥 " + std::to_string(entry.max_streak) + " streak)\n";
            rank++;
        }

        embed.set_description(desc);
        payload.add_embed(embed);

        bot.send_message(msg.channel_id, payload);
    }
};

inline GeoGuessrModule* GeoGuessrModule::instance = nullptr;

} // namespace bronx::cogs
