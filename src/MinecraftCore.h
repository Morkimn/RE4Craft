#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace mashup {
using Json = nlohmann::json;
using Inventory = std::map<std::string, int>;
using Reader = std::function<std::string(const std::string&)>;
struct Recipe {
    std::string id, output;
    int count = 0, width = 0, height = 0;
    std::vector<std::string> pattern;
    std::vector<std::vector<std::string>> ingredients;
};
struct Content {
    std::vector<Recipe> recipes;
    void load(const Reader& read);
};
int itemCount(const Inventory& inv, const std::string& id);
int stackSize(const std::string& id);
int usedSlots(const Inventory& inv);
bool craft(Inventory& inv, const Recipe& recipe, std::string& error);
struct Food {
    int level = 20, timer = 0;
    float saturation = 5, exhaustion = 0;
    void exhaust(float amount) { exhaustion = std::clamp(exhaustion + amount, 0.f, 40.f); }
    bool eatBread();
    // Returns change in Minecraft health units. Caller maps it to the host's maximum HP.
    float tick(float hp, int difficulty = 2, bool naturalRegeneration = true);
};
Json saveState(const Inventory& inv, const Food& food);
void loadState(const Json& json, Inventory& inv, Food& food);
}
