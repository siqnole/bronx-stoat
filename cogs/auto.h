#pragma once
// cogs/auto.h — unified Auto module:
//   • .ar   — autoresponder (keyword → text/embed reply)
//   • .are  — autoreact    (keyword → emoji reactions)
//   • .recurse — recurring scheduled messages (moved from recurring.h)
//
// All three features share the AutoScopeFilter system.

#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "command_config.h"
#include "reaction_roles.h"   // clean_emoji / display_emoji reuse

#include <sstream>
#include <algorithm>
#include <fstream>
#include <thread>
#include <chrono>
#include <unistd.h>   // sysconf

namespace bronx::cogs {

// ============================================================================
// Shared utility helpers
// ============================================================================

// Parse any whitespace / comma / colon-delimited list of emoji IDs/unicode.
// ":id1::id2:" or "id1,id2" or "🔥 ✨" or any mixture.
static std::vector<std::string> parse_emoji_list(const std::string& text) {
    std::vector<std::string> out;
    std::string t = text;
    size_t i = 0;
    while (i < t.size()) {
        // skip separators
        while (i < t.size() && (std::isspace(t[i]) || t[i] == ',' || t[i] == ';')) i++;
        if (i >= t.size()) break;

        if (t[i] == ':') {
            // :ULID: custom emoji
            size_t close = t.find(':', i + 1);
            if (close != std::string::npos && close > i + 1) {
                std::string inner = t.substr(i + 1, close - i - 1);
                // strip if already a raw ULID (26-char base32)
                if (!inner.empty()) {
                    out.push_back(inner.size() == 26 ? inner : (":" + inner + ":"));
                }
                i = close + 1;
                continue;
            }
        }

        // Unicode emoji (multi-byte UTF-8) or raw ULID
        unsigned char c = static_cast<unsigned char>(t[i]);
        if (c >= 0x80) {
            // UTF-8 sequence
            size_t len = 1;
            if ((c & 0xF8) == 0xF0) len = 4;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xE0) == 0xC0) len = 2;
            std::string emoji = t.substr(i, std::min(len, t.size() - i));
            // consume optional variation selector (EF B8 8F / EF B8 8E)
            size_t ni = i + len;
            if (ni + 2 < t.size() &&
                static_cast<unsigned char>(t[ni]) == 0xEF &&
                static_cast<unsigned char>(t[ni+1]) == 0xB8 &&
                (static_cast<unsigned char>(t[ni+2]) == 0x8F || static_cast<unsigned char>(t[ni+2]) == 0x8E)) {
                emoji += t.substr(ni, 3);
                ni += 3;
            }
            out.push_back(emoji);
            i = ni;
            continue;
        }

        // ASCII: could be a raw ULID (26 alnum chars)
        size_t word_end = i;
        while (word_end < t.size() && !std::isspace(t[word_end]) && t[word_end] != ',' && t[word_end] != ';' && t[word_end] != ':')
            word_end++;
        std::string word = t.substr(i, word_end - i);
        if (!word.empty()) out.push_back(word);
        i = word_end;
    }
    return out;
}

// Encode emoji list back to comma-separated storage string
static std::string emojis_to_string(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ",";
        out += v[i];
    }
    return out;
}

// Display a stored emoji for user-facing output
static std::string display_auto_emoji(const std::string& e) {
    if (e.size() == 26 && e.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos)
        return ":" + e + ":";
    return e;
}

// ── Scope filter ──────────────────────────────────────────────────────────────
// Returns true if this message should trigger the auto (passes all filter rules).
// member_roles: the sender's role IDs (from msg.raw["member"]["roles"] if present).
static bool passes_filter(const Database::AutoScopeFilter& f,
                          const std::string& user_id,
                          const std::string& channel_id,
                          const std::vector<std::string>& member_roles) {
    auto has_any = [](const std::vector<std::string>& list, const std::string& val) {
        return std::find(list.begin(), list.end(), val) != list.end();
    };
    auto has_any_role = [&](const std::vector<std::string>& roles) {
        for (const auto& r : member_roles)
            if (has_any(roles, r)) return true;
        return false;
    };

    // Channel allow/ignore
    if (!f.allow_channels.empty() && !has_any(f.allow_channels, channel_id)) return false;
    if (!f.ignore_channels.empty() && has_any(f.ignore_channels, channel_id)) return false;
    // User allow/ignore
    if (!f.allow_users.empty() && !has_any(f.allow_users, user_id)) return false;
    if (!f.ignore_users.empty() && has_any(f.ignore_users, user_id)) return false;
    // Role allow/ignore
    if (!f.allow_roles.empty() && !has_any_role(f.allow_roles)) return false;
    if (!f.ignore_roles.empty() && has_any_role(f.ignore_roles)) return false;
    return true;
}

// Extract role IDs from msg.raw["member"]["roles"] if present
static std::vector<std::string> extract_member_roles(const stoatpp::events::Message& msg) {
    std::vector<std::string> roles;
    if (msg.raw.contains("member") && msg.raw["member"].is_object()) {
        const auto& m = msg.raw["member"];
        if (m.contains("roles") && m["roles"].is_array()) {
            for (const auto& r : m["roles"])
                if (r.is_string()) roles.push_back(r.get<std::string>());
        }
    }
    return roles;
}

// ── Keyword matching ──────────────────────────────────────────────────────────
static bool keyword_matches(const std::string& content_lower,
                            const std::string& keyword_lower,
                            const std::string& match_type) {
    if (match_type == "exact")  return content_lower == keyword_lower;
    if (match_type == "starts") return content_lower.rfind(keyword_lower, 0) == 0;
    if (match_type == "ends")   return content_lower.size() >= keyword_lower.size() &&
                                       content_lower.compare(content_lower.size() - keyword_lower.size(),
                                                             keyword_lower.size(), keyword_lower) == 0;
    // default: "contains"
    return content_lower.find(keyword_lower) != std::string::npos;
}

// ── Embed/text sender reused from recurring (inline) ─────────────────────────
static void send_auto_response(stoatpp::cluster& bot,
                               const Database::AutoresponderConfig& ar,
                               const stoatpp::events::Message& trigger) {
    stoatpp::models::MessagePayload payload;
    payload.replies.push_back({trigger.id, false});

    if (ar.message_type == "embed") {
        nlohmann::json emb_json = nlohmann::json::object();
        if (!ar.embed_json.empty()) {
            try { emb_json = nlohmann::json::parse(ar.embed_json); } catch (...) {}
        }
        if (!ar.content.empty()) payload.content = ar.content;
        nlohmann::json embed = nlohmann::json::object();
        for (const char* key : {"title", "description", "color", "colour", "image"}) {
            if (emb_json.contains(key) && emb_json[key].is_string()) {
                std::string val = emb_json[key].get<std::string>();
                // unescape \n
                std::string unescaped;
                for (size_t i = 0; i < val.size(); ++i) {
                    if (val[i] == '\\' && i+1 < val.size() && val[i+1] == 'n') { unescaped += '\n'; i++; }
                    else unescaped += val[i];
                }
                std::string k = key;
                if (k == "color") k = "colour";
                if (k == "image") {
                    std::string desc = embed.contains("description") ? embed["description"].get<std::string>() : "";
                    if (!desc.empty()) desc += "\n";
                    desc += "![](" + unescaped + ")";
                    embed["description"] = desc;
                } else {
                    embed[k] = unescaped;
                }
            }
        }
        if (!embed.empty()) payload.embeds.push_back(embed);
    } else {
        payload.content = ar.content;
    }

    bot.send_message(trigger.channel_id, payload);
}

// ── parse_pairs helper (copied from recurring, needed for edit wizards) ───────
static std::unordered_map<std::string, std::string> parse_kv_pairs(const std::string& text) {
    std::unordered_map<std::string, std::string> pairs;
    std::vector<std::string> keys = {
        "title:", "description:", "desc:", "colour:", "color:",
        "image:", "content:", "keyword:", "match:", "type:", "name:", "emojis:", "owneronly:",
        "interval:", "channel:"
    };
    struct KP { std::string key; size_t pos; };
    std::vector<KP> occ;
    for (const auto& key : keys) {
        size_t pos = 0;
        while ((pos = text.find(key, pos)) != std::string::npos) {
            if (pos == 0 || std::isspace((unsigned char)text[pos-1]))
                occ.push_back({key, pos});
            pos += key.length();
        }
    }
    std::sort(occ.begin(), occ.end(), [](const KP& a, const KP& b){ return a.pos < b.pos; });
    for (size_t i = 0; i < occ.size(); ++i) {
        size_t sv = occ[i].pos + occ[i].key.size();
        size_t ev = (i+1 < occ.size()) ? occ[i+1].pos : text.size();
        std::string val = text.substr(sv, ev - sv);
        val.erase(0, val.find_first_not_of(" \t\r\n"));
        if (!val.empty()) val.erase(val.find_last_not_of(" \t\r\n,") + 1);
        std::string k = occ[i].key; k.pop_back();
        if (k == "desc") k = "description";
        if (k == "colour") k = "color";
        if (k == "match") k = "match_type";
        pairs[k] = val;
    }
    return pairs;
}

// ── Resolve auto by name or numeric id ────────────────────────────────────────
template<typename T, typename GetById, typename GetByName>
static std::optional<T> resolve_auto(const std::string& server_id, const std::string& query,
                                     GetById by_id, GetByName by_name) {
    bool all_digits = !query.empty() && std::all_of(query.begin(), query.end(),
        [](unsigned char c){ return std::isdigit(c); });
    if (all_digits) {
        try {
            int id = std::stoi(query);
            auto r = by_id(id, server_id);
            if (r) return r;
        } catch (...) {}
    }
    return by_name(server_id, query);
}

// ── Filter mutation helper ────────────────────────────────────────────────────
// action: "allow" or "ignore"
// target: "role" | "user" | "channel"
// value: the ID or name to add/remove
static bool mutate_filter(Database::AutoScopeFilter& f,
                           const std::string& action,
                           const std::string& target,
                           const std::string& value,
                           bool remove = false) {
    std::vector<std::string>* list = nullptr;
    if      (action == "allow"  && target == "role")    list = &f.allow_roles;
    else if (action == "allow"  && target == "user")    list = &f.allow_users;
    else if (action == "allow"  && target == "channel") list = &f.allow_channels;
    else if (action == "ignore" && target == "role")    list = &f.ignore_roles;
    else if (action == "ignore" && target == "user")    list = &f.ignore_users;
    else if (action == "ignore" && target == "channel") list = &f.ignore_channels;
    else return false;

    if (remove) {
        list->erase(std::remove(list->begin(), list->end(), value), list->end());
    } else {
        if (std::find(list->begin(), list->end(), value) == list->end())
            list->push_back(value);
    }
    return true;
}

// ── Format filter for display ─────────────────────────────────────────────────
static std::string format_filter(const Database::AutoScopeFilter& f) {
    auto fmt_list = [](const std::string& label, const std::vector<std::string>& v) -> std::string {
        if (v.empty()) return "";
        std::string out = "• " + label + ": ";
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) out += ", ";
            out += "`" + v[i] + "`";
        }
        return out + "\n";
    };
    std::string out;
    out += fmt_list("Allow roles",    f.allow_roles);
    out += fmt_list("Allow users",    f.allow_users);
    out += fmt_list("Allow channels", f.allow_channels);
    out += fmt_list("Ignore roles",   f.ignore_roles);
    out += fmt_list("Ignore users",   f.ignore_users);
    out += fmt_list("Ignore channels",f.ignore_channels);
    return out.empty() ? "*No restrictions — fires for everyone*" : out;
}

// ── Format interval for display ───────────────────────────────────────────────
static std::string fmt_interval(uint64_t s) {
    uint64_t d=s/86400, h=(s%86400)/3600, m=(s%3600)/60, sec=s%60;
    std::string o;
    if (d) o+=std::to_string(d)+"d ";
    if (h) o+=std::to_string(h)+"h ";
    if (m) o+=std::to_string(m)+"m ";
    if (sec&&!d&&!h) o+=std::to_string(sec)+"s";
    while (!o.empty()&&o.back()==' ') o.pop_back();
    return o.empty()?std::to_string(s)+"s":o;
}

// ── Owner check with hardcoded fallback ──────────────────────────────────────
static constexpr const char* OWNER_ID_FALLBACK = "01FZB2QAPRVT8PVMF11480GRCD";
static bool is_bot_owner(stoatpp::cluster& bot, const std::string& uid) {
    return bot.is_owner(uid) || uid == OWNER_ID_FALLBACK;
}

// ── Token substitution (from recurring.h) ────────────────────────────────────
static std::string get_ram_usage() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream ss(line.substr(6)); long kb=0; ss>>kb;
            return kb<1024?std::to_string(kb)+" KB":std::to_string(kb/1024)+" MB";
        }
    }
    return "N/A";
}
static std::string get_thread_count() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("Threads:", 0) == 0) {
            std::istringstream ss(line.substr(8)); long n=0; ss>>n; return std::to_string(n);
        }
    }
    return "N/A";
}
static std::string get_cpu_usage() {
    std::ifstream sf("/proc/self/stat");
    if (!sf.is_open()) return "N/A";
    std::string d; long ut=0,st=0;
    for (int i=0;i<13;++i) sf>>d;
    sf>>ut>>st; sf.close();
    std::ifstream uf("/proc/uptime"); double up=0; if(uf.is_open()) uf>>up;
    long clk=sysconf(_SC_CLK_TCK); if(clk<=0) return "N/A";
    double pct=(up>0)?(double(ut+st)/clk/up*100.0):0.0;
    char buf[16]; snprintf(buf,sizeof(buf),"%.1f",pct); return buf;
}
static std::string unescape_nl(std::string s) {
    std::string o; o.reserve(s.size());
    for (size_t i=0;i<s.size();++i) {
        if (s[i]=='\\'&&i+1<s.size()&&s[i+1]=='n'){o+='\n';++i;} else o+=s[i];
    }
    return o;
}
static std::string apply_tokens(std::string text, stoatpp::cluster& bot,
                                const std::string& server_id, bool owner_tokens) {
    auto ra=[](std::string& s,const std::string& f,const std::string& t){
        size_t p=0; while((p=s.find(f,p))!=std::string::npos){s.replace(p,f.size(),t);p+=t.size();}
    };
    std::string sname="Unknown";
    int mcnt=0;
    if (owner_tokens) {
        for (const auto& s : bot.get_servers()) {
            if (s.raw.contains("member_count") && s.raw["member_count"].is_number()) {
                mcnt += s.raw["member_count"].get<int>();
            } else {
                mcnt += s.channels.size();
            }
        }
    } else {
        auto srv = bot.get_server(server_id);
        if (srv) {
            sname = srv->name;
            if (srv->raw.contains("member_count") && srv->raw["member_count"].is_number()) {
                mcnt = srv->raw["member_count"].get<int>();
            } else {
                mcnt = (int)srv->channels.size();
            }
        }
    }
    
    // ensure sname is set
    auto srv = bot.get_server(server_id);
    if (srv) sname = srv->name;
    if(owner_tokens){
        ra(text,"{ram}",get_ram_usage());
        ra(text,"{cpu}",get_cpu_usage()+"%");
        ra(text,"{threads}",get_thread_count());
        ra(text,"{commands}",std::to_string(bot.get_commands_executed()));
    }
    std::string scnt=std::to_string(bot.get_servers().size());
    uint64_t up=bot.uptime();
    uint64_t upd=up/86400,uph=(up%86400)/3600,upm=(up%3600)/60,ups=up%60;
    std::string upstr;
    if(upd) upstr+=std::to_string(upd)+"d ";
    if(uph) upstr+=std::to_string(uph)+"h ";
    if(upm) upstr+=std::to_string(upm)+"m ";
    upstr+=std::to_string(ups)+"s";
    
    std::string ping=std::to_string(bot.ping_latency())+"ms";
    ra(text,"{server}",sname);
    ra(text,"{membercount}",std::to_string(mcnt));
    ra(text,"{servercount}",scnt);
    ra(text,"{cachecount}",scnt);
    ra(text,"{uptime.days}",std::to_string(upd));
    ra(text,"{uptime.hours}",std::to_string(uph));
    ra(text,"{uptime.minutes}",std::to_string(upm));
    ra(text,"{uptime.seconds}",std::to_string(ups));
    ra(text,"{uptime}",upstr);
    ra(text,"{ping}",ping);
    return text;
}
static void send_recurring_msg(stoatpp::cluster& bot, const Database::RecurringMessage& rm,
                               const std::string& ch, std::function<void(bool)> cb=nullptr) {
    stoatpp::models::MessagePayload payload;
    if (rm.message_type=="embed") {
        nlohmann::json ej=nlohmann::json::object();
        if(!rm.embed_json.empty()){try{ej=nlohmann::json::parse(rm.embed_json);}catch(...){}};
        if(!rm.content.empty()) payload.content=apply_tokens(rm.content,bot,rm.server_id,rm.owner_tokens);
        nlohmann::json embed=nlohmann::json::object();
        if(ej.contains("title")&&ej["title"].is_string())
            embed["title"]=unescape_nl(apply_tokens(ej["title"].get<std::string>(),bot,rm.server_id,rm.owner_tokens));
        if(ej.contains("description")&&ej["description"].is_string())
            embed["description"]=unescape_nl(apply_tokens(ej["description"].get<std::string>(),bot,rm.server_id,rm.owner_tokens));
        if(ej.contains("color")&&ej["color"].is_string()){
            std::string col=ej["color"].get<std::string>();
            if(col=="random"){char h[8];snprintf(h,sizeof(h),"#%06x",rand()%0xFFFFFF);col=h;}
            embed["colour"]=col;
        }
        if(ej.contains("image")&&ej["image"].is_string()){
            std::string img=ej["image"].get<std::string>();
            std::string desc=embed.contains("description")?embed["description"].get<std::string>():"";
            if(!desc.empty())desc+="\n";
            desc+="![]("+img+")";
            embed["description"]=desc;
        }
        if(!embed.empty()) payload.embeds.push_back(embed);
    } else {
        payload.content=apply_tokens(rm.content,bot,rm.server_id,rm.owner_tokens);
    }
    bot.send_message(ch,payload,[cb](stoatpp::models::Message,bool ok){if(cb)cb(ok);});
}

// ============================================================================
// AutoModule
// ============================================================================

class AutoModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {

        // ────────────────────────────────────────────────────────────────────
        // .ar — Autoresponder
        // ────────────────────────────────────────────────────────────────────
        bot.register_command({
            "ar",
            {"autorespond", "autoresponder"},
            "manage keyword-triggered auto responses (text or embed).",
            "", {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) { bot.send_message(msg.channel_id,"⚠️ Server only."); return; }
                if (!g_db) { bot.send_message(msg.channel_id,"⚠️ Database unavailable."); return; }
                std::string pfx = msg.prefix.empty()?bot.config().command_prefix:msg.prefix;

                bot.fetch_member(msg.server_id, msg.author.id,
                    [&bot,msg,args,pfx](stoatpp::models::Member member, bool ok){
                        if (!ok){ bot.send_message(msg.channel_id,"⚠️ Could not verify permissions."); return; }
                        if (!is_user_trusted_or_owner(bot,msg.server_id,msg.author.id,member)){
                            bot.send_message(msg.channel_id,"⚠️ You need Manage Server permission."); return;
                        }

                        if (args.empty()){
                            bot.send_message(msg.channel_id,
                                "**Autoresponder subcommands:**\n"
                                "• `" + pfx + "ar add <name> <keyword>` — wizard to create\n"
                                "• `" + pfx + "ar remove <name|id>`\n"
                                "• `" + pfx + "ar list`\n"
                                "• `" + pfx + "ar edit <name|id> key:val [...]`\n"
                                "  Keys: `keyword`, `match` (contains/exact/starts/ends), `type` (text/embed), `content`, `title`, `description`, `color`, `image`, `name`\n"
                                "• `" + pfx + "ar filter <name|id> allow|ignore role|user|channel <value>`\n"
                                "• `" + pfx + "ar filter <name|id> remove allow|ignore role|user|channel <value>`\n"
                                "• `" + pfx + "ar filter <name|id> clear`\n"
                                "• `" + pfx + "ar info <name|id>`");
                            return;
                        }

                        std::string sub=args[0];
                        std::transform(sub.begin(),sub.end(),sub.begin(),::tolower);

                        // ── add ──────────────────────────────────────────────
                        if (sub=="add") {
                            if (args.size()<3){ bot.send_message(msg.channel_id,"usage: `"+pfx+"ar add <name> <keyword>`"); return; }
                            std::string ar_name=args[1];
                            std::string keyword;
                            for (size_t i=2;i<args.size();++i){ if(i>2)keyword+=" "; keyword+=args[i]; }

                            if (g_db->get_autoresponders_for_server(msg.server_id).size()>=25){
                                bot.send_message(msg.channel_id,"⚠️ Maximum 25 autoresponders per server."); return;
                            }

                            bot.send_message(msg.channel_id,
                                "💬 **Autoresponder Setup — \""+ar_name+"\"**\n"
                                "Keyword: `"+keyword+"`\n"
                                "**Step 1/3**: Match type? Reply: `contains`, `exact`, `starts`, `ends`");
                            bot.await_message(msg.channel_id,msg.author.id,std::chrono::seconds(30),
                                [&bot,msg,ar_name,keyword,pfx](std::optional<stoatpp::events::Message> r1){
                                    if(!r1){bot.send_message(msg.channel_id,"⏱️ Timed out.");return;}
                                    std::string mt=r1->content; std::transform(mt.begin(),mt.end(),mt.begin(),::tolower);
                                    if(mt=="cancel"){bot.send_message(msg.channel_id,"🚫 Cancelled.");return;}
                                    if(mt!="contains"&&mt!="exact"&&mt!="starts"&&mt!="ends") mt="contains";

                                    bot.send_message(msg.channel_id,
                                        "**Step 2/3**: Message type? Reply: `text` or `embed`");
                                    bot.await_message(msg.channel_id,msg.author.id,std::chrono::seconds(30),
                                        [&bot,msg,ar_name,keyword,mt,pfx](std::optional<stoatpp::events::Message> r2){
                                            if(!r2){bot.send_message(msg.channel_id,"⏱️ Timed out.");return;}
                                            std::string mtype=r2->content; std::transform(mtype.begin(),mtype.end(),mtype.begin(),::tolower);
                                            if(mtype=="cancel"){bot.send_message(msg.channel_id,"🚫 Cancelled.");return;}
                                            if(mtype!="text"&&mtype!="embed") mtype="text";

                                            std::string prompt = "**Step 3/3**: ";
                                            if (mtype=="text") prompt+="Type the response content.";
                                            else prompt+="Provide embed fields: `title:...`, `description:...`, `color:#hex`, `image:url`, `content:...` (optional text outside embed).";
                                            bot.send_message(msg.channel_id,prompt);
                                            bot.await_message(msg.channel_id,msg.author.id,std::chrono::seconds(120),
                                                [&bot,msg,ar_name,keyword,mt,mtype](std::optional<stoatpp::events::Message> r3){
                                                    if(!r3){bot.send_message(msg.channel_id,"⏱️ Timed out.");return;}
                                                    if(r3->content=="cancel"){bot.send_message(msg.channel_id,"🚫 Cancelled.");return;}

                                                    Database::AutoresponderConfig c;
                                                    c.server_id=msg.server_id;
                                                    c.name=ar_name;
                                                    c.keyword=keyword;
                                                    c.match_type=mt;
                                                    c.message_type=mtype;
                                                    if (mtype=="embed"){
                                                        auto pairs=parse_kv_pairs(r3->content);
                                                        nlohmann::json emb=nlohmann::json::object();
                                                        for(const auto&[k,v]:pairs){
                                                            if(k=="content") c.content=v;
                                                            else emb[k]=v;
                                                        }
                                                        c.embed_json=emb.dump();
                                                    } else {
                                                        c.content=r3->content;
                                                    }
                                                    if(g_db->add_autoresponder(c)){
                                                        bot.send_message(msg.channel_id,
                                                            "✅ Autoresponder **\""+ar_name+"\"** created! (ID: `"+std::to_string(c.id)+"`)");
                                                    } else {
                                                        bot.send_message(msg.channel_id,"⚠️ Failed to save.");
                                                    }
                                                });
                                        });
                                });
                            return;
                        }

                        // ── remove ───────────────────────────────────────────
                        if (sub=="remove"||sub=="delete"||sub=="del") {
                            if (args.size()<2){bot.send_message(msg.channel_id,"usage: `"+pfx+"ar remove <name|id>`");return;}
                            std::string q; for(size_t i=1;i<args.size();++i){if(i>1)q+=" ";q+=args[i];}
                            auto opt=resolve_auto<Database::AutoresponderConfig>(msg.server_id,q,
                                [](int id,const std::string& sid){return g_db->get_autoresponder_by_id(id,sid);},
                                [](const std::string& sid,const std::string& n){return g_db->get_autoresponder_by_name(sid,n);});
                            if(!opt){bot.send_message(msg.channel_id,"⚠️ Not found: `"+q+"`");return;}
                            if(g_db->remove_autoresponder(opt->id,msg.server_id))
                                bot.send_message(msg.channel_id,"✅ Removed **\""+opt->name+"\"**.");
                            else bot.send_message(msg.channel_id,"⚠️ Failed to remove.");
                            return;
                        }

                        // ── list ─────────────────────────────────────────────
                        if (sub=="list"||sub=="ls") {
                            auto list=g_db->get_autoresponders_for_server(msg.server_id);
                            if(list.empty()){bot.send_message(msg.channel_id,"ℹ️ No autoresponders configured.");return;}
                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed=nlohmann::json::object();
                            embed["title"]="💬 Autoresponders ("+std::to_string(list.size())+"/25)";
                            embed["colour"]="#5865f2";
                            std::string desc;
                            for(const auto& c:list){
                                desc+="**#"+std::to_string(c.id)+" — "+c.name+"**\n";
                                desc+="• Keyword: `"+c.keyword+"` ("+c.match_type+")\n";
                                desc+="• Type: `"+c.message_type+"`\n\n";
                            }
                            embed["description"]=desc;
                            payload.embeds.push_back(embed);
                            bot.send_message(msg.channel_id,payload);
                            return;
                        }

                        // ── info ─────────────────────────────────────────────
                        if (sub=="info") {
                            if(args.size()<2){bot.send_message(msg.channel_id,"usage: `"+pfx+"ar info <name|id>`");return;}
                            std::string q; for(size_t i=1;i<args.size();++i){if(i>1)q+=" ";q+=args[i];}
                            auto opt=resolve_auto<Database::AutoresponderConfig>(msg.server_id,q,
                                [](int id,const std::string& sid){return g_db->get_autoresponder_by_id(id,sid);},
                                [](const std::string& sid,const std::string& n){return g_db->get_autoresponder_by_name(sid,n);});
                            if(!opt){bot.send_message(msg.channel_id,"⚠️ Not found: `"+q+"`");return;}
                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed=nlohmann::json::object();
                            embed["title"]="💬 AR #"+std::to_string(opt->id)+" — "+opt->name;
                            embed["colour"]="#5865f2";
                            std::string desc="**Keyword:** `"+opt->keyword+"` ("+opt->match_type+")\n";
                            desc+="**Type:** `"+opt->message_type+"`\n";
                            if(!opt->content.empty()) desc+="**Content preview:** "+opt->content.substr(0,80)+"\n";
                            desc+="\n**Filters:**\n"+format_filter(opt->filter);
                            embed["description"]=desc;
                            payload.embeds.push_back(embed);
                            bot.send_message(msg.channel_id,payload);
                            return;
                        }

                        // ── edit ─────────────────────────────────────────────
                        if (sub=="edit") {
                            if(args.size()<3){bot.send_message(msg.channel_id,"usage: `"+pfx+"ar edit <name|id> key:val [...]`");return;}
                            auto opt=resolve_auto<Database::AutoresponderConfig>(msg.server_id,args[1],
                                [](int id,const std::string& sid){return g_db->get_autoresponder_by_id(id,sid);},
                                [](const std::string& sid,const std::string& n){return g_db->get_autoresponder_by_name(sid,n);});
                            if(!opt){bot.send_message(msg.channel_id,"⚠️ Not found: `"+args[1]+"`");return;}
                            std::string pairs_text;
                            for(size_t i=2;i<args.size();++i){if(i>2)pairs_text+=" ";pairs_text+=args[i];}
                            auto pairs=parse_kv_pairs(pairs_text);
                            if(pairs.empty()){bot.send_message(msg.channel_id,"⚠️ No valid key:value pairs found.");return;}
                            Database::AutoresponderConfig updated=*opt;
                            nlohmann::json emb=nlohmann::json::object();
                            if(!updated.embed_json.empty()){try{emb=nlohmann::json::parse(updated.embed_json);}catch(...){}};
                            std::vector<std::string> notes;
                            for(const auto&[k,v]:pairs){
                                if(k=="name"){updated.name=v;notes.push_back("name→`"+v+"`");}
                                else if(k=="keyword"){updated.keyword=v;notes.push_back("keyword→`"+v+"`");}
                                else if(k=="match_type"){updated.match_type=v;notes.push_back("match→`"+v+"`");}
                                else if(k=="type"){updated.message_type=v;notes.push_back("type→`"+v+"`");}
                                else if(k=="content"){updated.content=v;notes.push_back("content updated");}
                                else if(k=="title"||k=="description"||k=="color"||k=="image"){
                                    if(v=="none"||v=="clear")emb.erase(k);else emb[k]=v;
                                    notes.push_back(k+" updated");
                                }
                            }
                            updated.embed_json=emb.dump();
                            if(g_db->update_autoresponder(updated)){
                                std::string n; for(size_t i=0;i<notes.size();++i){if(i)n+=", ";n+=notes[i];}
                                bot.send_message(msg.channel_id,"✅ Updated **\""+updated.name+"\"**: "+n+".");
                            } else bot.send_message(msg.channel_id,"⚠️ Update failed.");
                            return;
                        }

                        // ── filter ───────────────────────────────────────────
                        if (sub=="filter") {
                            // .ar filter <name|id> allow|ignore role|user|channel <value>
                            // .ar filter <name|id> remove allow|ignore role|user|channel <value>
                            // .ar filter <name|id> clear
                            if(args.size()<3){bot.send_message(msg.channel_id,"usage: `"+pfx+"ar filter <name|id> allow|ignore|clear ...`");return;}
                            auto opt=resolve_auto<Database::AutoresponderConfig>(msg.server_id,args[1],
                                [](int id,const std::string& sid){return g_db->get_autoresponder_by_id(id,sid);},
                                [](const std::string& sid,const std::string& n){return g_db->get_autoresponder_by_name(sid,n);});
                            if(!opt){bot.send_message(msg.channel_id,"⚠️ Not found: `"+args[1]+"`");return;}
                            Database::AutoresponderConfig updated=*opt;

                            std::string action=args[2]; std::transform(action.begin(),action.end(),action.begin(),::tolower);
                            if(action=="clear"){
                                updated.filter={};
                                if(g_db->update_autoresponder(updated)) bot.send_message(msg.channel_id,"✅ Filters cleared.");
                                else bot.send_message(msg.channel_id,"⚠️ Failed.");
                                return;
                            }
                            bool is_remove=(action=="remove");
                            size_t arg_offset=3;
                            if(is_remove&&args.size()>3){action=args[3];std::transform(action.begin(),action.end(),action.begin(),::tolower);arg_offset=4;}

                            if(args.size()<arg_offset+2){bot.send_message(msg.channel_id,"⚠️ Not enough arguments.");return;}
                            std::string target=args[arg_offset]; std::transform(target.begin(),target.end(),target.begin(),::tolower);
                            std::string value;
                            for(size_t i=arg_offset+1;i<args.size();++i){if(i>arg_offset+1)value+=" ";value+=args[i];}
                            // Clean up channel/role mentions
                            if(value.rfind("<#",0)==0&&value.back()=='>') value=value.substr(2,value.size()-3);
                            if(value.rfind("<%",0)==0&&value.back()=='>') value=value.substr(2,value.size()-3);

                            if(!mutate_filter(updated.filter,action,target,value,is_remove)){
                                bot.send_message(msg.channel_id,"⚠️ Invalid filter parameters. Use: allow|ignore + role|user|channel");
                                return;
                            }
                            if(g_db->update_autoresponder(updated)){
                                bot.send_message(msg.channel_id,"✅ Filter "+(is_remove?std::string("removed"):std::string("added"))+": "+action+" "+target+" `"+value+"`.");
                            } else bot.send_message(msg.channel_id,"⚠️ Failed.");
                            return;
                        }

                        bot.send_message(msg.channel_id,"⚠️ Unknown subcommand. Use `add`, `remove`, `list`, `edit`, `filter`, `info`.");
                    });
            },
            "Utility", 0, 0,
            {"add <name> <keyword>", "remove <name|id>", "list", "edit <name|id> key:val", "filter <name|id> allow|ignore ...", "info <name|id>"}
        });

        // ────────────────────────────────────────────────────────────────────
        // .are — Autoreact
        // ────────────────────────────────────────────────────────────────────
        bot.register_command({
            "are",
            {"autoreact", "autoreaction"},
            "manage keyword-triggered auto emoji reactions.",
            "", {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) { bot.send_message(msg.channel_id,"⚠️ Server only."); return; }
                if (!g_db) { bot.send_message(msg.channel_id,"⚠️ Database unavailable."); return; }
                std::string pfx=msg.prefix.empty()?bot.config().command_prefix:msg.prefix;

                bot.fetch_member(msg.server_id,msg.author.id,
                    [&bot,msg,args,pfx](stoatpp::models::Member member,bool ok){
                        if(!ok){bot.send_message(msg.channel_id,"⚠️ Could not verify permissions.");return;}
                        if(!is_user_trusted_or_owner(bot,msg.server_id,msg.author.id,member)){
                            bot.send_message(msg.channel_id,"⚠️ You need Manage Server permission.");return;
                        }

                        if(args.empty()){
                            bot.send_message(msg.channel_id,
                                "**Autoreact subcommands:**\n"
                                "• `"+pfx+"are add <name> <keyword> <emoji(s)>` — emojis: `:id:`, unicode, comma/space-separated\n"
                                "• `"+pfx+"are remove <name|id>`\n"
                                "• `"+pfx+"are list`\n"
                                "• `"+pfx+"are edit <name|id> key:val` — keys: `keyword`, `match`, `emojis`, `name`\n"
                                "• `"+pfx+"are filter <name|id> allow|ignore role|user|channel <value>`\n"
                                "• `"+pfx+"are filter <name|id> remove allow|ignore role|user|channel <value>`\n"
                                "• `"+pfx+"are filter <name|id> clear`\n"
                                "• `"+pfx+"are info <name|id>`");
                            return;
                        }

                        std::string sub=args[0]; std::transform(sub.begin(),sub.end(),sub.begin(),::tolower);

                        // ── add ──────────────────────────────────────────────
                        if(sub=="add"){
                            if(args.size()<4){bot.send_message(msg.channel_id,"usage: `"+pfx+"are add <name> <keyword> <emoji(s)>`");return;}
                            if(g_db->get_autoreacts_for_server(msg.server_id).size()>=25){
                                bot.send_message(msg.channel_id,"⚠️ Maximum 25 autoreacts per server.");return;
                            }
                            std::string name=args[1], keyword=args[2];
                            std::string emoji_text;
                            for(size_t i=3;i<args.size();++i){if(i>3)emoji_text+=" ";emoji_text+=args[i];}

                            auto emojis=parse_emoji_list(emoji_text);
                            if(emojis.empty()){bot.send_message(msg.channel_id,"⚠️ Could not parse any emojis from: `"+emoji_text+"`");return;}

                            Database::AutoreactConfig c;
                            c.server_id=msg.server_id; c.name=name; c.keyword=keyword;
                            c.match_type="contains";
                            c.emojis=emojis_to_string(emojis);
                            if(g_db->add_autoreact(c)){
                                std::string edisp;
                                for(const auto& e:emojis) edisp+=display_auto_emoji(e)+" ";
                                bot.send_message(msg.channel_id,
                                    "✅ Autoreact **\""+name+"\"** created! (ID: `"+std::to_string(c.id)+"`)\n"
                                    "Reacts with: "+edisp+"\nwhen `"+keyword+"` is seen.");
                            } else bot.send_message(msg.channel_id,"⚠️ Failed to save.");
                            return;
                        }

                        // ── remove ───────────────────────────────────────────
                        if(sub=="remove"||sub=="delete"||sub=="del"){
                            if(args.size()<2){bot.send_message(msg.channel_id,"usage: `"+pfx+"are remove <name|id>`");return;}
                            std::string q; for(size_t i=1;i<args.size();++i){if(i>1)q+=" ";q+=args[i];}
                            auto opt=resolve_auto<Database::AutoreactConfig>(msg.server_id,q,
                                [](int id,const std::string& sid){return g_db->get_autoreact_by_id(id,sid);},
                                [](const std::string& sid,const std::string& n){return g_db->get_autoreact_by_name(sid,n);});
                            if(!opt){bot.send_message(msg.channel_id,"⚠️ Not found: `"+q+"`");return;}
                            if(g_db->remove_autoreact(opt->id,msg.server_id))
                                bot.send_message(msg.channel_id,"✅ Removed **\""+opt->name+"\"**.");
                            else bot.send_message(msg.channel_id,"⚠️ Failed.");
                            return;
                        }

                        // ── list ─────────────────────────────────────────────
                        if(sub=="list"||sub=="ls"){
                            auto list=g_db->get_autoreacts_for_server(msg.server_id);
                            if(list.empty()){bot.send_message(msg.channel_id,"ℹ️ No autoreacts configured.");return;}
                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed=nlohmann::json::object();
                            embed["title"]="⚡ Autoreacts ("+std::to_string(list.size())+"/25)";
                            embed["colour"]="#f39c12";
                            std::string desc;
                            for(const auto& c:list){
                                auto el=parse_emoji_list(c.emojis);
                                std::string edisp;
                                for(const auto& e:el) edisp+=display_auto_emoji(e)+" ";
                                desc+="**#"+std::to_string(c.id)+" — "+c.name+"**\n";
                                desc+="• Keyword: `"+c.keyword+"` ("+c.match_type+")\n";
                                desc+="• Emojis: "+edisp+"\n\n";
                            }
                            embed["description"]=desc;
                            payload.embeds.push_back(embed);
                            bot.send_message(msg.channel_id,payload);
                            return;
                        }

                        // ── info ─────────────────────────────────────────────
                        if(sub=="info"){
                            if(args.size()<2){bot.send_message(msg.channel_id,"usage: `"+pfx+"are info <name|id>`");return;}
                            std::string q; for(size_t i=1;i<args.size();++i){if(i>1)q+=" ";q+=args[i];}
                            auto opt=resolve_auto<Database::AutoreactConfig>(msg.server_id,q,
                                [](int id,const std::string& sid){return g_db->get_autoreact_by_id(id,sid);},
                                [](const std::string& sid,const std::string& n){return g_db->get_autoreact_by_name(sid,n);});
                            if(!opt){bot.send_message(msg.channel_id,"⚠️ Not found: `"+q+"`");return;}
                            auto el=parse_emoji_list(opt->emojis);
                            std::string edisp; for(const auto& e:el) edisp+=display_auto_emoji(e)+" ";
                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed=nlohmann::json::object();
                            embed["title"]="⚡ ARE #"+std::to_string(opt->id)+" — "+opt->name;
                            embed["colour"]="#f39c12";
                            std::string desc="**Keyword:** `"+opt->keyword+"` ("+opt->match_type+")\n";
                            desc+="**Emojis:** "+edisp+"\n\n";
                            desc+="**Filters:**\n"+format_filter(opt->filter);
                            embed["description"]=desc;
                            payload.embeds.push_back(embed);
                            bot.send_message(msg.channel_id,payload);
                            return;
                        }

                        // ── edit ─────────────────────────────────────────────
                        if(sub=="edit"){
                            if(args.size()<3){bot.send_message(msg.channel_id,"usage: `"+pfx+"are edit <name|id> key:val`");return;}
                            auto opt=resolve_auto<Database::AutoreactConfig>(msg.server_id,args[1],
                                [](int id,const std::string& sid){return g_db->get_autoreact_by_id(id,sid);},
                                [](const std::string& sid,const std::string& n){return g_db->get_autoreact_by_name(sid,n);});
                            if(!opt){bot.send_message(msg.channel_id,"⚠️ Not found: `"+args[1]+"`");return;}
                            std::string pt; for(size_t i=2;i<args.size();++i){if(i>2)pt+=" ";pt+=args[i];}
                            auto pairs=parse_kv_pairs(pt);
                            if(pairs.empty()){bot.send_message(msg.channel_id,"⚠️ No valid key:value pairs.");return;}
                            Database::AutoreactConfig updated=*opt;
                            std::vector<std::string> notes;
                            for(const auto&[k,v]:pairs){
                                if(k=="name"){updated.name=v;notes.push_back("name→`"+v+"`");}
                                else if(k=="keyword"){updated.keyword=v;notes.push_back("keyword→`"+v+"`");}
                                else if(k=="match_type"){updated.match_type=v;notes.push_back("match→`"+v+"`");}
                                else if(k=="emojis"){
                                    auto el=parse_emoji_list(v);
                                    updated.emojis=emojis_to_string(el);
                                    notes.push_back("emojis updated ("+std::to_string(el.size())+")");
                                }
                            }
                            if(g_db->update_autoreact(updated)){
                                std::string n; for(size_t i=0;i<notes.size();++i){if(i)n+=", ";n+=notes[i];}
                                bot.send_message(msg.channel_id,"✅ Updated **\""+updated.name+"\"**: "+n+".");
                            } else bot.send_message(msg.channel_id,"⚠️ Update failed.");
                            return;
                        }

                        // ── filter ───────────────────────────────────────────
                        if(sub=="filter"){
                            if(args.size()<3){bot.send_message(msg.channel_id,"usage: `"+pfx+"are filter <name|id> allow|ignore|clear ...`");return;}
                            auto opt=resolve_auto<Database::AutoreactConfig>(msg.server_id,args[1],
                                [](int id,const std::string& sid){return g_db->get_autoreact_by_id(id,sid);},
                                [](const std::string& sid,const std::string& n){return g_db->get_autoreact_by_name(sid,n);});
                            if(!opt){bot.send_message(msg.channel_id,"⚠️ Not found: `"+args[1]+"`");return;}
                            Database::AutoreactConfig updated=*opt;
                            std::string action=args[2]; std::transform(action.begin(),action.end(),action.begin(),::tolower);
                            if(action=="clear"){
                                updated.filter={};
                                if(g_db->update_autoreact(updated)) bot.send_message(msg.channel_id,"✅ Filters cleared.");
                                else bot.send_message(msg.channel_id,"⚠️ Failed.");
                                return;
                            }
                            bool is_remove=(action=="remove");
                            size_t aoff=3;
                            if(is_remove&&args.size()>3){action=args[3];std::transform(action.begin(),action.end(),action.begin(),::tolower);aoff=4;}
                            if(args.size()<aoff+2){bot.send_message(msg.channel_id,"⚠️ Not enough arguments.");return;}
                            std::string target=args[aoff]; std::transform(target.begin(),target.end(),target.begin(),::tolower);
                            std::string value; for(size_t i=aoff+1;i<args.size();++i){if(i>aoff+1)value+=" ";value+=args[i];}
                            if(value.rfind("<#",0)==0&&value.back()=='>') value=value.substr(2,value.size()-3);
                            if(value.rfind("<%",0)==0&&value.back()=='>') value=value.substr(2,value.size()-3);
                            if(!mutate_filter(updated.filter,action,target,value,is_remove)){
                                bot.send_message(msg.channel_id,"⚠️ Invalid filter params.");return;
                            }
                            if(g_db->update_autoreact(updated))
                                bot.send_message(msg.channel_id,"✅ Filter "+(is_remove?std::string("removed"):std::string("added"))+": "+action+" "+target+" `"+value+"`.");
                            else bot.send_message(msg.channel_id,"⚠️ Failed.");
                            return;
                        }

                        bot.send_message(msg.channel_id,"⚠️ Unknown subcommand. Use `add`, `remove`, `list`, `edit`, `filter`, `info`.");
                    });
            },
            "Utility", 0, 0,
            {"add <name> <keyword> <emoji(s)>", "remove <name|id>", "list", "edit <name|id> key:val", "filter <name|id> allow|ignore ...", "info <name|id>"}
        });

        // ────────────────────────────────────────────────────────────────────
        // .recurse (moved from recurring.h — all logic preserved)
        // ────────────────────────────────────────────────────────────────────
        bot.register_command({
            "recurse",
            {"autopost", "scheduled"},
            "configure auto recurring/scheduled messages for channels.",
            "", {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling recurse...\n";
                if (msg.server_id.empty()) { bot.send_message(msg.channel_id,"⚠️ Server only."); return; }
                if (!g_db) { bot.send_message(msg.channel_id,"⚠️ Database unavailable."); return; }

                bot.fetch_member(msg.server_id,msg.author.id,
                    [&bot,msg,args](stoatpp::models::Member member,bool ok){
                        if(!ok){bot.send_message(msg.channel_id,"⚠️ Failed to verify permissions.");return;}
                        std::string pfx=msg.prefix.empty()?bot.config().command_prefix:msg.prefix;

                        // example — owner only
                        if(!args.empty()&&args[0]=="example"){
                            if(!is_bot_owner(bot,msg.author.id)){
                                bot.send_message(msg.channel_id,"⚠️ Owner only.");return;
                            }
                            Database::RecurringMessage ex;
                            ex.server_id=msg.server_id; ex.channel_id=msg.channel_id;
                            ex.message_type="embed"; ex.owner_tokens=true;
                            nlohmann::json emb;
                            emb["title"]="📊 Bot Diagnostics";
                            emb["description"]=
                                "**Server:** {server}\n**Total Servers:** {servercount}\n"
                                "**Members (cached):** {membercount}\n**Cache Size:** {cachecount}\n"
                                "**Uptime:** {uptime}\n**Ping:** {ping}\n"
                                "─────────────────\n"
                                "**RAM:** {ram}\n**CPU:** {cpu}\n**Threads:** {threads}";
                            emb["color"]="#5865f2";
                            ex.embed_json=emb.dump();
                            send_recurring_msg(bot,ex,msg.channel_id,[&bot,msg](bool ok){
                                if(!ok)bot.send_message(msg.channel_id,"⚠️ Failed to send example embed.");
                            });
                            return;
                        }

                        if(!is_user_trusted_or_owner(bot,msg.server_id,msg.author.id,member)){
                            bot.send_message(msg.channel_id,"⚠️ You need Manage Server permission.");return;
                        }

                        if(args.empty()){
                            bot.send_message(msg.channel_id,
                                "📋 **Recurring Messages Subcommands:**\n"
                                "• `"+pfx+"recurse add <name> <#channel> <interval>` (min 5m)\n"
                                "• `"+pfx+"recurse remove <name|id>`\n"
                                "• `"+pfx+"recurse list`\n"
                                "• `" + pfx + "recurse edit <name|id> key:val`\n"
                                "  Keys: `name`, `channel`, `interval`, `type`, `content`, `title`, `description`, `color`, `image`, `owneronly` (owner only)\n"
                                "• `" + pfx + "recurse test <name|id>` \u2014 preview (no timer reset)\n"
                                "• `"+pfx+"recurse send <name|id>` — send now + reset timer\n"
                                "• `"+pfx+"recurse example` — (owner) diagnostics embed\n\n"
                                "Tokens: `{server}` `{membercount}` `{servercount}` `{cachecount}` `{uptime}` `{ping}`\n"
                                "Owner tokens: `{ram}` `{cpu}` `{threads}`");
                            return;
                        }

                        std::string sub=args[0]; std::transform(sub.begin(),sub.end(),sub.begin(),::tolower);

                        auto resolve_rm=[&](const std::string& query)->std::optional<Database::RecurringMessage>{
                            bool all_digits=!query.empty()&&std::all_of(query.begin(),query.end(),[](unsigned char c){return std::isdigit(c);});
                            if(all_digits){try{int id=std::stoi(query);auto r=g_db->get_recurring_by_id(id,msg.server_id);if(r)return r;}catch(...){}};
                            return g_db->get_recurring_by_name(msg.server_id,query);
                        };

                        auto parse_pairs_r=[](const std::string& text)->std::unordered_map<std::string,std::string>{
                            return parse_kv_pairs(text);
                        };

                        // ── add ──────────────────────────────────────────────
                        if(sub=="add"){
                            if(args.size()<4){bot.send_message(msg.channel_id,"usage: `"+pfx+"recurse add <name> <#channel> <interval>`");return;}
                            if(g_db->count_recurring_for_server(msg.server_id)>=10){
                                bot.send_message(msg.channel_id,"⚠️ Maximum 10 recurring messages per server.");return;
                            }
                            std::string rname=args[1];
                            std::string ch_arg=args[2];
                            std::string target_ch=ch_arg;
                            if(ch_arg.rfind("<#",0)==0&&ch_arg.back()=='>') target_ch=ch_arg.substr(2,ch_arg.size()-3);
                            if(g_db->recurring_channel_occupied(target_ch)){
                                bot.send_message(msg.channel_id,"⚠️ Channel <#"+target_ch+"> already has a recurring message.");return;
                            }
                            uint64_t interval=parse_duration(args[3]);
                            if(interval==0){bot.send_message(msg.channel_id,"⚠️ Invalid interval. Examples: `5m`, `1h`, `2d`");return;}
                            if(interval<300){bot.send_message(msg.channel_id,"⚠️ Minimum interval is 5 minutes.");return;}
                            bool owner=is_bot_owner(bot,msg.author.id);
                            std::string tok_hint="\n*Tokens:* `{server}` `{membercount}` `{servercount}` `{cachecount}` `{uptime}` `{ping}`";
                            if(owner) tok_hint+=" `{ram}` `{cpu}` `{threads}`";
                            bot.send_message(msg.channel_id,"💬 **Recurse Setup — \""+rname+"\"**\nReply: `text` or `embed`");
                            bot.await_message(msg.channel_id,msg.author.id,std::chrono::seconds(30),
                                [&bot,msg,rname,target_ch,interval,owner,tok_hint](std::optional<stoatpp::events::Message> r1){
                                    if(!r1){bot.send_message(msg.channel_id,"⏱️ Timed out.");return;}
                                    std::string t=r1->content; std::transform(t.begin(),t.end(),t.begin(),::tolower);
                                    if(t=="cancel"){bot.send_message(msg.channel_id,"🚫 Cancelled.");return;}
                                    if(t!="text"&&t!="embed") t="text";
                                    if(t=="text"){
                                        bot.send_message(msg.channel_id,"Type the message content."+tok_hint);
                                        bot.await_message(msg.channel_id,msg.author.id,std::chrono::seconds(120),
                                            [&bot,msg,rname,target_ch,interval,owner](std::optional<stoatpp::events::Message> r2){
                                                if(!r2||r2->content=="cancel"){bot.send_message(msg.channel_id,"⏱️/🚫");return;}
                                                Database::RecurringMessage rm;
                                                rm.server_id=msg.server_id;rm.channel_id=target_ch;rm.name=rname;
                                                rm.message_type="text";rm.content=r2->content;rm.interval_seconds=interval;rm.owner_tokens=owner;
                                                if(g_db->add_recurring_message(rm))
                                                    bot.send_message(msg.channel_id,"✅ **\""+rname+"\"** will post to <#"+target_ch+"> every **"+fmt_interval(interval)+"** (ID: `"+std::to_string(rm.id)+"`).");
                                                else bot.send_message(msg.channel_id,"⚠️ Failed.");
                                            });
                                    } else {
                                        bot.send_message(msg.channel_id,"Provide embed fields: `title:...`, `description:...`, `color:#hex`, `image:url`, `content:...`"+tok_hint);
                                        bot.await_message(msg.channel_id,msg.author.id,std::chrono::seconds(120),
                                            [&bot,msg,rname,target_ch,interval,owner](std::optional<stoatpp::events::Message> r2){
                                                if(!r2||r2->content=="cancel"){bot.send_message(msg.channel_id,"⏱️/🚫");return;}
                                                auto pairs=parse_kv_pairs(r2->content);
                                                nlohmann::json emb=nlohmann::json::object();
                                                std::string plain;
                                                for(const auto&[k,v]:pairs){if(k=="content")plain=v;else emb[k]=v;}
                                                Database::RecurringMessage rm;
                                                rm.server_id=msg.server_id;rm.channel_id=target_ch;rm.name=rname;
                                                rm.message_type="embed";rm.content=plain;rm.embed_json=emb.dump();
                                                rm.interval_seconds=interval;rm.owner_tokens=owner;
                                                if(g_db->add_recurring_message(rm))
                                                    bot.send_message(msg.channel_id,"✅ **\""+rname+"\"** (embed) will post to <#"+target_ch+"> every **"+fmt_interval(interval)+"** (ID: `"+std::to_string(rm.id)+"`).");
                                                else bot.send_message(msg.channel_id,"⚠️ Failed.");
                                            });
                                    }
                                });
                            return;
                        }

                        // ── remove ───────────────────────────────────────────
                        if(sub=="remove"||sub=="delete"||sub=="del"){
                            if(args.size()<2){bot.send_message(msg.channel_id,"usage: `"+pfx+"recurse remove <name|id>`");return;}
                            std::string q; for(size_t i=1;i<args.size();++i){if(i>1)q+=" ";q+=args[i];}
                            auto opt=resolve_rm(q);
                            if(!opt){bot.send_message(msg.channel_id,"⚠️ Not found: `"+q+"`");return;}
                            if(g_db->remove_recurring_message(opt->id,msg.server_id))
                                bot.send_message(msg.channel_id,"✅ Removed **\""+opt->name+"\"**.");
                            else bot.send_message(msg.channel_id,"⚠️ Failed.");
                            return;
                        }

                        // ── list ─────────────────────────────────────────────
                        if(sub=="list"||sub=="ls"){
                            auto now_ts=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                            auto msgs=g_db->get_recurring_messages_for_server(msg.server_id);
                            if(msgs.empty()){bot.send_message(msg.channel_id,"ℹ️ No recurring messages.");return;}
                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed=nlohmann::json::object();
                            embed["title"]="🔁 Recurring Messages ("+std::to_string(msgs.size())+"/10)";
                            embed["colour"]="#5865f2";
                            std::string desc;
                            for(const auto& rm:msgs){
                                uint64_t next_ts=rm.last_sent+rm.interval_seconds;
                                std::string ns=rm.last_sent==0?"*first send pending*":next_ts<=now_ts?"*sending any moment*":"<t:"+std::to_string(next_ts)+":R>";
                                desc+="**#"+std::to_string(rm.id)+" — "+rm.name+"**\n";
                                desc+="• Channel: <#"+rm.channel_id+">\n";
                                desc+="• Interval: every **"+fmt_interval(rm.interval_seconds)+"**\n";
                                desc+="• Next send: "+ns+"\n";
                                desc+="• Type: `"+rm.message_type+"`"+(rm.owner_tokens?" 🔑":"")+"\n\n";
                            }
                            embed["description"]=desc;
                            payload.embeds.push_back(embed);
                            bot.send_message(msg.channel_id,payload);
                            return;
                        }

                        // ── test ─────────────────────────────────────────────
                        if(sub=="test"){
                            if(args.size()<2){bot.send_message(msg.channel_id,"usage: `"+pfx+"recurse test <name|id>`");return;}
                            std::string q; for(size_t i=1;i<args.size();++i){if(i>1)q+=" ";q+=args[i];}
                            auto opt=resolve_rm(q);
                            if(!opt){bot.send_message(msg.channel_id,"⚠️ Not found: `"+q+"`");return;}
                            send_recurring_msg(bot,*opt,opt->channel_id,[&bot,msg,opt](bool ok){
                                if(ok) bot.send_message(msg.channel_id,"✅ Test **\""+opt->name+"\"** sent to <#"+opt->channel_id+"> *(timer unchanged)*.");
                                else   bot.send_message(msg.channel_id,"⚠️ Send failed.");
                            });
                            return;
                        }

                        // ── send ─────────────────────────────────────────────
                        if(sub=="send"){
                            if(args.size()<2){bot.send_message(msg.channel_id,"usage: `"+pfx+"recurse send <name|id>`");return;}
                            std::string q; for(size_t i=1;i<args.size();++i){if(i>1)q+=" ";q+=args[i];}
                            auto opt=resolve_rm(q);
                            if(!opt){bot.send_message(msg.channel_id,"⚠️ Not found: `"+q+"`");return;}
                            uint64_t now_ts=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                            g_db->update_recurring_last_sent(opt->id,now_ts);
                            send_recurring_msg(bot,*opt,opt->channel_id,[&bot,msg,opt,now_ts](bool ok){
                                if(ok){
                                    uint64_t next=now_ts+opt->interval_seconds;
                                    bot.send_message(msg.channel_id,"✅ Sent **\""+opt->name+"\"** and reset timer. Next: <t:"+std::to_string(next)+":R>.");
                                } else bot.send_message(msg.channel_id,"⚠️ Send failed (timer was still reset).");
                            });
                            return;
                        }

                        // ── edit ─────────────────────────────────────────────
                        if(sub=="edit"){
                            if(args.size()<3){bot.send_message(msg.channel_id,"usage: `"+pfx+"recurse edit <name|id> key:val`");return;}
                            auto opt=resolve_rm(args[1]);
                            if(!opt){bot.send_message(msg.channel_id,"⚠️ Not found: `"+args[1]+"`");return;}
                            std::string pt; for(size_t i=2;i<args.size();++i){if(i>2)pt+=" ";pt+=args[i];}
                            auto pairs=parse_pairs_r(pt);
                            if(pairs.empty()){bot.send_message(msg.channel_id,"⚠️ No valid key:value pairs.");return;}
                            Database::RecurringMessage updated=*opt;
                            nlohmann::json emb=nlohmann::json::object();
                            if(!updated.embed_json.empty()){try{emb=nlohmann::json::parse(updated.embed_json);}catch(...){}};
                            std::vector<std::string> notes;
                            for(const auto&[k,v]:pairs){
                                if(k=="name"){updated.name=v;notes.push_back("name→`"+v+"`");}
                                else if(k=="keyword"||k=="channel"){
                                    std::string ch=v;
                                    if(ch.rfind("<#",0)==0&&ch.back()=='>') ch=ch.substr(2,ch.size()-3);
                                    if(ch!=updated.channel_id&&g_db->recurring_channel_occupied(ch)){
                                        bot.send_message(msg.channel_id,"⚠️ Channel <#"+ch+"> already has a recurring message.");return;
                                    }
                                    updated.channel_id=ch;notes.push_back("channel→<#"+ch+">");
                                }
                                else if(k=="interval"){
                                    uint64_t sec=parse_duration(v);
                                    if(sec<300){bot.send_message(msg.channel_id,"\u26a0\ufe0f Minimum 5 minutes.");return;}
                                    updated.interval_seconds=sec;notes.push_back("interval\u2192"+fmt_interval(sec));
                                }
                                else if(k=="type"){updated.message_type=v;notes.push_back("type\u2192`"+v+"`");}
                                else if(k=="owneronly"){
                                    bool val = (v=="true"||v=="1"||v=="yes");
                                    if(val && !is_bot_owner(bot, msg.author.id)) {
                                        bot.send_message(msg.channel_id, "\u26a0\ufe0f Only the bot owner can enable owner tokens."); return;
                                    }
                                    updated.owner_tokens=val; notes.push_back(std::string("owner_tokens\u2192") + (val?"true":"false"));
                                }
                                else if(k=="content"){updated.content=v;notes.push_back("content updated");}
                                else if(k=="title"||k=="description"||k=="color"||k=="image"){
                                    if(v=="none"||v=="clear")emb.erase(k);else emb[k]=v;
                                    notes.push_back(k+" updated");
                                }
                            }
                            updated.embed_json=emb.dump();
                            if(g_db->update_recurring_message(updated)){
                                std::string n; for(size_t i=0;i<notes.size();++i){if(i)n+=", ";n+=notes[i];}
                                bot.send_message(msg.channel_id,"✅ Updated **\""+updated.name+"\"**: "+n+".");
                            } else bot.send_message(msg.channel_id,"⚠️ Update failed.");
                            return;
                        }

                        bot.send_message(msg.channel_id,"⚠️ Unknown subcommand. Use `add`, `remove`, `list`, `edit`, `test`, `send`, or `example`.");
                    });
            },
            "Utility", 0, 0,
            {"add <name> <#channel> <interval>","remove <name|id>","list","edit <name|id> key:val","test <name|id>","send <name|id>","example"}
        });

        // ────────────────────────────────────────────────────────────────────
        // on_message — keyword scanning for autoresponders and autoreacts
        // ────────────────────────────────────────────────────────────────────
        bot.on_message([&bot](const stoatpp::events::Message& msg) {
            // Ignore bots, DMs, empty content
            if (msg.server_id.empty()) return;
            if (msg.author.bot)        return;
            if (msg.content.empty())   return;
            if (!g_db)                 return;

            std::string content_lower = msg.content;
            std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(), ::tolower);

            // Extract member roles from raw payload (Revolt includes them in message events)
            std::vector<std::string> member_roles = extract_member_roles(msg);

            // ── Autoresponders ────────────────────────────────────────────
            auto responders = g_db->get_autoresponders_for_server(msg.server_id);
            for (const auto& ar : responders) {
                std::string kw = ar.keyword;
                std::transform(kw.begin(), kw.end(), kw.begin(), ::tolower);
                if (!keyword_matches(content_lower, kw, ar.match_type)) continue;
                if (!passes_filter(ar.filter, msg.author.id, msg.channel_id, member_roles)) continue;
                // Fire in a thread to avoid blocking the event loop
                std::thread([&bot, ar, msg]() {
                    send_auto_response(bot, ar, msg);
                }).detach();
                break; // only one autoresponder per message (first match wins)
            }

            // ── Autoreacts ────────────────────────────────────────────────
            auto reacts = g_db->get_autoreacts_for_server(msg.server_id);
            for (const auto& are : reacts) {
                std::string kw = are.keyword;
                std::transform(kw.begin(), kw.end(), kw.begin(), ::tolower);
                if (!keyword_matches(content_lower, kw, are.match_type)) continue;
                if (!passes_filter(are.filter, msg.author.id, msg.channel_id, member_roles)) continue;
                auto emojis = parse_emoji_list(are.emojis);
                std::thread([&bot, emojis, msg]() {
                    for (const auto& e : emojis) {
                        bot.react_to_message(msg.channel_id, msg.id, e);
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }).detach();
                // Multiple autoreacts can fire on the same message (all matches)
            }
        });

        // ────────────────────────────────────────────────────────────────────
        // Background timer: fire due recurring messages every 30 seconds
        // ────────────────────────────────────────────────────────────────────
        bot.start_timer([&bot](stoatpp::timer) {
            if (!g_db) return;
            auto now = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            auto due = g_db->get_due_recurring_messages(now);
            for (const auto& rm : due) {
                g_db->update_recurring_last_sent(rm.id, now);
                std::thread([&bot, rm]() {
                    send_recurring_msg(bot, rm, rm.channel_id);
                }).detach();
            }
        }, 30);
    }
};

} // namespace bronx::cogs
