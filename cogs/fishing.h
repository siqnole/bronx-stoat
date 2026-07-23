#pragma once
#include "games.h"
#include "helpers.h"
#include "stoatpp/bot_module.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace bronx::cogs {

struct RodDef {
  std::string id;
  std::string name;
  int64_t cost = 0;
  int token_cost = 0;
  int req_prestige = 0;
  double luck_mult = 1.0;
  int base_cd = 30; // seconds
  std::string passive_effect;
  std::string desc;
};

struct BaitDef {
  std::string id;
  std::string name;
  int pack_size = 10;
  int64_t cost = 0;
  int token_cost = 0;
  int req_prestige = 0;
  double luck_bonus = 0.0;
  int cd_delta = 0;
  double rarity_boost = 1.0;
  std::string desc;
};

struct RodEffectDef {
  std::string id;
  std::string name;
  int64_t cost = 0;
  int token_cost = 0;
  double luck_bonus = 0.0;
  double double_catch_bonus = 0.0;
  double value_mult = 1.0;
  int cd_reduction = 0;
  std::string special_flag; // "none", "godslayer", "nebula"
  std::string desc;
};

struct FishDef {
  std::string id;
  std::string name;
  std::string rarity;
  double base_weight_min;
  double base_weight_max;
  int64_t base_val_min;
  int64_t base_val_max;
  double base_odds;        // probability e.g. 0.50, 0.25, 0.12...
  std::string effect_type; // "none", "gold_burst", "adrenaline",
                           // "luck_blessing", "prestige_token", "xp_surge"
};

class FishingModule : public stoatpp::bot_module {
public:
  struct ShopSession {
    int current_page = 0;
    int max_pages = 7;
    std::string user_id;
  };

  struct BossSession {
    int current_page = 0;
    int max_pages = 3;
    std::string user_id;
  };

  struct EquipSession {
    std::string author_id;
    std::vector<std::string> owned_emojis;
  };

private:
  std::vector<RodDef> rods_;
  std::vector<BaitDef> baits_;
  std::vector<RodEffectDef> rod_effects_;
  std::vector<FishDef> fish_pool_;

  std::unordered_map<std::string, ShopSession> shop_sessions_;
  std::unordered_map<std::string, BossSession> boss_sessions_;
  std::unordered_map<std::string, EquipSession> equip_sessions_;
  std::mutex shop_mutex_;

  void init_catalogs() {
    // 1. Rods Catalog
    rods_ = {
        // Standard Rods
        {"wooden_rod", "Wooden Rod", 0, 0, 0, 1.0, 30, "None",
         "Old reliable wooden fishing stick."},
        {"bamboo_pole", "Bamboo Cane Pole", 1000, 0, 0, 1.15, 28, "Flexibility",
         "Lightweight bamboo pole with decent elasticity."},
        {"fiberglass_rod", "Fiberglass Spinner", 5000, 0, 0, 1.4, 25, "None",
         "Flexible, lightweight spinner rod."},
        {"graphite_caster", "Graphite Caster", 12000, 0, 0, 1.7, 22,
         "Precision", "Smooth graphite casting rod."},
        {"carbon_rod", "Carbon Fiber Master", 25000, 0, 0, 2.0, 20, "None",
         "High tensile strength carbon rod."},
        {"magma_forge_rod", "Magma Forge Rod", 60000, 0, 0, 2.6, 17,
         "Molten Heat (+15% Value)", "Forged in volcanic magma chambers."},
        {"titanium_harpoon", "Titanium Harpoon Rod", 100000, 0, 0, 3.2, 16,
         "None", "Heavy-duty titanium rod built for leviathans."},
        {"quantum_tether", "Quantum Tether Rod", 250000, 0, 0, 4.2, 14,
         "Quantum Flux (+10% Double Catch)",
         "Anchors line across quantum states."},
        {"neon_cyber_rod", "Neon Cyber-Lure 2077", 500000, 0, 0, 5.5, 12,
         "Sonic Pulse", "Cybernetic rod with holographic sonic attraction."},
        {"hydra_striker", "Hydra Striker Rod", 1200000, 0, 0, 7.5, 10,
         "Triple Hook (+20% Double Catch)",
         "Three-headed rod that strikes with ferocity."},
        {"poseidons_trident", "Poseidon's Trident Rod", 2500000, 0, 0, 10.0, 9,
         "Sea Lord (+50% Value)",
         "Forged in the ocean depths with sea energy."},
        {"nebula_starlight_rod", "Nebula Starlight Rod", 5000000, 0, 0, 14.0, 8,
         "Stardust Aura (2x Mythic Odds)",
         "Woven from stardust and cosmic winds."},

        // Prestige Rods
        {"abyssal_reaper", "Abyssal Reaper Rod", 0, 50, 1, 18.0, 7,
         "Abyssal Hunger (+25% Double Catch)",
         "Harvester of the dark trench depths."},
        {"solar_flare_rod", "Solar Flare Rod", 0, 120, 2, 35.0, 5,
         "Solar Fusion (3x Value)",
         "Blazing plasma rod that vaporizes ocean resistance."},
        {"chronos_anchor", "Chronos Time Anchor", 0, 250, 3, 75.0, 4,
         "Temporal Bend (-50% Cooldown)",
         "Anchor bound to the flow of spacetime."},
        {"void_walker_rod", "Void Walker Rod", 0, 400, 4, 120.0, 3,
         "Void Warp (-60% Cooldown, +35% Double Catch)",
         "Traverses through non-Euclidean voids."},
        {"multiverse_rod", "Multiversal Singularity Rod", 0, 600, 5, 200.0, 2,
         "Singularity Rift (Unlocks Multivertic Rarity & LaTeX FX)",
         "Wields the primordial threads of reality."},
        {"multivertic_god_rod", "Multivertic Omnipotent Rod", 0, 1000, 6, 500.0,
         1, "Multivertic Dominance (5x Multivertic Odds & 5x Value)",
         "Supreme rod transcending all multivertic dimensions."}};

    // 2. Baits Catalog
    baits_ = {
        // Standard Baits
        {"worm", "Earthworm", 10, 10, 0, 0, 0.0, 0, 1.0,
         "Standard garden earthworm bait."},
        {"maggot", "Fat Maggot", 10, 25, 0, 0, 0.05, -1, 1.0,
         "Juicy maggot bait (-1s CD)."},
        {"breadcrumb", "Breadcrumbs", 10, 50, 0, 0, 0.10, -3, 1.0,
         "Attracts fish rapidly (-3s cooldown)."},
        {"minnow", "Live Minnow", 10, 120, 0, 0, 0.20, -2, 1.1,
         "Small live fish lure (+20% Luck)."},
        {"cricket", "Glowing Cricket", 10, 250, 0, 0, 0.30, -2, 1.2,
         "Brite glow attracts larger fish (+30% Luck)."},
        {"squid_strip", "Squid Strip", 10, 500, 0, 0, 0.50, -3, 1.35,
         "Tough squid strip (+50% Luck)."},
        {"shrimp", "Deepsea Shrimp", 10, 1000, 0, 0, 0.75, -4, 1.5,
         "A savory treat for rare ocean fish (+75% Luck)."},
        {"chum_bucket", "Blood Chum Bucket", 5, 2500, 0, 0, 1.20, -4, 2.0,
         "Attracts predatory fish (+120% Luck & 2x Rarity)."},
        {"golden_fly", "Golden Mayfly", 5, 5000, 0, 0, 1.80, -5, 2.5,
         "Shining gold lure (+180% Luck & 2.5x Rarity)."},
        {"plasma_worm", "Plasma Glow Worm", 5, 15000, 0, 0, 2.50, -6, 3.5,
         "Energized plasma lure (+250% Luck)."},

        // Prestige Baits
        {"void_grub", "Void Grub", 5, 0, 5, 1, 4.00, -6, 5.0,
         "Cosmic organism from the abyss (+400% Luck)."},
        {"starlight_lure", "Starlight Lure", 5, 0, 15, 2, 10.00, -8, 12.0,
         "Contains compressed stardust (+1000% Luck)."},
        {"chronos_bait", "Chronos Temporal Bait", 5, 0, 35, 3, 25.00, -10, 30.0,
         "Warps local time (-10s CD, +2500% Luck)."},
        {"hyper_singularity", "Hyper-Singularity Bait", 5, 0, 50, 4, 50.00, -12,
         50.0, "Supercompressed gravity core (+5000% Luck)."},
        {"singularity_core", "Singularity Core Bait", 5, 0, 80, 5, 100.00, -15,
         100.0, "Pure concentrated singularity (+10,000% Luck)."},
        {"multivertic_spark", "Multivertic Essence Spark", 5, 0, 200, 6, 250.00,
         -20, 250.0, "Fragment of multivertic creation (+25,000% Luck)."}};

    // 3. Rod Effects Catalog
    rod_effects_ = {
        {"none", "None", 0, 0, 0.0, 0.0, 1.0, 0, "none", "No active effect."},
        {"aether_glow", "Aether Glow", 50000, 0, 0.25, 0.05, 1.15, 0, "none",
         "Cyan aura granting +25% Luck & +15% Value."},
        {"maelstrom", "Maelstrom Vortex", 200000, 0, 0.50, 0.15, 1.30, 2,
         "none", "Vortex energy (+50% Luck, +15% Double Catch)."},
        {"midas_touch", "Midas Touch", 500000, 0, 0.30, 0.0, 2.50, 0, "none",
         "Golden enchant multiplying fish sell value by 2.5x."},
        {"hyperdrive", "Hyperdrive Engine", 1000000, 0, 0.75, 0.10, 1.50, 5,
         "none", "Reduces cast cooldown by an additional 5s."},
        {"nebula_weaver", "Nebula Weaver", 0, 50, 2.00, 0.20, 3.00, 3, "nebula",
         "Cosmic sparkle granting +200% Luck & Mythic odds boost."},
        {"godslayer_aura", "Godslayer Aura", 0, 150, 5.00, 0.35, 5.00, 5,
         "godslayer",
         "Primordial godslayer aura granting +500% Luck & Divine odds."}};

    // 4. Fish Pool Catalog
    fish_pool_ = {
        // Common (~50% base odds, 1 in 2)
        {"bluegill", "Bluegill", "Common", 0.3, 1.5, 15, 45, 0.50, "none"},
        {"yellow_perch", "Yellow Perch", "Common", 0.5, 2.0, 20, 55, 0.50,
         "none"},
        {"river_trout", "River Trout", "Common", 0.8, 3.5, 30, 80, 0.50,
         "gold_burst"},
        {"catfish", "Channel Catfish", "Common", 1.2, 5.0, 40, 100, 0.50,
         "none"},
        {"mackerel", "Atlantic Mackerel", "Common", 0.6, 2.5, 25, 65, 0.50,
         "none"},

        // Uncommon (~25% base odds, 1 in 4)
        {"red_snapper", "Red Snapper", "Uncommon", 2.5, 8.0, 100, 250, 0.25,
         "none"},
        {"yellowfin_tuna", "Yellowfin Tuna", "Uncommon", 5.0, 18.0, 150, 380,
         0.25, "gold_burst"},
        {"atlantic_salmon", "Atlantic Salmon", "Uncommon", 3.0, 12.0, 120, 320,
         0.25, "xp_surge"},
        {"halibut", "Pacific Halibut", "Uncommon", 8.0, 25.0, 200, 500, 0.25,
         "none"},

        // Rare (~12% base odds, 1 in 8)
        {"swordfish", "Broadbill Swordfish", "Rare", 15.0, 50.0, 500, 1500,
         0.12, "adrenaline"},
        {"hammerhead", "Hammerhead Shark", "Rare", 25.0, 90.0, 800, 2200, 0.12,
         "none"},
        {"electric_eel", "Electric Eel", "Rare", 10.0, 35.0, 600, 1800, 0.12,
         "luck_blessing"},
        {"giant_squid", "Giant Kraken Tentacle", "Rare", 30.0, 100.0, 1000,
         3000, 0.12, "gold_burst"},

        // Epic (~6% base odds, 1 in 17)
        {"great_white", "Great White Shark", "Epic", 80.0, 300.0, 3000, 8500,
         0.06, "gold_burst"},
        {"coelacanth", "Deepsea Coelacanth", "Epic", 40.0, 150.0, 2500, 7000,
         0.06, "xp_surge"},
        {"giant_oarfish", "Giant Sea Serpent Oarfish", "Epic", 60.0, 220.0,
         3500, 10000, 0.06, "luck_blessing"},

        // Legendary (~1.8% base odds, 1 in 55)
        {"golden_leviathan", "Golden Leviathan", "Legendary", 200.0, 800.0,
         20000, 60000, 0.018, "gold_burst"},
        {"megalodon_tooth", "Megalodon Tooth Shark", "Legendary", 350.0, 1200.0,
         30000, 85000, 0.018, "prestige_token"},
        {"phantom_sailfish", "Spectral Phantom Sailfish", "Legendary", 150.0,
         600.0, 18000, 50000, 0.018, "adrenaline"},

        // Mythic (~0.15% base odds, 1 in 666)
        {"phoenix_koi", "Phoenix Emperor Koi", "Mythic", 500.0, 2500.0, 150000,
         450000, 0.0015, "prestige_token"},
        {"crystal_dragonfish", "Crystal Dragonfish", "Mythic", 800.0, 3500.0,
         200000, 600000, 0.0015, "luck_blessing"},

        // Exotic (~0.015% base odds, 1 in 6,666)
        {"cosmic_jellyfish", "Cosmic Nebula Jellyfish", "Exotic", 1500.0,
         8000.0, 1000000, 3500000, 0.00015, "prestige_token"},
        {"chrono_leviathan", "Chrono Spacetime Leviathan", "Exotic", 2500.0,
         12000.0, 1800000, 5000000, 0.00015, "adrenaline"},

        // Divine / Secret (~0.0015% base odds, 1 in 66,666)
        {"poseidons_pet", "Poseidon's Primordial Serpent", "Divine", 5000.0,
         30000.0, 12000000, 40000000, 0.000015, "prestige_token"},
        {"astral_void_whale", "Astral Void Whale", "Divine", 10000.0, 50000.0,
         20000000, 60000000, 0.000015, "luck_blessing"},

        // Singularity (~0.0001% base odds | 1 in 1,000,000)
        {"singularity_serpent", "Singularity Dimensional Serpent",
         "Singularity", 25000.0, 150000.0, 150000000, 500000000, 0.000001,
         "prestige_token"},
        {"galaxy_angler", "Galaxy Swallowed Angler", "Singularity", 40000.0,
         200000.0, 250000000, 750000000, 0.000001, "gold_burst"},

        // Multivertic Primordial (~0.00001% base odds | 1 in 10,000,000)
        {"omnipresent_kraken", "The Omnipresent Kraken God", "Multivertic",
         100000.0, 1000000.0, 2000000000, 5000000000LL, 0.0000001,
         "prestige_token"},
        {"multivertic_architect", "Multivertic Cosmic Architect", "Multivertic",
         500000.0, 5000000.0, 15000000000LL, 50000000000LL, 0.00000002,
         "prestige_token"}};

    if (g_db) {
        auto custom_rods = g_db->get_custom_rods();
        for (const auto& cr : custom_rods) {
            rods_.push_back({cr.rod_id, cr.name, cr.cost, cr.token_cost, cr.req_prestige, cr.luck_mult, cr.base_cd, "Custom", cr.description});
        }
        auto custom_baits = g_db->get_custom_baits();
        for (const auto& cb : custom_baits) {
            baits_.push_back({cb.bait_id, cb.name, cb.pack_size, cb.cost, cb.token_cost, cb.req_prestige, cb.luck_bonus, cb.cd_delta, 1.0, cb.description});
        }
        auto custom_fish = g_db->get_custom_fish();
        for (const auto& cf : custom_fish) {
            fish_pool_.push_back({cf.fish_id, cf.name, cf.rarity, cf.weight_min, cf.weight_max, cf.val_min, cf.val_max, cf.base_odds, "none"});
        }
    }
  }

public:
  static inline FishingModule* instance = nullptr;

  void add_custom_rod_mem(const RodDef& r) { rods_.push_back(r); }
  void add_custom_bait_mem(const BaitDef& b) { baits_.push_back(b); }
  void add_custom_fish_mem(const FishDef& f) { fish_pool_.push_back(f); }

  const RodDef *get_rod(const std::string &id) const {
    for (const auto &r : rods_)
      if (r.id == id)
        return &r;
    return &rods_[0]; // default wooden rod
  }

  void get_prestige_fish_requirements(int current_prestige, std::string &out_rarity, double &out_weight) const {
    if (current_prestige == 0) {
      out_rarity = "Rare";
      out_weight = 10.0;
    } else if (current_prestige == 1) {
      out_rarity = "Epic";
      out_weight = 20.0;
    } else if (current_prestige == 2) {
      out_rarity = "Legendary";
      out_weight = 30.0;
    } else if (current_prestige == 3) {
      out_rarity = "Mythic";
      out_weight = 40.0;
    } else if (current_prestige == 4) {
      out_rarity = "Exotic";
      out_weight = 50.0;
    } else if (current_prestige == 5) {
      out_rarity = "Divine";
      out_weight = 60.0;
    } else {
      out_rarity = "Singularity";
      out_weight = 75.0;
    }
  }

  int get_rarity_precedence(const std::string &rarity) const {
    std::string lower = rarity;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "common") return 0;
    if (lower == "uncommon") return 1;
    if (lower == "rare") return 2;
    if (lower == "epic") return 3;
    if (lower == "legendary") return 4;
    if (lower == "mythic") return 5;
    if (lower == "exotic") return 6;
    if (lower == "divine") return 7;
    if (lower == "singularity") return 8;
    if (lower == "multivertic") return 9;
    return -1;
  }

  std::optional<bronx::Database::CaughtFish> find_prestige_qualifying_fish(
      const std::vector<bronx::Database::CaughtFish> &inv,
      const std::string &req_rarity,
      double req_weight) const {
    int req_prec = get_rarity_precedence(req_rarity);
    std::optional<bronx::Database::CaughtFish> best_match;
    for (const auto &fish : inv) {
      if (get_rarity_precedence(fish.rarity) >= req_prec && fish.weight >= req_weight) {
        if (!best_match || fish.value < best_match->value) {
          best_match = fish;
        }
      }
    }
    return best_match;
  }

  const BaitDef *get_bait(const std::string &id) const {
    for (const auto &b : baits_)
      if (b.id == id)
        return &b;
    return &baits_[0]; // default worm
  }

  const RodEffectDef *get_effect(const std::string &id) const {
    for (const auto &e : rod_effects_)
      if (e.id == id)
        return &e;
    return &rod_effects_[0]; // default none
  }

  std::string resolve_user_id(const std::string &arg) {
    if (arg.size() > 3 && arg.front() == '<' && arg[1] == '@' &&
        arg.back() == '>') {
      return arg.substr(2, arg.size() - 3);
    }
    return arg;
  }

  std::string format_cooldown(int seconds) {
    if (seconds >= 3600) {
      int h = seconds / 3600;
      int m = (seconds % 3600) / 60;
      return std::to_string(h) + "h " + std::to_string(m) + "m";
    }
    if (seconds >= 60) {
      return std::to_string(seconds / 60) + "m " +
             std::to_string(seconds % 60) + "s";
    }
    return std::to_string(seconds) + "s";
  }

  // Levenshtein distance for fuzzy string matching
  int levenshtein_distance(const std::string &s1, const std::string &s2) {
    size_t len1 = s1.size(), len2 = s2.size();
    std::vector<std::vector<int>> d(len1 + 1, std::vector<int>(len2 + 1));
    for (size_t i = 0; i <= len1; ++i)
      d[i][0] = static_cast<int>(i);
    for (size_t j = 0; j <= len2; ++j)
      d[0][j] = static_cast<int>(j);
    for (size_t i = 1; i <= len1; ++i) {
      for (size_t j = 1; j <= len2; ++j) {
        int cost = (tolower(s1[i - 1]) == tolower(s2[j - 1])) ? 0 : 1;
        d[i][j] = std::min(
            {d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
      }
    }
    return d[len1][len2];
  }

  // Generic fuzzy matching helper against candidate string list
  std::string fuzzy_match_str(const std::string &input,
                              const std::vector<std::string> &candidates) {
    if (input.empty() || candidates.empty())
      return input;
    std::string lower_in = input;
    std::transform(lower_in.begin(), lower_in.end(), lower_in.begin(),
                   ::tolower);

    // 1. Exact match
    for (const auto &c : candidates) {
      std::string lower_c = c;
      std::transform(lower_c.begin(), lower_c.end(), lower_c.begin(),
                     ::tolower);
      if (lower_c == lower_in)
        return c;
    }

    // 2. Prefix / Contains match
    for (const auto &c : candidates) {
      std::string lower_c = c;
      std::transform(lower_c.begin(), lower_c.end(), lower_c.begin(),
                     ::tolower);
      if (lower_c.find(lower_in) != std::string::npos)
        return c;
    }

    // 3. Levenshtein distance match
    std::string best = candidates[0];
    int min_dist = 999;
    for (const auto &c : candidates) {
      std::string lower_c = c;
      std::transform(lower_c.begin(), lower_c.end(), lower_c.begin(),
                     ::tolower);
      int dist = levenshtein_distance(lower_in, lower_c);
      if (dist < min_dist) {
        min_dist = dist;
        best = c;
      }
    }

    if (min_dist <= 4)
      return best;
    return input;
  }

  struct PetDef {
    std::string id;
    std::string name;
    std::string emoji;
    std::string rarity;
    std::string description;
    double luck_bonus = 0.0;
    double rarity_boost = 1.0;
    double weight_mult = 1.0;
    double value_mult = 1.0;
    int cd_reduction = 0;
    double double_catch_bonus = 0.0;
    int event_token_cost = 0;
  };

  std::vector<PetDef> get_pets_catalog() const {
    return {{"baby_otter", "Baby River Otter", "🦦", "Common",
             "+0.5x Luck Multiplier on every cast", 0.5, 1.0, 1.0, 1.0, 0, 0.0,
             25},
            {"sun_turtle", "Golden Sun Turtle", "🐢", "Uncommon",
             "+0.5x Rarity Chance Boost", 0.0, 1.5, 1.0, 1.0, 0, 0.0, 50},
            {"frost_penguin", "Frostbite Penguin", "🐧", "Rare",
             "-3s Cooldown Reduction on all casts", 0.0, 1.0, 1.0, 1.0, 3, 0.0,
             100},
            {"coral_axolotl", "Blossom Coral Axolotl", "🦎", "Epic",
             "2.0x Catch Weight Multiplier", 0.0, 1.0, 2.0, 1.0, 0, 0.0, 150},
            {"golden_dolphin", "Golden Sun Dolphin", "🐬", "Legendary",
             "2.0x Sell Value Multiplier on all fish", 0.0, 1.0, 1.0, 2.0, 0,
             0.0, 250},
            {"void_kraken", "Pocket Void Kraken", "🦑", "Mythic",
             "+25% Double Catch Chance & +0.5x Luck", 0.5, 1.0, 1.0, 1.0, 0,
             0.25, 500},
            {"cosmic_dragon", "Cosmic Nebula Dragon", "🐉", "Exotic",
             "+1.0x Luck, 1.5x Value Multiplier, & -2s Cooldown", 1.0, 1.2, 1.0,
             1.5, 2, 0.0, 1000}};
  }

  std::optional<PetDef> get_pet_def(const std::string &pet_id) const {
    for (const auto &p : get_pets_catalog()) {
      if (p.id == pet_id)
        return p;
    }
    return std::nullopt;
  }

  Database::FishBossState get_or_init_boss(std::shared_ptr<Database> db,
                                           const std::string &type) const {
    if (!db)
      return {};
    auto opt = db->get_global_boss(type);
    if (opt)
      return *opt;

    Database::FishBossState b;
    b.boss_type = type;
    if (type == "daily") {
      b.boss_name = "☀️ Sunken Sun-Kraken";
      b.season_theme = "Summer";
      b.current_hp = 50000;
      b.max_hp = 50000;
      b.req_type = "catches";
      b.req_progress = 0;
      b.req_target = 2500;
    } else if (type == "monthly") {
      b.boss_name = "🌙 Abyssal Moon-Titan";
      b.season_theme = "Summer";
      b.current_hp = 500000;
      b.max_hp = 500000;
      b.req_type = "sell_value";
      b.req_progress = 0;
      b.req_target = 25000000;
    } else { // seasonal
      b.boss_name = "🌺 Solar Sunfire Leviathan";
      b.season_theme = "Summer";
      b.current_hp = 250000;
      b.max_hp = 250000;
      b.req_type = "total_weight";
      b.req_progress = 0;
      b.req_target = 50000;
    }
    db->save_global_boss(b);
    return b;
  }

  std::string make_progress_bar(int64_t current, int64_t max_val,
                                int length = 12) const {
    if (max_val <= 0)
      max_val = 1;
    double ratio = static_cast<double>(current) / static_cast<double>(max_val);
    if (ratio < 0.0)
      ratio = 0.0;
    if (ratio > 1.0)
      ratio = 1.0;

    int filled = static_cast<int>(std::round(ratio * length));
    std::string bar = "`[";
    for (int i = 0; i < length; ++i) {
      if (i < filled)
        bar += "█";
      else
        bar += "░";
    }
    bar += "]`";
    return bar;
  }

  stoatpp::models::MessagePayload render_boss_page(stoatpp::cluster &bot,
                                                   const std::string &author_id,
                                                   int page_idx) const {
    if (page_idx < 0)
      page_idx = 0;
    if (page_idx >= 3)
      page_idx = 2;

    stoatpp::models::MessagePayload payload;
    payload.content = " ";
    stoatpp::models::Embed embed;

    std::string footer_nav =
        "\n\n*Page " + std::to_string(page_idx + 1) +
        " of 3 | React ◀ ▶ to navigate or type `.fish boss <1-3>`*";

    if (page_idx == 0) {
      auto b = get_or_init_boss(g_db, "daily");
      int64_t u_contrib = g_db->get_user_boss_contribution(author_id, "daily");
      double pct = (b.max_hp > 0) ? (100.0 * b.current_hp / b.max_hp) : 0.0;
      double req_pct =
          (b.req_target > 0) ? (100.0 * b.req_progress / b.req_target) : 0.0;

      embed.set_colour(0xE74C3C);
      embed.set_title("🗡️ Bronx Global Fish Boss — ☀️ Daily Boss");
      std::string desc = "**Boss Name:** " + b.boss_name +
                         "\n"
                         "**Season Theme:** " +
                         b.season_theme +
                         "\n\n"
                         "**Boss Health:** `" +
                         std::to_string(b.current_hp) + " / " +
                         std::to_string(b.max_hp) + " HP` (" +
                         std::to_string(static_cast<int>(pct)) + "%)\n" +
                         make_progress_bar(b.current_hp, b.max_hp) +
                         "\n\n"
                         "**Global Requirement:** Fished Catches Goal\n"
                         "**Progress:** `" +
                         std::to_string(b.req_progress) + " / " +
                         std::to_string(b.req_target) + " Catches` (" +
                         std::to_string(static_cast<int>(req_pct)) + "%)\n" +
                         make_progress_bar(b.req_progress, b.req_target) +
                         "\n\n"
                         "👤 **Your Contribution:** `" +
                         std::to_string(u_contrib) +
                         " Catches`\n\n"
                         "🎁 **Boss Defeat Rewards:**\n"
                         "• 15 Event Tokens 🎫\n"
                         "• High Value Seasonal Fish drops\n"
                         "• Random Pet Drop chance\n";
      desc += footer_nav;
      embed.set_description(desc);
    } else if (page_idx == 1) {
      auto b = get_or_init_boss(g_db, "monthly");
      int64_t u_contrib =
          g_db->get_user_boss_contribution(author_id, "monthly");
      double pct = (b.max_hp > 0) ? (100.0 * b.current_hp / b.max_hp) : 0.0;
      double req_pct =
          (b.req_target > 0) ? (100.0 * b.req_progress / b.req_target) : 0.0;

      embed.set_colour(0x9B59B6);
      embed.set_title("🗡️ Bronx Global Fish Boss — 🌙 Monthly Boss");
      std::string desc = "**Boss Name:** " + b.boss_name +
                         "\n"
                         "**Season Theme:** " +
                         b.season_theme +
                         "\n\n"
                         "**Boss Health:** `" +
                         std::to_string(b.current_hp) + " / " +
                         std::to_string(b.max_hp) + " HP` (" +
                         std::to_string(static_cast<int>(pct)) + "%)\n" +
                         make_progress_bar(b.current_hp, b.max_hp) +
                         "\n\n"
                         "**Global Requirement:** Total Fished Value Sold\n"
                         "**Progress:** `$" +
                         std::to_string(b.req_progress) + " / $" +
                         std::to_string(b.req_target) + " Coins` (" +
                         std::to_string(static_cast<int>(req_pct)) + "%)\n" +
                         make_progress_bar(b.req_progress, b.req_target) +
                         "\n\n"
                         "👤 **Your Contribution:** `$" +
                         std::to_string(u_contrib) +
                         " Coins Sold`\n\n"
                         "🎁 **Boss Defeat Rewards:**\n"
                         "• 100 Event Tokens 🎫\n"
                         "• Exclusive Mythic Pet Drop chance\n"
                         "• Legendary Rod Enchant Tokens\n";
      desc += footer_nav;
      embed.set_description(desc);
    } else if (page_idx == 2) {
      auto b = get_or_init_boss(g_db, "seasonal");
      int64_t u_contrib =
          g_db->get_user_boss_contribution(author_id, "seasonal");
      double pct = (b.max_hp > 0) ? (100.0 * b.current_hp / b.max_hp) : 0.0;
      double req_pct =
          (b.req_target > 0) ? (100.0 * b.req_progress / b.req_target) : 0.0;

      embed.set_colour(0xF1C40F);
      embed.set_title("🗡️ Bronx Global Fish Boss — 🌺 Seasonal Boss (" +
                      b.season_theme + " Theme)");
      std::string desc = "**Boss Name:** " + b.boss_name +
                         "\n"
                         "**Current Season:** " +
                         b.season_theme +
                         "\n\n"
                         "**Boss Health:** `" +
                         std::to_string(b.current_hp) + " / " +
                         std::to_string(b.max_hp) + " HP` (" +
                         std::to_string(static_cast<int>(pct)) + "%)\n" +
                         make_progress_bar(b.current_hp, b.max_hp) +
                         "\n\n"
                         "**Global Requirement:** Total Weight Fished\n"
                         "**Progress:** `" +
                         std::to_string(b.req_progress) + " / " +
                         std::to_string(b.req_target) + " kg` (" +
                         std::to_string(static_cast<int>(req_pct)) + "%)\n" +
                         make_progress_bar(b.req_progress, b.req_target) +
                         "\n\n"
                         "👤 **Your Contribution:** `" +
                         std::to_string(u_contrib) +
                         " kg Fished`\n\n"
                         "🎁 **Seasonal Rewards:**\n"
                         "• Golden Sun Turtle Pet 🐢\n"
                         "• Summer Sun-Fish ($2,500,000 Coins)\n"
                         "• 250 Event Tokens 🎫\n";
      desc += footer_nav;
      embed.set_description(desc);
    }

    payload.add_embed(embed);
    payload.set_interactions({"⏪", "◀", "▶", "⏩"});
    return payload;
  }

  stoatpp::models::MessagePayload
  render_pets_page(const std::string &author_id) const {
    stoatpp::models::MessagePayload payload;
    payload.content = " ";
    stoatpp::models::Embed embed;
    embed.set_colour(0x9B59B6);

    auto u_opt = g_db->get_fishing_user(author_id);
    if (!u_opt)
      return payload;
    auto u = *u_opt;

    auto user_pets = g_db->get_user_pets(author_id);
    std::optional<PetDef> active_pet = get_pet_def(u.active_pet_id);

    embed.set_title("🐾 Bronx Fishing Pets — 🐕 Companion System");
    std::string desc =
        "Equipped Pet: " +
        (active_pet ? ("**" + active_pet->emoji + " " + active_pet->name +
                       "** (*" + active_pet->description + "*)")
                    : "`None`") +
        "\n\n"
        "Use `.fish pet equip <pet_id>` or `.fish pet unequip` to manage your "
        "equipped pet.\n\n"
        "**YOUR UNLOCKED PETS:**\n";
    if (user_pets.empty()) {
      desc += "*You do not own any pets yet! Earn pets from Bosses or buy them "
              "in `.fish event` shop.*\n";
    } else {
      for (const auto &pid : user_pets) {
        auto pdef = get_pet_def(pid);
        if (pdef) {
          bool is_eq = (pid == u.active_pet_id);
          desc += "• `" + pdef->id + "` — **" + pdef->emoji + " " + pdef->name +
                  "** " + (is_eq ? "**(EQUIPPED)**" : "") +
                  "\n"
                  "   " +
                  pdef->description + "\n";
        }
      }
    }
    embed.set_description(desc);
    payload.add_embed(embed);
    return payload;
  }

  stoatpp::models::MessagePayload
  render_event_shop_page(const std::string &author_id) const {
    stoatpp::models::MessagePayload payload;
    payload.content = " ";
    stoatpp::models::Embed embed;
    embed.set_colour(0x1ABC9C);

    auto u_opt = g_db->get_fishing_user(author_id);
    int tokens = u_opt ? u_opt->event_tokens : 0;

    embed.set_title("🎪 Bronx Fishing Event Shop — 🎫 Exclusive Event Items");
    std::string desc =
        "Your Balance: **" + std::to_string(tokens) +
        " Event Tokens** 🎫\n"
        "Use `.fish event buy <item_id>` to purchase items:\n\n"
        "🐾 **EVENT PETS:**\n"
        "• `baby_otter` — **Baby River Otter** 🦦 (25 Tokens) — *+0.5x Luck*\n"
        "• `sun_turtle` — **Golden Sun Turtle** 🐢 (50 Tokens) — *+0.5x Rarity "
        "Boost*\n"
        "• `frost_penguin` — **Frostbite Penguin** 🐧 (100 Tokens) — *-3s "
        "Cooldown Reduction*\n"
        "• `coral_axolotl` — **Blossom Coral Axolotl** 🦎 (150 Tokens) — *2.0x "
        "Catch Weight*\n"
        "• `golden_dolphin` — **Golden Sun Dolphin** 🐬 (250 Tokens) — *2.0x "
        "Sell Value*\n"
        "• `void_kraken` — **Pocket Void Kraken** 🦑 (500 Tokens) — *+25% "
        "Double Catch & +0.5x Luck*\n\n"
        "✨ **PERMANENT UPGRADES:**\n"
        "• `perm_luck` — **Permanent +0.5x Base Luck** (100 Tokens)\n"
        "• `perm_value` — **Permanent 1.5x Sell Value Multiplier** (150 "
        "Tokens)\n"
        "• `perm_weight` — **Permanent 1.5x Catch Weight Multiplier** (150 "
        "Tokens)\n"
        "• `perm_cd` — **Permanent -2s Cooldown Reduction** (200 Tokens)\n\n"
        "🐠 **SEASONAL TROPHY FISH:**\n"
        "• `summer_sunfish` — **Summer Sun-Fish** (30 Tokens) — *Sells for "
        "2,500,000 coins!*\n";
    embed.set_description(desc);
    payload.add_embed(embed);
    return payload;
  }

  void render_profile(stoatpp::cluster &bot, const std::string &channel_id,
                      const std::string &target_uid) const {
    auto u_opt = g_db->get_fishing_user(target_uid);
    if (!u_opt) {
      bot.send_message(channel_id, "User profile not found.");
      return;
    }
    auto u = *u_opt;
    const RodDef *r = get_rod(u.rod_id);
    const BaitDef *b = get_bait(u.bait_id);
    auto pdef = get_pet_def(u.active_pet_id);

    stoatpp::models::MessagePayload payload;
    payload.content = " ";
    stoatpp::models::Embed embed;
    embed.set_colour(0xFFD700);
    embed.set_title("🏆 Fishing Profile & Flex — <@" + target_uid + ">");

    std::ostringstream ss_wt;
    ss_wt << std::fixed << std::setprecision(1) << u.total_weight;

    std::string rarest_formatted = u.rarest_catch_name;
    if (u.rarest_catch_odds < 1.0) {
      rarest_formatted =
          const_cast<FishingModule *>(this)->format_odds_and_rarity(
              "Custom", u.rarest_catch_odds, u.rarest_catch_name);
    }

    std::string pet_str = pdef ? (pdef->emoji + " **" + pdef->name + "** (" +
                                  pdef->description + ")")
                               : "`None`";

    std::string desc = "🔮 **Prestige Level:** Tier " +
                       std::to_string(u.fish_prestige) + " (" +
                       std::to_string(u.prestige_tokens) +
                       " Tokens)\n"
                       "🎫 **Event Tokens:** " +
                       std::to_string(u.event_tokens) +
                       " 🎫\n"
                       "🎣 **Equipped Rod:** " +
                       r->name +
                       "\n"
                       "🪱 **Equipped Bait:** " +
                       b->name +
                       "\n"
                       "🐾 **Equipped Pet:** " +
                       pet_str +
                       "\n\n"
                       "📊 **Total Catches:** " +
                       std::to_string(u.total_catches) +
                       "\n"
                       "⚖️ **Total Weight Reeled:** " +
                       ss_wt.str() +
                       " kg\n"
                       "💰 **Total Earned / Money Made:** $" +
                       std::to_string(u.total_earned) +
                       " coins\n\n"
                       "🔥 **BEST FISH & RAREST CATCH** 🔥\n" +
                       rarest_formatted + "\n";

    embed.set_description(desc);
    payload.add_embed(embed);
    bot.send_message(channel_id, payload);
  }

  void open_boss_paginator(stoatpp::cluster &bot, const std::string &channel_id,
                           const std::string &author_id,
                           const std::string &arg) {
    int start_page = 0;
    std::string lower_arg = arg;
    std::transform(lower_arg.begin(), lower_arg.end(), lower_arg.begin(),
                   ::tolower);

    if (!lower_arg.empty()) {
      try {
        int p = std::stoi(lower_arg);
        if (p >= 1 && p <= 3)
          start_page = p - 1;
      } catch (...) {
        if (lower_arg == "daily" || lower_arg == "day")
          start_page = 0;
        else if (lower_arg == "monthly" || lower_arg == "month")
          start_page = 1;
        else if (lower_arg == "seasonal" || lower_arg == "season" ||
                 lower_arg == "summer" || lower_arg == "fall" ||
                 lower_arg == "winter" || lower_arg == "spring")
          start_page = 2;
      }
    }

    auto payload = render_boss_page(bot, author_id, start_page);
    bot.send_message(
        channel_id, payload,
        [this, author_id, start_page](stoatpp::models::Message msg, bool ok) {
          if (ok) {
            std::lock_guard<std::mutex> lock(shop_mutex_);
            boss_sessions_[msg.id] = {start_page, 3, author_id};
          }
        });
  }

  // Fuzzy subcommand resolver for .fish
  std::string resolve_subcommand(const std::string &input) {
    std::string lower_in = input;
    std::transform(lower_in.begin(), lower_in.end(), lower_in.begin(),
                   ::tolower);

    if (lower_in == "c" || lower_in == "cast" || lower_in == "casting" ||
        lower_in == "line")
      return "cast";
    if (lower_in == "b" || lower_in == "inv" || lower_in == "inventory" ||
        lower_in == "bucket" || lower_in == "items" || lower_in == "fish")
      return "bucket";
    if (lower_in == "s" || lower_in == "sell" || lower_in == "selling")
      return "sell";
    if (lower_in == "shop" || lower_in == "store" || lower_in == "market" ||
        lower_in == "shp" || lower_in == "tackle")
      return "shop";
    if (lower_in == "buy" || lower_in == "purchase" || lower_in == "get")
      return "buy";
    if (lower_in == "eq" || lower_in == "equip" || lower_in == "use" ||
        lower_in == "wield")
      return "equip";
    if (lower_in == "bait" || lower_in == "usebait" || lower_in == "bt" ||
        lower_in == "lure")
      return "bait";
    if (lower_in == "up" || lower_in == "upg" || lower_in == "upgrade" ||
        lower_in == "stat")
      return "upgrade";
    if (lower_in == "rod" || lower_in == "stats" || lower_in == "info")
      return "rod";
    if (lower_in == "prest" || lower_in == "pr" || lower_in == "prestige" ||
        lower_in == "ascend")
      return "prestige";
    if (lower_in == "flx" || lower_in == "profile" || lower_in == "show" ||
        lower_in == "flex" || lower_in == "me")
      return "flex";
    if (lower_in == "lb" || lower_in == "top" || lower_in == "leaderboard" ||
        lower_in == "ranking")
      return "top";
    if (lower_in == "boss" || lower_in == "bosses" || lower_in == "raid")
      return "boss";
    if (lower_in == "pet" || lower_in == "pets" || lower_in == "companion")
      return "pets";
    if (lower_in == "event" || lower_in == "eventshop" || lower_in == "events")
      return "event";
    if (lower_in == "gift" || lower_in == "send" || lower_in == "trade" ||
        lower_in == "transfer" || lower_in == "give")
      return "gift";

    std::vector<std::string> subcommands = {
        "cast", "bucket",   "sell", "shop", "buy",  "equip", "bait", "upgrade",
        "rod",  "prestige", "flex", "top",  "boss", "pets",  "event", "gift"};
    return fuzzy_match_str(lower_in, subcommands);
  }

  // High Odds Visual Formatting System (as requested by user!)
  std::string format_odds_and_rarity(const std::string &rarity,
                                     double odds_decimal,
                                     const std::string &fish_name) {
    int64_t one_in_x = static_cast<int64_t>(std::round(1.0 / odds_decimal));
    double pct = odds_decimal * 100.0;

    std::ostringstream ss_pct;
    if (pct < 0.001) {
      ss_pct << std::fixed << std::setprecision(6) << pct << "%";
    } else if (pct < 0.01) {
      ss_pct << std::fixed << std::setprecision(4) << pct << "%";
    } else {
      ss_pct << std::fixed << std::setprecision(2) << pct << "%";
    }
    std::string pct_str = ss_pct.str();

    std::ostringstream ss_x;
    ss_x << "1 in ";
    std::string x_str = std::to_string(one_in_x);
    int insert_pos = static_cast<int>(x_str.length()) - 3;
    while (insert_pos > 0) {
      x_str.insert(insert_pos, ",");
      insert_pos -= 3;
    }
    ss_x << x_str;
    std::string odds_text = ss_x.str() + " (" + pct_str + ")";

    if (rarity == "Common") {
      return "🐟 **Common** `" + odds_text + "`";
    } else if (rarity == "Uncommon") {
      return "🟢 **__Uncommon__** `" + odds_text + "`";
    } else if (rarity == "Rare") {
      return "🔵 **__RARE__** `" + odds_text + "`";
    } else if (rarity == "Epic") {
      return "🟣 **__E P I C__** `" + odds_text + "`";
    } else if (rarity == "Legendary") {
      return "🟡 **__⚡ LEGENDARY ⚡__** `" + odds_text + "`";
    } else if (rarity == "Mythic") {
      return "$$\\textcolor{#FF0055}{\\text{\\underline{\\textbf{🔥 MYTHIC (" +
             odds_text + ") 🔥}}}}$$";
    } else if (rarity == "Exotic") {
      return "$$\\textcolor{#FF00FF}{\\text{\\underline{\\textbf{\\large ✨ "
             "EXOTIC (" +
             odds_text + ") ✨}}}}$$";
    } else if (rarity == "Divine") {
      return "$$\\textcolor{#00FFFF}{\\text{\\underline{\\textbf{\\Large 🔱 "
             "DIVINE (" +
             odds_text + ") 🔱}}}}$$";
    } else if (rarity == "Singularity") {
      return "$$\\textcolor{#FFD700}{\\text{\\underline{\\textbf{\\huge 🌟 "
             "SINGULARITY (" +
             odds_text + ") 🌟}}}}$$";
    } else if (rarity == "Multivertic") {
      return "$$\\textcolor{#FF0000}{\\text{\\underline{\\textbf{\\Huge 🌌 "
             "MULTIVERTIC PRIMORDIAL (" +
             odds_text + ") 🌌}}}}$$";
    }
    return "**" + rarity + "** `" + odds_text + "`";
  }

  // Interactive Shop Paginator Renderer (7 Total Pages)
  stoatpp::models::MessagePayload render_shop_page(int page_idx) {
    if (page_idx < 0)
      page_idx = 0;
    if (page_idx >= 7)
      page_idx = 6;

    stoatpp::models::MessagePayload payload;
    payload.content = " ";
    stoatpp::models::Embed embed;

    std::string footer_nav =
        "\n\n*Page " + std::to_string(page_idx + 1) +
        " of 7 | React ◀ ▶ to navigate or type `.shop <1-7/category>`*";

    // Separate standard baits into 2 groups
    std::vector<BaitDef> std_baits;
    std::vector<BaitDef> prest_baits;
    for (const auto &b : baits_) {
      if (b.req_prestige > 0)
        prest_baits.push_back(b);
      else
        std_baits.push_back(b);
    }

    if (page_idx == 0) {
      embed.set_colour(0x3498DB);
      embed.set_title("🛒 Bronx Shop — 🎣 Standard Rods (Part 1/2)");
      std::string desc = "Use `.fish buy <rod_id>` to purchase:\n\n";
      for (size_t i = 0; i < 6 && i < rods_.size(); ++i) {
        const auto &r = rods_[i];
        if (r.req_prestige > 0)
          continue;
        std::string price =
            (r.cost > 0) ? std::to_string(r.cost) + " coins" : "Free";
        desc += "`" + r.id + "` — **" + r.name + "** (" + price +
                ")\n"
                "   Luck: **" +
                std::to_string(r.luck_mult) + "x** | CD: **" +
                std::to_string(r.base_cd) + "s** | Passive: *" +
                r.passive_effect + "*\n";
      }
      desc += footer_nav;
      embed.set_description(desc);
    } else if (page_idx == 1) {
      embed.set_colour(0x3498DB);
      embed.set_title("🛒 Bronx Shop — 🎣 Standard Rods (Part 2/2)");
      std::string desc = "Use `.fish buy <rod_id>` to purchase:\n\n";
      for (size_t i = 6; i < 12 && i < rods_.size(); ++i) {
        const auto &r = rods_[i];
        if (r.req_prestige > 0)
          continue;
        std::string price =
            (r.cost > 0) ? std::to_string(r.cost) + " coins" : "Free";
        desc += "`" + r.id + "` — **" + r.name + "** (" + price +
                ")\n"
                "   Luck: **" +
                std::to_string(r.luck_mult) + "x** | CD: **" +
                std::to_string(r.base_cd) + "s** | Passive: *" +
                r.passive_effect + "*\n";
      }
      desc += footer_nav;
      embed.set_description(desc);
    } else if (page_idx == 2) {
      embed.set_colour(0x9B59B6);
      embed.set_title("🛒 Bronx Shop — 🔮 Prestige Rods");
      std::string desc = "Requires Fishing Prestige. Bought with Tokens:\n\n";
      for (const auto &r : rods_) {
        if (r.req_prestige == 0)
          continue;
        desc += "`" + r.id + "` — **" + r.name + "** (" +
                std::to_string(r.token_cost) + " Tokens | Req P" +
                std::to_string(r.req_prestige) +
                ")\n"
                "   Luck: **" +
                std::to_string(r.luck_mult) + "x** | CD: **" +
                std::to_string(r.base_cd) + "s** | Passive: *" +
                r.passive_effect + "*\n";
      }
      desc += footer_nav;
      embed.set_description(desc);
    } else if (page_idx == 3) {
      embed.set_colour(0x2ECC71);
      embed.set_title("🛒 Bronx Shop — 🪱 Standard Baits (Part 1/2)");
      std::string desc =
          "Use `.fish buy <bait_id> [quantity]` to purchase:\n\n";
      for (size_t i = 0; i < 5 && i < std_baits.size(); ++i) {
        const auto &b = std_baits[i];
        std::string price = std::to_string(b.cost) + " coins / pack of " +
                            std::to_string(b.pack_size);
        desc += "`" + b.id + "` — **" + b.name + "** (" + price +
                ")\n"
                "   Luck: **+" +
                std::to_string(static_cast<int>(b.luck_bonus * 100)) +
                "%** | CD: **" + std::to_string(b.cd_delta) + "s**\n";
      }
      desc += footer_nav;
      embed.set_description(desc);
    } else if (page_idx == 4) {
      embed.set_colour(0x2ECC71);
      embed.set_title("🛒 Bronx Shop — 🪱 Standard Baits (Part 2/2)");
      std::string desc =
          "Use `.fish buy <bait_id> [quantity]` to purchase:\n\n";
      for (size_t i = 5; i < 10 && i < std_baits.size(); ++i) {
        const auto &b = std_baits[i];
        std::string price = std::to_string(b.cost) + " coins / pack of " +
                            std::to_string(b.pack_size);
        desc += "`" + b.id + "` — **" + b.name + "** (" + price +
                ")\n"
                "   Luck: **+" +
                std::to_string(static_cast<int>(b.luck_bonus * 100)) +
                "%** | CD: **" + std::to_string(b.cd_delta) + "s**\n";
      }
      desc += footer_nav;
      embed.set_description(desc);
    } else if (page_idx == 5) {
      embed.set_colour(0x9B59B6);
      embed.set_title("🛒 Bronx Shop — 🔮 Prestige Baits");
      std::string desc = "High-tier bait bought with Prestige Tokens:\n\n";
      for (const auto &b : prest_baits) {
        std::string price = std::to_string(b.token_cost) + " Tokens (Req P" +
                            std::to_string(b.req_prestige) + ") / pack of " +
                            std::to_string(b.pack_size);
        desc += "`" + b.id + "` — **" + b.name + "** (" + price +
                ")\n"
                "   Luck: **+" +
                std::to_string(static_cast<int>(b.luck_bonus * 100)) +
                "%** | CD: **" + std::to_string(b.cd_delta) + "s**\n";
      }
      desc += footer_nav;
      embed.set_description(desc);
    } else if (page_idx == 6) {
      embed.set_colour(0xF1C40F);
      embed.set_title("🛒 Bronx Shop — ✨ Rod Enchants & Stat Upgrades");
      std::string desc = "**ROD ENCHANTS:**\n";
      for (const auto &ef : rod_effects_) {
        if (ef.id == "none")
          continue;
        std::string price = (ef.cost > 0)
                                ? std::to_string(ef.cost) + " coins"
                                : std::to_string(ef.token_cost) + " Tokens";
        desc += "`" + ef.id + "` — **" + ef.name + "** (" + price +
                ")\n"
                "   " +
                ef.desc + "\n";
      }
      desc += "\n**ROD STAT UPGRADES:**\n"
              "Use `.fish upgrade <stat>` to upgrade:\n"
              "• `luck` (+15% Luck/lvl) | `cooldown` (-1s CD/lvl)\n"
              "• `double` (+5% Double Catch/lvl) | `rare` (+10% Rare "
              "Multiplier/lvl)\n";
      desc += footer_nav;
      embed.set_description(desc);
    }

    payload.add_embed(embed);
    payload.set_interactions({"⏪", "◀", "▶", "⏩"});
    return payload;
  }

  void open_shop_paginator(stoatpp::cluster &bot, const std::string &channel_id,
                           const std::string &author_id,
                           const std::string &arg) {
    int start_page = 0;
    std::string lower_arg = arg;
    std::transform(lower_arg.begin(), lower_arg.end(), lower_arg.begin(),
                   ::tolower);

    if (!lower_arg.empty()) {
      try {
        int p = std::stoi(lower_arg);
        if (p >= 1 && p <= 7)
          start_page = p - 1;
      } catch (...) {
        if (lower_arg == "rod" || lower_arg == "rods")
          start_page = 0;
        else if (lower_arg == "prestige" || lower_arg == "prest")
          start_page = 2;
        else if (lower_arg == "bait" || lower_arg == "baits")
          start_page = 3;
        else if (lower_arg == "effect" || lower_arg == "effects" ||
                 lower_arg == "enchant" || lower_arg == "upgrade")
          start_page = 6;
      }
    }

    auto payload = render_shop_page(start_page);

    bot.send_message(channel_id, payload,
                     [this, start_page, author_id](stoatpp::models::Message res,
                                                   bool success) {
                       if (success) {
                         std::lock_guard<std::mutex> lock(shop_mutex_);
                         shop_sessions_[res.id] = {start_page, 7, author_id};
                       }
                     });
  }

  std::string get_item_emoji(const std::string& item_id) {
    if (item_id == "wooden_rod") return "🪵";
    if (item_id == "bamboo_pole") return "🎋";
    if (item_id == "fiberglass_rod") return "🎣";
    if (item_id == "graphite_caster") return "🖋️";
    if (item_id == "carbon_rod") return "🏴";
    if (item_id == "magma_forge_rod") return "🌋";
    if (item_id == "titanium_harpoon") return "🐳";
    if (item_id == "quantum_tether") return "🧬";
    if (item_id == "neon_cyber_rod") return "⚡";
    if (item_id == "hydra_striker") return "🐉";
    if (item_id == "poseidons_trident") return "🔱";
    if (item_id == "nebula_starlight_rod") return "🌌";
    if (item_id == "abyssal_reaper") return "💀";
    if (item_id == "solar_flare_rod") return "🔴";
    if (item_id == "chronos_anchor") return "⚓";
    if (item_id == "void_walker_rod") return "🕳️";
    if (item_id == "multiverse_rod") return "🌀";
    if (item_id == "multivertic_god_rod") return "👑";

    if (item_id == "worm") return "🪱";
    if (item_id == "maggot") return "🐛";
    if (item_id == "breadcrumb") return "🍞";
    if (item_id == "minnow") return "🐟";
    if (item_id == "cricket") return "🦗";
    if (item_id == "squid_strip") return "🦑";
    if (item_id == "shrimp") return "🦐";
    if (item_id == "chum_bucket") return "🪣";
    if (item_id == "golden_fly") return "🪰";
    if (item_id == "plasma_worm") return "🧪";
    if (item_id == "void_grub") return "🦠";
    if (item_id == "starlight_lure") return "⭐";
    if (item_id == "chronos_bait") return "⏳";
    if (item_id == "hyper_singularity") return "🪐";
    if (item_id == "singularity_core") return "🖤";
    if (item_id == "multivertic_spark") return "✨";

    return "📦";
  }

  std::string get_item_id_from_emoji(const std::string& emoji) {
    if (emoji == "🪵") return "wooden_rod";
    if (emoji == "🎋") return "bamboo_pole";
    if (emoji == "🎣") return "fiberglass_rod";
    if (emoji == "🖋️") return "graphite_caster";
    if (emoji == "🏴") return "carbon_rod";
    if (emoji == "🌋") return "magma_forge_rod";
    if (emoji == "🐳") return "titanium_harpoon";
    if (emoji == "🧬") return "quantum_tether";
    if (emoji == "⚡") return "neon_cyber_rod";
    if (emoji == "🐉") return "hydra_striker";
    if (emoji == "🔱") return "poseidons_trident";
    if (emoji == "🌌") return "nebula_starlight_rod";
    if (emoji == "💀") return "abyssal_reaper";
    if (emoji == "🔴") return "solar_flare_rod";
    if (emoji == "⚓") return "chronos_anchor";
    if (emoji == "🕳️") return "void_walker_rod";
    if (emoji == "🌀") return "multiverse_rod";
    if (emoji == "👑") return "multivertic_god_rod";

    if (emoji == "🪱") return "worm";
    if (emoji == "🐛") return "maggot";
    if (emoji == "🍞") return "breadcrumb";
    if (emoji == "🐟") return "minnow";
    if (emoji == "🦗") return "cricket";
    if (emoji == "🦑") return "squid_strip";
    if (emoji == "🦐") return "shrimp";
    if (emoji == "🪣") return "chum_bucket";
    if (emoji == "🪰") return "golden_fly";
    if (emoji == "🧪") return "plasma_worm";
    if (emoji == "🦠") return "void_grub";
    if (emoji == "⭐") return "starlight_lure";
    if (emoji == "⏳") return "chronos_bait";
    if (emoji == "🪐") return "hyper_singularity";
    if (emoji == "🖤") return "singularity_core";
    if (emoji == "✨") return "multivertic_spark";

    return "";
  }

  void execute_sell(stoatpp::cluster &bot, const stoatpp::events::Message &msg, const std::vector<std::string> &args) {
    if (args.size() < 2 || args[1] == "all") {
      auto [count, total_val] =
          g_db->sell_all_user_fish(msg.author.id);
      if (count == 0) {
        bot.send_message(msg.channel_id,
                         "You have no fish in your bucket to sell.");
      } else {
        auto b_monthly = get_or_init_boss(g_db, "monthly");
        b_monthly.req_progress += total_val;
        if (b_monthly.req_progress >= b_monthly.req_target) {
          b_monthly.req_progress = 0;
          auto u_seller = g_db->get_fishing_user(msg.author.id);
          if (u_seller) {
            u_seller->event_tokens += 100;
            g_db->update_fishing_user(*u_seller);
          }
        }
        g_db->save_global_boss(b_monthly);
        g_db->add_user_boss_contribution(msg.author.id, "monthly",
                                         total_val);

        bot.send_message(msg.channel_id,
                         "💰 Sold **" + std::to_string(count) +
                             "** fish for a total of **" +
                             std::to_string(total_val) + "** coins!");
      }
      return;
    }

    std::string target_rarity = args[1];
    auto [count, total_val] =
        g_db->sell_user_fish_by_rarity(msg.author.id, target_rarity);
    if (count > 0) {
      auto b_monthly = get_or_init_boss(g_db, "monthly");
      b_monthly.req_progress += total_val;
      if (b_monthly.req_progress >= b_monthly.req_target) {
        b_monthly.req_progress = 0;
        auto u_seller = g_db->get_fishing_user(msg.author.id);
        if (u_seller) {
          u_seller->event_tokens += 100;
          g_db->update_fishing_user(*u_seller);
        }
      }
      g_db->save_global_boss(b_monthly);
      g_db->add_user_boss_contribution(msg.author.id, "monthly",
                                       total_val);
    }
    if (count == 0) {
      bot.send_message(msg.channel_id, "No fish of rarity **" +
                                           target_rarity +
                                           "** found in your bucket.");
    } else {
      bot.send_message(msg.channel_id,
                       "💰 Sold **" + std::to_string(count) + "** " +
                           target_rarity + " fish for **" +
                           std::to_string(total_val) + "** coins!");
    }
  }

  void open_equip_selector(stoatpp::cluster& bot, const std::string& channel_id, const std::string& author_id) {
    if (!g_db) return;

    auto u_opt = g_db->get_fishing_user(author_id);
    if (!u_opt) return;
    auto u = *u_opt;

    std::vector<std::string> owned_rods = g_db->get_unlocked_rods(author_id);
    auto owned_baits = g_db->get_user_baits(author_id);

    stoatpp::models::MessagePayload payload;
    payload.content = " ";
    stoatpp::models::Embed embed;
    embed.set_colour(0x3498DB);
    embed.set_title("🎒 Gear Inventory Selector");

    std::string desc = "Click a reaction below to quickly equip a rod or bait!\n\n";

    desc += "🎣 **Rods Owned:**\n";
    std::vector<std::string> emojis;
    for (const auto& rod_id : owned_rods) {
      const RodDef* r = get_rod(rod_id);
      if (r) {
        std::string emoji = get_item_emoji(rod_id);
        std::string active_indicator = (u.rod_id == rod_id) ? " 🟢 *(Active)*" : "";
        desc += emoji + " **" + r->name + "**" + active_indicator + "\n";
        emojis.push_back(emoji);
      }
    }

    desc += "\n🪱 **Baits Owned:**\n";
    bool has_any_bait = false;
    for (const auto& b : owned_baits) {
      if (b.quantity > 0) {
        const BaitDef* def = get_bait(b.bait_id);
        if (def) {
          std::string emoji = get_item_emoji(b.bait_id);
          std::string active_indicator = (u.bait_id == b.bait_id) ? " 🟢 *(Active)*" : "";
          desc += emoji + " **" + def->name + "** (x" + std::to_string(b.quantity) + ")" + active_indicator + "\n";
          emojis.push_back(emoji);
          has_any_bait = true;
        }
      }
    }
    if (!has_any_bait) {
      desc += "*You do not own any bait. Visit `.shop` or `.tackle` to buy some!*\n";
    }

    embed.set_description(desc);
    payload.add_embed(embed);
    payload.set_interactions(emojis);

    bot.send_message(channel_id, payload, [this, author_id, emojis](stoatpp::models::Message res, bool success) {
      if (success) {
        std::lock_guard<std::mutex> lock(shop_mutex_);
        equip_sessions_[res.id] = {author_id, emojis};
      }
    });
  }

public:
  FishingModule() {
      instance = this;
      init_catalogs();
  }

  void register_handlers(stoatpp::cluster &bot) override {
    // Standalone .shop command alias
    // Standalone .profile command alias
    bot.register_command(
        {"profile",
         {"fishprofile", "fprofile"},
         "shows your fishing profile, total catches, reels, money made, and "
         "best fish!",
         "profile [@user]",
         {"[@user]"},
         [this, &bot](stoatpp::cluster &b, const stoatpp::events::Message &msg,
                      const std::vector<std::string> &args) {
           std::string target_uid = msg.author.id;
           if (!args.empty())
             target_uid = resolve_user_id(args[0]);
           render_profile(b, msg.channel_id, target_uid);
         },
         "Economy"});

    // Standalone .equip command
    bot.register_command(
        {"equip",
         {"eq", "wield"},
         "equip your unlocked rod or owned bait.",
         "equip [item_id]",
         {"[item_id]"},
         [this](stoatpp::cluster &bot, const stoatpp::events::Message &msg,
                const std::vector<std::string> &args) {
           if (args.empty()) {
             open_equip_selector(bot, msg.channel_id, msg.author.id);
             return;
           }

           std::string target = args[0];
           auto owned_rods = g_db->get_unlocked_rods(msg.author.id);
           std::string target_rod = fuzzy_match_str(target, owned_rods);

           if (g_db->has_unlocked_rod(msg.author.id, target_rod)) {
             auto u_opt = g_db->get_fishing_user(msg.author.id);
             if (u_opt) {
               auto u = *u_opt;
               u.rod_id = target_rod;
               g_db->update_fishing_user(u);
               const RodDef *r = get_rod(target_rod);
               bot.send_message(msg.channel_id, "🎣 Equipped **" + r->name + "**!");
             }
             return;
           }

           std::vector<std::string> bait_ids;
           for (const auto &b : baits_) bait_ids.push_back(b.id);
           std::string target_bait = fuzzy_match_str(target, bait_ids);

           int qty = g_db->get_bait_count(msg.author.id, target_bait);
           if (qty > 0) {
             auto u_opt = g_db->get_fishing_user(msg.author.id);
             if (u_opt) {
               auto u = *u_opt;
               u.bait_id = target_bait;
               g_db->update_fishing_user(u);
               const BaitDef *b = get_bait(target_bait);
               bot.send_message(msg.channel_id, "🪱 Equipped **" + b->name + "** bait!");
             }
             return;
           }

           bot.send_message(msg.channel_id, "❌ You do not own a rod or bait of type `" + target + "`.");
         },
         "Economy"});

    // Standalone .tackle command alias
    bot.register_command(
        {"tackle",
         {},
         "opens the Bronx fishing shop interactive paginator.",
         "tackle [page/category]",
         {"[page/category]"},
         [this](stoatpp::cluster &bot, const stoatpp::events::Message &msg,
                const std::vector<std::string> &args) {
           std::string arg = args.empty() ? "" : args[0];
           open_shop_paginator(bot, msg.channel_id, msg.author.id, arg);
         },
         "Economy"});

    // Standalone .boss command alias
    bot.register_command(
        {"boss",
         {"fishboss", "bosses"},
         "view active global passive fish bosses and team progress.",
         "boss [1-3/daily/monthly/seasonal]",
         {"[page]"},
         [this](stoatpp::cluster &bot, const stoatpp::events::Message &msg,
                const std::vector<std::string> &args) {
           std::string arg = args.empty() ? "" : args[0];
           open_boss_paginator(bot, msg.channel_id, msg.author.id, arg);
         },
         "Economy"});

    // Standalone .pets command alias
    bot.register_command(
        {"pets",
         {"fishpets", "pet"},
         "view and equip your unlocked fishing pet companions.",
         "pets [equip/unequip] [pet_id]",
         {"[action]", "[pet_id]"},
         [this](stoatpp::cluster &bot, const stoatpp::events::Message &msg,
                const std::vector<std::string> &args) {
           if (!args.empty() && (args[0] == "equip" || args[0] == "unequip" ||
                                 args[0] == "use")) {
             auto u_opt = g_db->get_fishing_user(msg.author.id);
             if (!u_opt)
               return;
             auto u = *u_opt;
             if (args[0] == "equip" || args[0] == "use") {
               if (args.size() < 2) {
                 bot.send_message(msg.channel_id,
                                  "❌ Usage: `.pets equip <pet_id>`");
                 return;
               }
               if (!g_db->has_user_pet(msg.author.id, args[1])) {
                 bot.send_message(msg.channel_id,
                                  "❌ You do not own that pet!");
                 return;
               }
               u.active_pet_id = args[1];
               g_db->update_fishing_user(u);
               bot.send_message(msg.channel_id,
                                "✅ Equipped pet companion `" + args[1] + "`!");
               return;
             } else if (args[0] == "unequip") {
               u.active_pet_id = "none";
               g_db->update_fishing_user(u);
               bot.send_message(msg.channel_id, "✅ Unequipped pet companion.");
               return;
             }
           }
           auto payload = render_pets_page(msg.author.id);
           bot.send_message(msg.channel_id, payload);
         },
         "Economy"});

    // Standalone .event command alias
    bot.register_command(
        {"event",
         {"eventshop", "eventbuy"},
         "access the fishing event shop for exclusive pets and permanent "
         "upgrades.",
         "event [buy] [item_id]",
         {"[action]", "[item_id]"},
         [this](stoatpp::cluster &bot, const stoatpp::events::Message &msg,
                const std::vector<std::string> &args) {
           if (args.size() >= 2 &&
               (args[0] == "buy" || args[0] == "purchase")) {
             std::string item_target = args[1];
             auto u_opt = g_db->get_fishing_user(msg.author.id);
             if (!u_opt)
               return;
             auto u = *u_opt;

             auto pdef = get_pet_def(item_target);
             if (pdef) {
               if (u.event_tokens < pdef->event_token_cost) {
                 bot.send_message(msg.channel_id,
                                  "❌ Requires " +
                                      std::to_string(pdef->event_token_cost) +
                                      " Event Tokens 🎫.");
                 return;
               }
               if (g_db->has_user_pet(msg.author.id, item_target)) {
                 bot.send_message(msg.channel_id,
                                  "❌ You already own this pet!");
                 return;
               }
               u.event_tokens -= pdef->event_token_cost;
               g_db->update_fishing_user(u);
               g_db->add_user_pet(msg.author.id, item_target);
               bot.send_message(msg.channel_id, "🎉 Purchased pet **" +
                                                    pdef->emoji + " " +
                                                    pdef->name + "**!");
               return;
             }
             if (item_target == "perm_luck") {
               if (u.event_tokens < 100) {
                 bot.send_message(msg.channel_id,
                                  "❌ Costs 100 Event Tokens 🎫.");
                 return;
               }
               u.event_tokens -= 100;
               u.perm_luck += 0.5;
               g_db->update_fishing_user(u);
               bot.send_message(msg.channel_id,
                                "🎉 Purchased **Permanent +0.5x Base Luck**!");
               return;
             } else if (item_target == "perm_value") {
               if (u.event_tokens < 150) {
                 bot.send_message(msg.channel_id,
                                  "❌ Costs 150 Event Tokens 🎫.");
                 return;
               }
               u.event_tokens -= 150;
               u.perm_val_mult *= 1.5;
               g_db->update_fishing_user(u);
               bot.send_message(
                   msg.channel_id,
                   "🎉 Purchased **Permanent 1.5x Sell Value Multiplier**!");
               return;
             } else if (item_target == "perm_weight") {
               if (u.event_tokens < 150) {
                 bot.send_message(msg.channel_id,
                                  "❌ Costs 150 Event Tokens 🎫.");
                 return;
               }
               u.event_tokens -= 150;
               u.perm_wt_mult *= 1.5;
               g_db->update_fishing_user(u);
               bot.send_message(
                   msg.channel_id,
                   "🎉 Purchased **Permanent 1.5x Catch Weight Multiplier**!");
               return;
             } else if (item_target == "perm_cd") {
               if (u.event_tokens < 200) {
                 bot.send_message(msg.channel_id,
                                  "❌ Costs 200 Event Tokens 🎫.");
                 return;
               }
               u.event_tokens -= 200;
               u.perm_cd_reduction += 2;
               g_db->update_fishing_user(u);
               bot.send_message(
                   msg.channel_id,
                   "🎉 Purchased **Permanent -2s Cooldown Reduction**!");
               return;
             } else if (item_target == "summer_sunfish") {
               if (u.event_tokens < 30) {
                 bot.send_message(msg.channel_id,
                                  "❌ Costs 30 Event Tokens 🎫.");
                 return;
               }
               u.event_tokens -= 30;
               g_db->update_fishing_user(u);
               g_db->add_fish_to_inventory(msg.author.id, "summer_sunfish",
                                           "Summer Sun-Fish", "Epic", 0.05,
                                           125.0, 2500000);
               bot.send_message(
                   msg.channel_id,
                   "🎉 Purchased **Summer Sun-Fish** ($2,500,000 value)!");
               return;
             }
             bot.send_message(msg.channel_id,
                              "❌ Unknown event item `" + item_target + "`.");
             return;
           }
           auto payload = render_event_shop_page(msg.author.id);
           bot.send_message(msg.channel_id, payload);
         },
         "Economy"});

    // Interactive reaction pagination handler for shop & boss paginators
    bot.on_message_react([this, &bot](const stoatpp::events::MessageReact &e) {
      if (e.user_id == bot.current_user().id)
        return;

      std::unique_lock<std::mutex> lock(shop_mutex_);
      auto it_s = shop_sessions_.find(e.id);
      if (it_s != shop_sessions_.end()) {
        int page = it_s->second.current_page;
        int max_pages = it_s->second.max_pages;

        bool changed = false;
        if (e.emoji_id == "⏪" || e.emoji_id == "⏮" || e.emoji_id == "⏮️") {
          page = 0;
          changed = true;
        } else if (e.emoji_id == "◀" || e.emoji_id == "⬅" ||
                   e.emoji_id == "◀️") {
          page = (page - 1 + max_pages) % max_pages;
          changed = true;
        } else if (e.emoji_id == "▶" || e.emoji_id == "➡" ||
                   e.emoji_id == "▶️") {
          page = (page + 1) % max_pages;
          changed = true;
        } else if (e.emoji_id == "⏩" || e.emoji_id == "⏭" ||
                   e.emoji_id == "⏭️") {
          page = max_pages - 1;
          changed = true;
        }

        if (changed) {
          shop_sessions_[e.id].current_page = page;
          lock.unlock();
          auto payload = render_shop_page(page);
          bot.edit_message(e.channel_id, e.id, payload, [&bot, e](auto, bool) {
            bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
          });
        } else {
          lock.unlock();
          bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
        }
        return;
      }

      auto it_b = boss_sessions_.find(e.id);
      if (it_b != boss_sessions_.end()) {
        int page = it_b->second.current_page;
        int max_pages = it_b->second.max_pages;

        bool changed = false;
        if (e.emoji_id == "⏪" || e.emoji_id == "⏮" || e.emoji_id == "⏮️") {
          page = 0;
          changed = true;
        } else if (e.emoji_id == "◀" || e.emoji_id == "⬅" ||
                   e.emoji_id == "◀️") {
          page = (page - 1 + max_pages) % max_pages;
          changed = true;
        } else if (e.emoji_id == "▶" || e.emoji_id == "➡" ||
                   e.emoji_id == "▶️") {
          page = (page + 1) % max_pages;
          changed = true;
        } else if (e.emoji_id == "⏩" || e.emoji_id == "⏭" ||
                   e.emoji_id == "⏭️") {
          page = max_pages - 1;
          changed = true;
        }

        if (changed) {
          boss_sessions_[e.id].current_page = page;
          lock.unlock();
          auto payload = render_boss_page(bot, e.user_id, page);
          bot.edit_message(e.channel_id, e.id, payload, [&bot, e](auto, bool) {
            bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
          });
        } else {
          lock.unlock();
          bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
        }
        return;
      }

      auto it_eq = equip_sessions_.find(e.id);
      if (it_eq != equip_sessions_.end()) {
        std::string author_id = it_eq->second.author_id;
        std::vector<std::string> owned_emojis = it_eq->second.owned_emojis;

        if (e.user_id != author_id) {
          lock.unlock();
          bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
          return;
        }

        std::string item_id = get_item_id_from_emoji(e.emoji_id);
        if (!item_id.empty()) {
          auto u_opt = g_db->get_fishing_user(author_id);
          if (u_opt) {
            auto u = *u_opt;
            std::string item_name = "";

            // Is it a rod?
            const RodDef* r = get_rod(item_id);
            if (r) {
              u.rod_id = item_id;
              item_name = r->name;
            } else {
              // Is it a bait?
              const BaitDef* b = get_bait(item_id);
              if (b) {
                u.bait_id = item_id;
                item_name = b->name;
              }
            }

            if (!item_name.empty()) {
              g_db->update_fishing_user(u);
              equip_sessions_.erase(e.id);
              lock.unlock();

              bot.send_message(e.channel_id, "✅ <@" + author_id + "> successfully equipped **" + item_name + "**!");
              
              stoatpp::models::MessagePayload clear_payload;
              clear_payload.content = "✅ Equipped **" + item_name + "**!";
              bot.edit_message(e.channel_id, e.id, clear_payload);
              return;
            }
          }
        }
        lock.unlock();
        bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
        return;
      }
    });

    bot.register_command(
        {"fish",
         {"f", "fishing"},
         "in-depth fishing economy game with rods, baits, effects, prestiges, "
         "pets, global bosses, and odds flexing!",
         "fish "
         "[cast/bucket/sell/shop/buy/equip/bait/upgrade/rod/prestige/profile/"
         "top/boss/pets/event]",
         {"[subcommand]", "[args...]"},
         [this](stoatpp::cluster &bot, const stoatpp::events::Message &msg,
                const std::vector<std::string> &args) {
           if (!g_db) {
             bot.send_message(msg.channel_id,
                              "database connection unavailable.");
             return;
           }

           std::string raw_sub = args.empty() ? "cast" : args[0];
           std::string sub = resolve_subcommand(raw_sub);

           //  1. CAST LINE
           if (sub == "cast") {
             auto u_opt = g_db->get_fishing_user(msg.author.id);
             if (!u_opt)
               return;
             auto u = *u_opt;

             const RodDef *rod = get_rod(u.rod_id);
             const BaitDef *bait = get_bait(u.bait_id);
             const RodEffectDef *effect = get_effect(u.active_rod_effect);
             std::optional<PetDef> pet_def = get_pet_def(u.active_pet_id);

             double pet_luck = pet_def ? pet_def->luck_bonus : 0.0;
             double pet_rarity_boost = pet_def ? pet_def->rarity_boost : 1.0;
             double pet_wt_mult = pet_def ? pet_def->weight_mult : 1.0;
             double pet_val_mult = pet_def ? pet_def->value_mult : 1.0;
             int pet_cd_red = pet_def ? pet_def->cd_reduction : 0;
             double pet_dc_bonus = pet_def ? pet_def->double_catch_bonus : 0.0;

             // Check active bait quantity
             int bait_qty = g_db->get_bait_count(msg.author.id, u.bait_id);
             if (bait_qty <= 0) {
               bot.send_message(msg.channel_id,
                                "❌ You have run out of your active bait (**" +
                                    bait->name +
                                    "**)! Use `.fish bait <bait_id>` or buy "
                                    "more in `.fish shop`.");
               return;
             }

             // Calculate effective cooldown
             int calc_cd = rod->base_cd + bait->cd_delta -
                           (u.rod_cd_level * 1) - effect->cd_reduction -
                           pet_cd_red - u.perm_cd_reduction;
             if (effect->special_flag == "hyperdrive")
               calc_cd -= 5;
             if (rod->id == "chronos_anchor")
               calc_cd = calc_cd / 2;
             if (calc_cd < 2)
               calc_cd = 2; // min 2s cooldown

             int remaining =
                 g_db->get_fish_cooldown_remaining(msg.author.id, calc_cd);
             if (remaining > 0) {
               bot.send_message(msg.channel_id,
                                "⏱️ Please wait **" +
                                    format_cooldown(remaining) +
                                    "** before casting your line again.");
               return;
             }

             // Consume 1 bait
             g_db->consume_bait(msg.author.id, u.bait_id);

             // Update cooldown timer
             g_db->update_last_fish_time(msg.author.id);

             // Calculate luck multipliers
             double prestige_luck = 1.0 + (u.fish_prestige * 0.25);
             double rod_luck_stat = 1.0 + (u.rod_luck_level * 0.15);
             double temp_boost =
                 (u.temp_luck_casts > 0) ? u.temp_luck_boost : 1.0;

             double total_luck = rod->luck_mult * (1.0 + bait->luck_bonus) *
                                 (1.0 + effect->luck_bonus) * prestige_luck *
                                 rod_luck_stat * temp_boost *
                                 (1.0 + pet_luck + u.perm_luck);

             // Decrement temp luck casts if active
             if (u.temp_luck_casts > 0) {
               u.temp_luck_casts--;
               if (u.temp_luck_casts == 0)
                 u.temp_luck_boost = 1.0;
             }

             // Calculate weighted roll for fish selection
             std::random_device rd;
             std::mt19937 gen(rd());

             double total_weight = 0.0;
             std::vector<double> weights;

             for (const auto &fish : fish_pool_) {
               double w = fish.base_odds;
               if (fish.rarity != "Common") {
                 w *= total_luck * bait->rarity_boost *
                      (1.0 + u.rod_rare_level * 0.10) * pet_rarity_boost;
               }
               if (effect->special_flag == "nebula" &&
                   (fish.rarity == "Mythic" || fish.rarity == "Exotic")) {
                 w *= 3.0;
               }
               if (effect->special_flag == "godslayer" &&
                   (fish.rarity == "Divine" || fish.rarity == "Singularity" ||
                    fish.rarity == "Multivertic")) {
                 w *= 5.0;
               }
               if (rod->id == "multivertic_god_rod" &&
                   fish.rarity == "Multivertic") {
                 w *= 5.0;
               }

               weights.push_back(w);
               total_weight += w;
             }

             std::discrete_distribution<> dist(weights.begin(), weights.end());
             int fish_idx = dist(gen);
             FishDef caught = fish_pool_[fish_idx];

             // Compute actual odds decimal for display
             double exact_odds_decimal = weights[fish_idx] / total_weight;

             // Generate weight & value
             std::uniform_real_distribution<double> w_dist(
                 caught.base_weight_min, caught.base_weight_max);
             double weight_kg = w_dist(gen) * pet_wt_mult * u.perm_wt_mult;

             std::uniform_int_distribution<int64_t> v_dist(caught.base_val_min,
                                                           caught.base_val_max);
             int64_t base_val = v_dist(gen);

             // Apply value multipliers from rod, effect, pet, prestige
             double val_mult =
                 effect->value_mult * pet_val_mult * u.perm_val_mult;
             if (rod->id == "poseidons_trident")
               val_mult *= 1.5;
             if (rod->id == "solar_flare_rod")
               val_mult *= 3.0;
             if (rod->id == "multivertic_god_rod")
               val_mult *= 5.0;

             int64_t final_val = static_cast<int64_t>(base_val * val_mult);

             // Check Double Catch chance
             double double_catch_chance = (u.rod_double_level * 0.05) +
                                          effect->double_catch_bonus +
                                          pet_dc_bonus;
             if (rod->id == "abyssal_reaper")
               double_catch_chance += 0.25;

             std::uniform_real_distribution<double> dc_dist(0.0, 1.0);
             bool is_double_catch = (dc_dist(gen) < double_catch_chance);
             int catch_count = is_double_catch ? 2 : 1;

             // Add to DB inventory
             for (int i = 0; i < catch_count; ++i) {
               g_db->add_fish_to_inventory(
                   msg.author.id, caught.id, caught.name, caught.rarity,
                   exact_odds_decimal * 100.0, weight_kg, final_val);
             }

             // Update user fishing stats
             u.total_catches += catch_count;
             u.total_weight += (weight_kg * catch_count);

             if (exact_odds_decimal < u.rarest_catch_odds) {
               u.rarest_catch_odds = exact_odds_decimal;
               u.rarest_catch_name = caught.name + " (" + caught.rarity + ")";
             }

             // Update Boss progress & user contribution
             auto b_daily = get_or_init_boss(g_db, "daily");
             b_daily.req_progress += catch_count;
             if (b_daily.req_progress >= b_daily.req_target) {
               b_daily.req_progress = 0;
               u.event_tokens += 15;
             }
             g_db->save_global_boss(b_daily);
             g_db->add_user_boss_contribution(msg.author.id, "daily",
                                              catch_count);

             auto b_seas = get_or_init_boss(g_db, "seasonal");
             b_seas.req_progress +=
                 static_cast<int64_t>(weight_kg * catch_count);
             if (b_seas.req_progress >= b_seas.req_target) {
               b_seas.req_progress = 0;
               u.event_tokens += 50;
             }
             g_db->save_global_boss(b_seas);
             g_db->add_user_boss_contribution(
                 msg.author.id, "seasonal",
                 static_cast<int64_t>(weight_kg * catch_count));

             g_db->update_fishing_user(u);

             // Trigger Fish Effects if any
             std::string effect_notice;
             if (caught.effect_type == "gold_burst") {
               int64_t bonus_gold = final_val * 2;
               g_db->update_balances(msg.author.id, bonus_gold, 0);
               effect_notice =
                   "\n💥 **Fish Effect [Gold Burst]!** You found an extra **" +
                   std::to_string(bonus_gold) + "** coins hidden in the catch!";
             } else if (caught.effect_type == "adrenaline") {
               g_db->update_last_fish_time(
                   msg.author.id); // resets last cast time
               effect_notice = "\n⚡ **Fish Effect [Adrenaline Rush]!** Cast "
                               "cooldown instantly reset to 0s!";
             } else if (caught.effect_type == "luck_blessing") {
               u.temp_luck_boost = 3.0; // +200% Luck
               u.temp_luck_casts = 3;
               g_db->update_fishing_user(u);
               effect_notice = "\n🍀 **Fish Effect [Luck Blessing]!** Gained "
                               "**+200% Luck** for your next **3** casts!";
             } else if (caught.effect_type == "prestige_token") {
               u.prestige_tokens += 2;
               g_db->update_fishing_user(u);
               effect_notice =
                   "\n🔮 **Fish Effect [Prestige Essence]!** Extracted **+2 "
                   "Prestige Tokens** from the creature!";
             } else if (caught.effect_type == "xp_surge") {
               uint32_t unused_lvl;
               bool unused_up;
               g_db->add_xp(msg.author.id, 250, unused_lvl, unused_up,
                            msg.server_id);
               effect_notice = "\n⭐ **Fish Effect [XP Surge]!** Gained **+250 "
                               "Leveling XP**!";
             }

             // Pet Drop chance on rare catches
             if (caught.rarity == "Mythic" || caught.rarity == "Exotic" ||
                 caught.rarity == "Divine" || caught.rarity == "Singularity" ||
                 caught.rarity == "Multivertic") {
               std::uniform_int_distribution<int> p_roll(1, 100);
               if (p_roll(gen) <= 30) {
                 std::string drop_pet =
                     (caught.rarity == "Multivertic" ||
                      caught.rarity == "Singularity")
                         ? "cosmic_dragon"
                         : (caught.rarity == "Divine" ? "void_kraken"
                                                      : "golden_dolphin");
                 if (!g_db->has_user_pet(msg.author.id, drop_pet)) {
                   g_db->add_user_pet(msg.author.id, drop_pet);
                   auto pdef_drop = get_pet_def(drop_pet);
                   effect_notice +=
                       "\n🐾 **WILD PET DROP!** You caught and tamed a **" +
                       (pdef_drop ? pdef_drop->emoji + " " + pdef_drop->name
                                  : drop_pet) +
                       "**! Check `.fish pets`!";
                 }
               }
             }

             // Format Response Embed
             stoatpp::models::MessagePayload payload;
             payload.content = " ";
             stoatpp::models::Embed embed;
             embed.set_colour(0x2ECC71);
             embed.set_title("🎣 Bronx Fishing — Cast Result");

             std::string odds_formatted = format_odds_and_rarity(
                 caught.rarity, exact_odds_decimal, caught.name);

             std::ostringstream ss_wt;
             ss_wt << std::fixed << std::setprecision(2) << weight_kg;

             std::string desc = "You cast your **" + rod->name + "** with **" +
                                bait->name + "**...\n\n";
             if (is_double_catch) {
               desc += "👯 **DOUBLE CATCH!** You reeled in **2x** fish!\n";
             }
             desc += "Caught: **" + caught.name +
                     "**!\n"
                     "Rarity & Odds: " +
                     odds_formatted +
                     "\n"
                     "Weight: **" +
                     ss_wt.str() +
                     " kg**\n"
                     "Value: **" +
                     std::to_string(final_val * catch_count) + "** coins\n" +
                     effect_notice +
                     "\n\n"
                     "*Bait remaining: **" +
                     std::to_string(bait_qty - 1) + "** | Next cast in **" +
                     std::to_string(calc_cd) + "s***";

             embed.set_description(desc);
             payload.add_embed(embed);
             bot.send_message(msg.channel_id, payload);
             return;
           }

           //  2. INVENTORY / BUCKET
           if (sub == "bucket") {
             auto inv = g_db->get_user_fish_inventory(msg.author.id);
             auto u = g_db->get_fishing_user(msg.author.id);

             stoatpp::models::MessagePayload payload;
             payload.content = " ";
             stoatpp::models::Embed embed;
             embed.set_colour(0x3498DB);
             embed.set_title("🐟 Bronx Fish Bucket — <@" + msg.author.id + ">");

             if (inv.empty()) {
               embed.set_description("Your fish bucket is empty! Cast your "
                                     "line with `.fish` to catch some fish.");
             } else {
               int64_t total_val = 0;
               double total_wt = 0.0;
               std::string desc =
                   "Total Fish: **" + std::to_string(inv.size()) + "**\n\n";

               int display_limit = 10;
               int count = 0;
               for (const auto &fish : inv) {
                 total_val += fish.value;
                 total_wt += fish.weight;

                 if (count < display_limit) {
                   std::ostringstream ss_wt;
                   ss_wt << std::fixed << std::setprecision(1) << fish.weight;
                   desc += "**#" + std::to_string(fish.id) + "** " +
                           fish.fish_name + " (" + fish.rarity + ") — **" +
                           ss_wt.str() + "kg** | **" +
                           std::to_string(fish.value) + "** coins\n";
                   count++;
                 }
               }

               if (inv.size() > display_limit) {
                 desc += "*...and " +
                         std::to_string(inv.size() - display_limit) +
                         " more fish.*\n";
               }

               std::ostringstream ss_total_wt;
               ss_total_wt << std::fixed << std::setprecision(1) << total_wt;
               desc += "\n**Total Bucket Weight:** " + ss_total_wt.str() +
                       " kg\n"
                       "**Total Bucket Value:** **" +
                       std::to_string(total_val) +
                       "** coins\n"
                       "*Use `.fish sell all` or `.fish sell <rarity>` to sell "
                       "your catch!*";

               embed.set_description(desc);
             }

             payload.add_embed(embed);
             bot.send_message(msg.channel_id, payload);
             return;
           }

           //  3. SELL FISH
           if (sub == "sell") {
              auto u_seller_opt = g_db->get_fishing_user(msg.author.id);
              bool has_prestige_fish = false;
              std::string prestige_fish_name = "";

              if (u_seller_opt) {
                std::string req_rarity;
                double req_weight = 0.0;
                get_prestige_fish_requirements(u_seller_opt->fish_prestige, req_rarity, req_weight);

                auto inv = g_db->get_user_fish_inventory(msg.author.id);
                auto qualifying_opt = find_prestige_qualifying_fish(inv, req_rarity, req_weight);
                if (qualifying_opt.has_value()) {
                  if (args.size() < 2 || args[1] == "all" || get_rarity_precedence(qualifying_opt->rarity) == get_rarity_precedence(args[1])) {
                    has_prestige_fish = true;
                    prestige_fish_name = qualifying_opt->fish_name + " (" + qualifying_opt->rarity + ", " + std::to_string((int)qualifying_opt->weight) + "kg)";
                  }
                }
              }

              if (has_prestige_fish) {
                bot.send_message(msg.channel_id,
                                 "⚠️ **Warning**: You have a fish required for your next prestige level in your bucket (**" + prestige_fish_name + "**)! "
                                 "Do you still want to sell it? Type `yes` to confirm and sell all/selected, or `no` to cancel.");

                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [this, &bot, msg, args](std::optional<stoatpp::events::Message> reply) {
                  if (!reply) {
                    bot.send_message(msg.channel_id, "⏱️ Sell confirmation timed out. Action cancelled.");
                    return;
                  }

                  std::string ans = reply->content;
                  std::transform(ans.begin(), ans.end(), ans.begin(), ::tolower);
                  if (ans == "yes" || ans == "y" || ans == "confirm") {
                    execute_sell(bot, msg, args);
                  } else {
                    bot.send_message(msg.channel_id, "❌ Sale cancelled.");
                  }
                });
                return;
              }

              execute_sell(bot, msg, args);
              return;
            }

           //  4. SHOP
           if (sub == "shop") {
             std::string arg = (args.size() > 1) ? args[1] : "";
             open_shop_paginator(bot, msg.channel_id, msg.author.id, arg);
             return;
           }

           //  5. BUY GEAR (Fuzzy item matching)
           if (sub == "buy") {
             if (args.size() < 2) {
               bot.send_message(msg.channel_id,
                                "Usage: `.fish buy <item_id> [quantity]`");
               return;
             }

             std::string raw_item_id = args[1];
             int qty = 1;
             if (args.size() >= 3) {
               try {
                 qty = std::stoi(args[2]);
               } catch (...) {
               }
             }
             if (qty <= 0)
               qty = 1;

             // Collect all shop item candidates for fuzzy match
             std::vector<std::string> item_candidates;
             for (const auto &r : rods_)
               item_candidates.push_back(r.id);
             for (const auto &b : baits_)
               item_candidates.push_back(b.id);
             for (const auto &ef : rod_effects_)
               if (ef.id != "none")
                 item_candidates.push_back(ef.id);

             std::string item_id =
                 fuzzy_match_str(raw_item_id, item_candidates);

             auto u_opt = g_db->get_fishing_user(msg.author.id);
             auto eco_opt = g_db->get_economy_user(msg.author.id);
             if (!u_opt || !eco_opt)
               return;

             auto u = *u_opt;
             auto eco = *eco_opt;

             // Check Rods
             for (const auto &r : rods_) {
               if (r.id == item_id) {
                 if (g_db->has_unlocked_rod(msg.author.id, r.id)) {
                   bot.send_message(msg.channel_id,
                                    "You already own the **" + r.name +
                                        "**! Use `.fish equip " + r.id +
                                        "` to equip it.");
                   return;
                 }

                 if (u.fish_prestige < r.req_prestige) {
                   bot.send_message(msg.channel_id,
                                    "❌ Requires Fishing Prestige **" +
                                        std::to_string(r.req_prestige) + "**.");
                   return;
                 }

                 if (r.cost > 0) {
                   if (eco.wallet < r.cost) {
                     bot.send_message(msg.channel_id,
                                      "❌ You do not have enough coins (" +
                                          std::to_string(r.cost) +
                                          " required).");
                     return;
                   }
                   g_db->update_balances(msg.author.id, -r.cost, 0);
                 } else if (r.token_cost > 0) {
                   if (u.prestige_tokens < r.token_cost) {
                     bot.send_message(
                         msg.channel_id,
                         "❌ You do not have enough Prestige Tokens (" +
                             std::to_string(r.token_cost) + " required).");
                     return;
                   }
                   u.prestige_tokens -= r.token_cost;
                 }

                 g_db->unlock_rod(msg.author.id, r.id);
                 u.rod_id = r.id;
                 g_db->update_fishing_user(u);

                 bot.send_message(msg.channel_id,
                                  "🎉 Purchased and equipped **" + r.name +
                                      "**!");
                 return;
               }
             }

             // Check Baits
             for (const auto &b : baits_) {
               if (b.id == item_id) {
                 if (u.fish_prestige < b.req_prestige) {
                   bot.send_message(msg.channel_id,
                                    "❌ Requires Fishing Prestige **" +
                                        std::to_string(b.req_prestige) + "**.");
                   return;
                 }

                 int64_t total_coins = b.cost * qty;
                 int total_tokens = b.token_cost * qty;
                 int total_baits = b.pack_size * qty;

                 if (total_coins > 0) {
                   if (eco.wallet < total_coins) {
                     bot.send_message(msg.channel_id,
                                      "❌ You do not have enough coins (" +
                                          std::to_string(total_coins) +
                                          " required).");
                     return;
                   }
                   g_db->update_balances(msg.author.id, -total_coins, 0);
                 } else if (total_tokens > 0) {
                   if (u.prestige_tokens < total_tokens) {
                     bot.send_message(
                         msg.channel_id,
                         "❌ You do not have enough Prestige Tokens (" +
                             std::to_string(total_tokens) + " required).");
                     return;
                   }
                   u.prestige_tokens -= total_tokens;
                   g_db->update_fishing_user(u);
                 }

                 g_db->add_bait(msg.author.id, b.id, total_baits);
                 bot.send_message(msg.channel_id,
                                  "🎉 Purchased **" +
                                      std::to_string(total_baits) + "**x **" +
                                      b.name + "**!");
                 return;
               }
             }

             // Check Rod Effects
             for (const auto &ef : rod_effects_) {
               if (ef.id == item_id) {
                 if (ef.cost > 0) {
                   if (eco.wallet < ef.cost) {
                     bot.send_message(msg.channel_id,
                                      "❌ You do not have enough coins (" +
                                          std::to_string(ef.cost) +
                                          " required).");
                     return;
                   }
                   g_db->update_balances(msg.author.id, -ef.cost, 0);
                 } else if (ef.token_cost > 0) {
                   if (u.prestige_tokens < ef.token_cost) {
                     bot.send_message(
                         msg.channel_id,
                         "❌ You do not have enough Prestige Tokens (" +
                             std::to_string(ef.token_cost) + " required).");
                     return;
                   }
                   u.prestige_tokens -= ef.token_cost;
                 }

                 u.active_rod_effect = ef.id;
                 g_db->update_fishing_user(u);

                 bot.send_message(msg.channel_id,
                                  "✨ Enchanted rod with **" + ef.name + "**!");
                 return;
               }
             }

             bot.send_message(msg.channel_id,
                              "❌ Unknown item ID `" + raw_item_id +
                                  "`. Check `.fish shop` for item IDs.");
             return;
           }

           //  6. EQUIP ROD / BAIT
           if (sub == "equip") {
             if (args.size() < 2) {
               open_equip_selector(bot, msg.channel_id, msg.author.id);
               return;
             }
             std::vector<std::string> owned_rods =
                 g_db->get_unlocked_rods(msg.author.id);
             std::string target_rod = fuzzy_match_str(args[1], owned_rods);

             if (!g_db->has_unlocked_rod(msg.author.id, target_rod)) {
               bot.send_message(msg.channel_id,
                                "❌ You have not unlocked this rod yet.");
               return;
             }
             auto u_opt = g_db->get_fishing_user(msg.author.id);
             if (!u_opt)
               return;
             auto u = *u_opt;
             u.rod_id = target_rod;
             g_db->update_fishing_user(u);
             const RodDef *r = get_rod(target_rod);
             bot.send_message(msg.channel_id,
                              "🎣 Equipped **" + r->name + "**!");
             return;
           }

           if (sub == "bait") {
             if (args.size() < 2) {
               auto baits = g_db->get_user_baits(msg.author.id);
               std::string desc = "Your Active Baits:\n";
               for (const auto &b : baits) {
                 const BaitDef *def = get_bait(b.bait_id);
                 desc += "- `" + b.bait_id + "`: **" + def->name + "** (x" +
                         std::to_string(b.quantity) + ")\n";
               }
               bot.send_message(msg.channel_id, desc);
               return;
             }
             std::vector<std::string> bait_ids;
             for (const auto &b : baits_)
               bait_ids.push_back(b.id);
             std::string target_bait = fuzzy_match_str(args[1], bait_ids);

             int qty = g_db->get_bait_count(msg.author.id, target_bait);
             if (qty <= 0) {
               bot.send_message(msg.channel_id,
                                "❌ You do not own any bait of type `" +
                                    target_bait + "`.");
               return;
             }
             auto u_opt = g_db->get_fishing_user(msg.author.id);
             if (!u_opt)
               return;
             auto u = *u_opt;
             u.bait_id = target_bait;
             g_db->update_fishing_user(u);
             const BaitDef *b = get_bait(target_bait);
             bot.send_message(msg.channel_id, "🪱 Set active bait to **" +
                                                  b->name + "** (Remaining: " +
                                                  std::to_string(qty) + ")!");
             return;
           }

           //  7. ROD UPGRADES
           if (sub == "upgrade") {
             auto u_opt = g_db->get_fishing_user(msg.author.id);
             auto eco_opt = g_db->get_economy_user(msg.author.id);
             if (!u_opt || !eco_opt)
               return;
             auto u = *u_opt;
             auto eco = *eco_opt;

             if (args.size() < 2) {
               std::string desc =
                   "🔧 **ROD STAT UPGRADES**\n"
                   "1. `luck` — Level " +
                   std::to_string(u.rod_luck_level) +
                   "/20 (+15% Luck/lvl) | Cost: **" +
                   std::to_string(10000 * (u.rod_luck_level + 1)) +
                   "** coins\n"
                   "2. `cooldown` — Level " +
                   std::to_string(u.rod_cd_level) +
                   "/15 (-1s CD/lvl) | Cost: **" +
                   std::to_string(15000 * (u.rod_cd_level + 1)) +
                   "** coins\n"
                   "3. `double` — Level " +
                   std::to_string(u.rod_double_level) +
                   "/10 (+5% Double Catch/lvl) | Cost: **" +
                   std::to_string(25000 * (u.rod_double_level + 1)) +
                   "** coins\n"
                   "4. `rare` — Level " +
                   std::to_string(u.rod_rare_level) +
                   "/15 (+10% Rare Odds/lvl) | Cost: **" +
                   std::to_string(20000 * (u.rod_rare_level + 1)) +
                   "** coins\n\n"
                   "Use `.fish upgrade <stat>` to upgrade!";
               bot.send_message(msg.channel_id, desc);
               return;
             }

             std::string stat = fuzzy_match_str(
                 args[1], {"luck", "cooldown", "double", "rare"});
             std::transform(stat.begin(), stat.end(), stat.begin(), ::tolower);

             if (stat == "luck") {
               if (u.rod_luck_level >= 20) {
                 bot.send_message(msg.channel_id, "Max level reached.");
                 return;
               }
               int64_t cost = 10000 * (u.rod_luck_level + 1);
               if (eco.wallet < cost) {
                 bot.send_message(msg.channel_id, "Not enough coins.");
                 return;
               }
               g_db->update_balances(msg.author.id, -cost, 0);
               u.rod_luck_level++;
               g_db->update_fishing_user(u);
               bot.send_message(msg.channel_id,
                                "⬆️ Upgraded **Luck** to Level **" +
                                    std::to_string(u.rod_luck_level) + "**!");
             } else if (stat == "cooldown" || stat == "cd") {
               if (u.rod_cd_level >= 15) {
                 bot.send_message(msg.channel_id, "Max level reached.");
                 return;
               }
               int64_t cost = 15000 * (u.rod_cd_level + 1);
               if (eco.wallet < cost) {
                 bot.send_message(msg.channel_id, "Not enough coins.");
                 return;
               }
               g_db->update_balances(msg.author.id, -cost, 0);
               u.rod_cd_level++;
               g_db->update_fishing_user(u);
               bot.send_message(
                   msg.channel_id,
                   "⬆️ Upgraded **Cooldown Reduction** to Level **" +
                       std::to_string(u.rod_cd_level) + "**!");
             } else if (stat == "double") {
               if (u.rod_double_level >= 10) {
                 bot.send_message(msg.channel_id, "Max level reached.");
                 return;
               }
               int64_t cost = 25000 * (u.rod_double_level + 1);
               if (eco.wallet < cost) {
                 bot.send_message(msg.channel_id, "Not enough coins.");
                 return;
               }
               g_db->update_balances(msg.author.id, -cost, 0);
               u.rod_double_level++;
               g_db->update_fishing_user(u);
               bot.send_message(msg.channel_id,
                                "⬆️ Upgraded **Double Catch** to Level **" +
                                    std::to_string(u.rod_double_level) + "**!");
             } else if (stat == "rare") {
               if (u.rod_rare_level >= 15) {
                 bot.send_message(msg.channel_id, "Max level reached.");
                 return;
               }
               int64_t cost = 20000 * (u.rod_rare_level + 1);
               if (eco.wallet < cost) {
                 bot.send_message(msg.channel_id, "Not enough coins.");
                 return;
               }
               g_db->update_balances(msg.author.id, -cost, 0);
               u.rod_rare_level++;
               g_db->update_fishing_user(u);
               bot.send_message(
                   msg.channel_id,
                   "⬆️ Upgraded **Rare Fish Multiplier** to Level **" +
                       std::to_string(u.rod_rare_level) + "**!");
             }
             return;
           }

           //  8. VIEW ROD DETAILS
           if (sub == "rod") {
             auto u_opt = g_db->get_fishing_user(msg.author.id);
             if (!u_opt)
               return;
             auto u = *u_opt;
             const RodDef *r = get_rod(u.rod_id);
             const RodEffectDef *ef = get_effect(u.active_rod_effect);

             stoatpp::models::MessagePayload payload;
             payload.content = " ";
             stoatpp::models::Embed embed;
             embed.set_colour(0x9B59B6);
             embed.set_title("🎣 Equipped Rod — " + r->name);

             std::string desc = "Rod Tier: **" + r->name +
                                "**\n"
                                "Base Luck: **" +
                                std::to_string(r->luck_mult) +
                                "x**\n"
                                "Base Cooldown: **" +
                                std::to_string(r->base_cd) +
                                "s**\n"
                                "Passive: *" +
                                r->passive_effect +
                                "*\n\n"
                                "✨ **Active Enchant:** " +
                                ef->name + " (" + ef->desc +
                                ")\n\n"
                                "🔧 **Upgrade Levels:**\n"
                                "- Luck Level: **" +
                                std::to_string(u.rod_luck_level) + "** (+ " +
                                std::to_string(u.rod_luck_level * 15) +
                                "%)\n"
                                "- Cooldown Level: **" +
                                std::to_string(u.rod_cd_level) + "** (- " +
                                std::to_string(u.rod_cd_level) +
                                "s)\n"
                                "- Double Catch Level: **" +
                                std::to_string(u.rod_double_level) + "** (" +
                                std::to_string(u.rod_double_level * 5) +
                                "% chance)\n"
                                "- Rare Multiplier Level: **" +
                                std::to_string(u.rod_rare_level) + "** (+ " +
                                std::to_string(u.rod_rare_level * 10) + "%)\n";

             embed.set_description(desc);
             payload.add_embed(embed);
             bot.send_message(msg.channel_id, payload);
             return;
           }

           //  9. PRESTIGE SYSTEM
           if (sub == "prestige") {
             auto u_opt = g_db->get_fishing_user(msg.author.id);
             auto eco_opt = g_db->get_economy_user(msg.author.id);
             if (!u_opt || !eco_opt)
               return;
             auto u = *u_opt;
             auto eco = *eco_opt;

             int req_catches = 50 * (u.fish_prestige + 1);
             int64_t req_coins = 100000 * (u.fish_prestige + 1);

             std::string req_rarity;
             double req_weight = 0.0;
             get_prestige_fish_requirements(u.fish_prestige, req_rarity, req_weight);

             auto inv = g_db->get_user_fish_inventory(msg.author.id);
             auto qualifying_opt = find_prestige_qualifying_fish(inv, req_rarity, req_weight);
             bool has_qualifying_fish = qualifying_opt.has_value();

             if (args.size() > 1 && args[1] == "confirm") {
               if (u.total_catches < req_catches || eco.wallet < req_coins || !has_qualifying_fish) {
                 bot.send_message(
                     msg.channel_id,
                     "❌ You do not meet the Fishing Prestige requirements.");
                 return;
               }

               if (g_db->delete_single_fish(msg.author.id, qualifying_opt->id)) {
                 g_db->update_balances(msg.author.id, -req_coins, 0);

                 u.fish_prestige++;
                 u.prestige_tokens += 25 * u.fish_prestige;
                 g_db->update_fishing_user(u);

                 bot.send_message(msg.channel_id,
                                  "🎉 **FISHING PRESTIGE LEVEL " +
                                      std::to_string(u.fish_prestige) +
                                      "!** 🎉\n"
                                      "Sacrificed **1x " + qualifying_opt->fish_name + " (" + qualifying_opt->rarity + ", " + std::to_string((int)qualifying_opt->weight) + "kg)**!\n"
                                      "You gained **+" +
                                      std::to_string(25 * u.fish_prestige) +
                                      " Prestige Tokens**!\n"
                                      "Permanent Luck Multiplier is now **+" +
                                      std::to_string(u.fish_prestige * 25) +
                                      "%** across all rods!");
               } else {
                 bot.send_message(msg.channel_id, "❌ Failed to consume required prestige fish from your bucket.");
               }
               return;
             }

             stoatpp::models::MessagePayload payload;
             payload.content = " ";
             stoatpp::models::Embed embed;
             embed.set_colour(0xE74C3C);
             embed.set_title("🔮 Fishing Prestige Status — Tier " +
                             std::to_string(u.fish_prestige));

             std::ostringstream ss_wt;
             ss_wt << std::fixed << std::setprecision(1) << req_weight;
             std::string fish_req_status = has_qualifying_fish ? 
                 "🟢 **FOUND** (" + qualifying_opt->fish_name + " - " + qualifying_opt->rarity + ", " + std::to_string((int)qualifying_opt->weight) + "kg)" :
                 "🔴 **MISSING** (Need 1x **" + req_rarity + "** or higher fish weighing **" + ss_wt.str() + "kg** or more)";

             std::string desc = "Current Fishing Prestige: **Tier " +
                                std::to_string(u.fish_prestige) +
                                "**\n"
                                "Prestige Tokens: **" +
                                std::to_string(u.prestige_tokens) +
                                "**\n"
                                "Permanent Luck Boost: **+" +
                                std::to_string(u.fish_prestige * 25) +
                                "%**\n\n"
                                "📋 **Requirements for Tier " +
                                std::to_string(u.fish_prestige + 1) +
                                ":**\n"
                                "- Total Catches: **" +
                                std::to_string(u.total_catches) + " / " +
                                std::to_string(req_catches) +
                                "**\n"
                                "- Wallet Coins: **" +
                                std::to_string(eco.wallet) + " / " +
                                std::to_string(req_coins) +
                                "**\n"
                                "- Prestige Sacrifice: " + fish_req_status + "\n\n"
                                "✨ **Rewards:**\n"
                                "- **+" +
                                std::to_string(25 * (u.fish_prestige + 1)) +
                                " Prestige Tokens**\n"
                                "- Permanent **+25% Luck**\n"
                                "- Unlocks Tier " +
                                std::to_string(u.fish_prestige + 1) +
                                " Prestige Rods & Baits in `.fish shop`!\n\n"
                                "Type `.fish prestige confirm` to prestige!";

             embed.set_description(desc);
             payload.add_embed(embed);
             bot.send_message(msg.channel_id, payload);
             return;
           }

           //  10. FLEX / PROFILE
           if (sub == "flex") {
             std::string target_uid = msg.author.id;
             if (args.size() > 1) {
               target_uid = resolve_user_id(args[1]);
             }
             render_profile(bot, msg.channel_id, target_uid);
             return;
           }

           //  11. LEADERBOARD / TOP
           if (sub == "top") {
             auto top_odds = g_db->get_top_fishers_by_odds(10);

             stoatpp::models::MessagePayload payload;
             payload.content = " ";
             stoatpp::models::Embed embed;
             embed.set_colour(0x1ABC9C);
             embed.set_title("🏆 Bronx Fishing — Rarest Catch Leaderboard");

             std::string desc =
                 "Top anglers ranked by rarest catch odds flex:\n\n";
             if (top_odds.empty()) {
               desc += "No fishing records found yet.";
             } else {
               int rank = 1;
               for (const auto &f : top_odds) {
                 int64_t one_in_x = static_cast<int64_t>(
                     std::round(1.0 / f.rarest_catch_odds));
                 desc += "**#" + std::to_string(rank) + "** <@" + f.user_id +
                         "> — " + f.rarest_catch_name + " `(1 in " +
                         std::to_string(one_in_x) + ")`\n";
                 rank++;
               }
             }

             embed.set_description(desc);
             payload.add_embed(embed);
             bot.send_message(msg.channel_id, payload);
             return;
           }

           //  12. GLOBAL FISH BOSS
           if (sub == "boss") {
             std::string arg = (args.size() > 1) ? args[1] : "";
             open_boss_paginator(bot, msg.channel_id, msg.author.id, arg);
             return;
           }

           //  13. PET COMPANIONS
           if (sub == "pets") {
             if (args.size() >= 2 && (args[1] == "equip" || args[1] == "use")) {
               if (args.size() < 3) {
                 bot.send_message(msg.channel_id,
                                  "❌ Usage: `.fish pets equip <pet_id>`");
                 return;
               }
               auto u_opt = g_db->get_fishing_user(msg.author.id);
               if (!u_opt)
                 return;
               auto u = *u_opt;
               if (!g_db->has_user_pet(msg.author.id, args[2])) {
                 bot.send_message(msg.channel_id,
                                  "❌ You do not own that pet!");
                 return;
               }
               u.active_pet_id = args[2];
               g_db->update_fishing_user(u);
               bot.send_message(msg.channel_id,
                                "✅ Equipped pet companion `" + args[2] + "`!");
               return;
             } else if (args.size() >= 2 && args[1] == "unequip") {
               auto u_opt = g_db->get_fishing_user(msg.author.id);
               if (!u_opt)
                 return;
               auto u = *u_opt;
               u.active_pet_id = "none";
               g_db->update_fishing_user(u);
               bot.send_message(msg.channel_id, "✅ Unequipped pet companion.");
               return;
             }
             auto payload = render_pets_page(msg.author.id);
             bot.send_message(msg.channel_id, payload);
             return;
           }

           //  14. EVENT SHOP & ECONOMY
           if (sub == "event") {
             if (args.size() >= 3 &&
                 (args[1] == "buy" || args[1] == "purchase")) {
               std::string item_target = args[2];
               auto u_opt = g_db->get_fishing_user(msg.author.id);
               if (!u_opt)
                 return;
               auto u = *u_opt;

               auto pdef = get_pet_def(item_target);
               if (pdef) {
                 if (u.event_tokens < pdef->event_token_cost) {
                   bot.send_message(msg.channel_id,
                                    "❌ Requires " +
                                        std::to_string(pdef->event_token_cost) +
                                        " Event Tokens 🎫.");
                   return;
                 }
                 if (g_db->has_user_pet(msg.author.id, item_target)) {
                   bot.send_message(msg.channel_id,
                                    "❌ You already own this pet!");
                   return;
                 }
                 u.event_tokens -= pdef->event_token_cost;
                 g_db->update_fishing_user(u);
                 g_db->add_user_pet(msg.author.id, item_target);
                 bot.send_message(msg.channel_id, "🎉 Purchased pet **" +
                                                      pdef->emoji + " " +
                                                      pdef->name + "**!");
                 return;
               }
               if (item_target == "perm_luck") {
                 if (u.event_tokens < 100) {
                   bot.send_message(msg.channel_id,
                                    "❌ Costs 100 Event Tokens 🎫.");
                   return;
                 }
                 u.event_tokens -= 100;
                 u.perm_luck += 0.5;
                 g_db->update_fishing_user(u);
                 bot.send_message(
                     msg.channel_id,
                     "🎉 Purchased **Permanent +0.5x Base Luck**!");
                 return;
               } else if (item_target == "perm_value") {
                 if (u.event_tokens < 150) {
                   bot.send_message(msg.channel_id,
                                    "❌ Costs 150 Event Tokens 🎫.");
                   return;
                 }
                 u.event_tokens -= 150;
                 u.perm_val_mult *= 1.5;
                 g_db->update_fishing_user(u);
                 bot.send_message(
                     msg.channel_id,
                     "🎉 Purchased **Permanent 1.5x Sell Value Multiplier**!");
                 return;
               } else if (item_target == "perm_weight") {
                 if (u.event_tokens < 150) {
                   bot.send_message(msg.channel_id,
                                    "❌ Costs 150 Event Tokens 🎫.");
                   return;
                 }
                 u.event_tokens -= 150;
                 u.perm_wt_mult *= 1.5;
                 g_db->update_fishing_user(u);
                 bot.send_message(msg.channel_id,
                                  "🎉 Purchased **Permanent 1.5x Catch Weight "
                                  "Multiplier**!");
                 return;
               } else if (item_target == "perm_cd") {
                 if (u.event_tokens < 200) {
                   bot.send_message(msg.channel_id,
                                    "❌ Costs 200 Event Tokens 🎫.");
                   return;
                 }
                 u.event_tokens -= 200;
                 u.perm_cd_reduction += 2;
                 g_db->update_fishing_user(u);
                 bot.send_message(
                     msg.channel_id,
                     "🎉 Purchased **Permanent -2s Cooldown Reduction**!");
                 return;
               } else if (item_target == "summer_sunfish") {
                 if (u.event_tokens < 30) {
                   bot.send_message(msg.channel_id,
                                    "❌ Costs 30 Event Tokens 🎫.");
                   return;
                 }
                 u.event_tokens -= 30;
                 g_db->update_fishing_user(u);
                 g_db->add_fish_to_inventory(msg.author.id, "summer_sunfish",
                                             "Summer Sun-Fish", "Epic", 0.05,
                                             125.0, 2500000);
                 bot.send_message(
                     msg.channel_id,
                     "🎉 Purchased **Summer Sun-Fish** ($2,500,000 value)!");
                 return;
               }
               bot.send_message(msg.channel_id,
                                "❌ Unknown event item `" + item_target + "`.");
               return;
             }
             auto payload = render_event_shop_page(msg.author.id);
             bot.send_message(msg.channel_id, payload);
             return;
           }
            //  15. P2P GIFTING / TRANSFER
            if (sub == "gift") {
              if (args.size() < 4) {
                bot.send_message(msg.channel_id,
                                 "❌ Usage:\n"
                                 "• `.fish gift <@user> fish <fish_id>`\n"
                                 "• `.fish gift <@user> bait <bait_id> <quantity>`\n"
                                 "• `.fish gift <@user> tokens <quantity>`");
                return;
              }

              std::string target_query = args[1];
              std::string gift_type = args[2];
              std::transform(gift_type.begin(), gift_type.end(), gift_type.begin(), ::tolower);

              helpers::resolve_user(bot, msg.server_id, target_query, [this, &bot, msg, gift_type, args](stoatpp::models::User target_user, stoatpp::models::Member, bool success) {
                if (!success) {
                  bot.send_message(msg.channel_id, "❌ Could not resolve recipient user.");
                  return;
                }

                std::string recipient_id = target_user.id;
                if (recipient_id == msg.author.id) {
                  bot.send_message(msg.channel_id, "❌ You cannot gift items to yourself!");
                  return;
                }

                if (gift_type == "fish") {
                  uint64_t fish_id = 0;
                  try {
                    fish_id = std::stoull(args[3]);
                  } catch (...) {
                    bot.send_message(msg.channel_id, "❌ Invalid fish ID. Specify the numeric ID (e.g. from `.fish bucket`).");
                    return;
                  }

                  // Verify sender owns this fish
                  auto inv = g_db->get_user_fish_inventory(msg.author.id);
                  bool owns_fish = false;
                  std::string fish_desc = "";
                  for (const auto &f : inv) {
                    if (f.id == fish_id) {
                      owns_fish = true;
                      fish_desc = f.fish_name + " (" + f.rarity + ", " + std::to_string((int)f.weight) + "kg)";
                      break;
                    }
                  }

                  if (!owns_fish) {
                    bot.send_message(msg.channel_id, "❌ You do not own a fish with ID #" + std::to_string(fish_id) + " in your bucket.");
                    return;
                  }

                  if (g_db->transfer_fish(fish_id, msg.author.id, recipient_id)) {
                    bot.send_message(msg.channel_id, "🎁 Successfully gifted **" + fish_desc + "** to <@" + recipient_id + ">!");
                  } else {
                    bot.send_message(msg.channel_id, "❌ Failed to transfer fish.");
                  }
                  return;
                }

                if (gift_type == "bait") {
                  std::string bait_id = args[3];
                  std::vector<std::string> bait_ids;
                  for (const auto &b : baits_) bait_ids.push_back(b.id);
                  std::string target_bait = fuzzy_match_str(bait_id, bait_ids);

                  int quantity = 1;
                  if (args.size() >= 5) {
                    try {
                      quantity = std::stoi(args[4]);
                    } catch (...) {}
                  }
                  if (quantity <= 0) {
                    bot.send_message(msg.channel_id, "❌ Quantity must be a positive number.");
                    return;
                  }

                  int owned_qty = g_db->get_bait_count(msg.author.id, target_bait);
                  if (owned_qty < quantity) {
                    bot.send_message(msg.channel_id, "❌ You only have **" + std::to_string(owned_qty) + "** packs of `" + target_bait + "` bait.");
                    return;
                  }

                  const BaitDef *def = get_bait(target_bait);
                  if (g_db->add_bait(msg.author.id, target_bait, -quantity) &&
                      g_db->add_bait(recipient_id, target_bait, quantity)) {
                    bot.send_message(msg.channel_id, "🎁 Successfully gifted **" + std::to_string(quantity) + "x " + def->name + "** packs to <@" + recipient_id + ">!");
                  } else {
                    bot.send_message(msg.channel_id, "❌ Failed to transfer bait.");
                  }
                  return;
                }

                if (gift_type == "tokens" || gift_type == "token") {
                  int quantity = 0;
                  try {
                    quantity = std::stoi(args[3]);
                  } catch (...) {
                    bot.send_message(msg.channel_id, "❌ Invalid token quantity.");
                    return;
                  }
                  if (quantity <= 0) {
                    bot.send_message(msg.channel_id, "❌ Quantity must be a positive number.");
                    return;
                  }

                  auto sender_opt = g_db->get_fishing_user(msg.author.id);
                  if (!sender_opt || sender_opt->prestige_tokens < quantity) {
                    int owned = sender_opt ? sender_opt->prestige_tokens : 0;
                    bot.send_message(msg.channel_id, "❌ You only have **" + std::to_string(owned) + "** Prestige Tokens.");
                    return;
                  }

                  auto recipient_opt = g_db->get_fishing_user(recipient_id);
                  if (!recipient_opt) {
                    g_db->ensure_fishing_user(recipient_id);
                    recipient_opt = g_db->get_fishing_user(recipient_id);
                  }

                  if (recipient_opt) {
                    sender_opt->prestige_tokens -= quantity;
                    recipient_opt->prestige_tokens += quantity;

                    g_db->update_fishing_user(*sender_opt);
                    g_db->update_fishing_user(*recipient_opt);

                    bot.send_message(msg.channel_id, "🎁 Successfully gifted **" + std::to_string(quantity) + " Prestige Tokens** 🪙 to <@" + recipient_id + ">!");
                  } else {
                    bot.send_message(msg.channel_id, "❌ Failed to resolve recipient's fishing profile.");
                  }
                  return;
                }

                bot.send_message(msg.channel_id, "❌ Unknown gift type. Choose from `fish`, `bait`, or `tokens`.");
              });
              return;
            }

            bot.send_message(msg.channel_id,
                             "Unknown subcommand. Usage: `.fish "
                             "[cast/bucket/sell/shop/buy/equip/bait/upgrade/rod/prestige/profile/top/boss/pets/event/gift]`");
          },
          "Economy",
          0,
          0,
          {"cast", "bucket", "sell", "shop", "buy", "equip", "bait", "upgrade",
           "rod", "prestige", "flex", "top", "boss", "pets", "event", "gift"}});
  }
};

} // namespace bronx::cogs
