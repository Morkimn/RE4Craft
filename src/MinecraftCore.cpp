#include "MinecraftCore.h"

namespace mashup {
static std::vector<std::string> resolve(const std::string& value, const Reader& read,
                                      std::set<std::string> trail = {}) {
    if (value.empty()) throw std::runtime_error("Empty ingredient");
    if (value[0] != '#') return {value};
    if (!trail.insert(value).second) throw std::runtime_error("Cyclic item tag: " + value);
    auto id = value.substr(1);
    auto colon = id.find(':');
    if (colon == std::string::npos) throw std::runtime_error("Invalid tag namespace");
    auto j = Json::parse(read("data/" + id.substr(0, colon) + "/tags/item/" + id.substr(colon+1) + ".json"));
    std::vector<std::string> values;
    for (auto& v : j.at("values")) {
        std::string name = v.is_string() ? v.get<std::string>() : v.at("id").get<std::string>();
        auto nested = resolve(name, read, trail);
        values.insert(values.end(), nested.begin(), nested.end());
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    if (values.empty()) throw std::runtime_error("Empty item tag: " + value);
    return values;
}
void Content::load(const Reader& read) {
    std::vector<Recipe> loaded;
    for (const char* name : {"oak_planks", "stick", "crafting_table", "bread", "wooden_pickaxe"}) {
        Recipe r;
        r.id = std::string("minecraft:") + name;
        auto j = Json::parse(read(std::string("data/minecraft/recipe/") + name + ".json"));
        r.output = j.at("result").at("id");
        r.count = j.at("result").value("count", 1);
        if (r.count < 1 || r.count > 64) throw std::runtime_error("Invalid recipe count");
        if (j.at("type") == "minecraft:crafting_shaped") {
            r.pattern = j.at("pattern").get<std::vector<std::string>>();
            r.height = static_cast<int>(r.pattern.size());
            for (auto& row : r.pattern) {
                r.width = std::max(r.width, static_cast<int>(row.size()));
                for (char c : row) if (c != ' ')
                    r.ingredients.push_back(resolve(j.at("key").at(std::string(1,c)), read));
            }
        } else if (j.at("type") == "minecraft:crafting_shapeless") {
            for (auto& v : j.at("ingredients")) r.ingredients.push_back(resolve(v, read));
            r.width = r.ingredients.size() > 4 ? 3 : 2;
            r.height = r.width;
        } else throw std::runtime_error("Unsupported recipe type");
        if (r.ingredients.empty() || r.ingredients.size() > 9 || r.width > 3 || r.height > 3)
            throw std::runtime_error("Invalid recipe dimensions");
        loaded.push_back(std::move(r));
    }
    recipes = std::move(loaded);
}
int itemCount(const Inventory& inv, const std::string& id) {
    auto it = inv.find(id); return it == inv.end() ? 0 : it->second;
}
int stackSize(const std::string& id) { return id == "minecraft:wooden_pickaxe" ? 1 : 64; }
int usedSlots(const Inventory& inv) {
    int total = 0;
    for (auto& p : inv) { if (p.second < 0 || p.second > 2304) return 100000; total += (p.second + stackSize(p.first)-1)/stackSize(p.first); }
    return total;
}
bool craft(Inventory& inv, const Recipe& r, std::string& error) {
    if ((r.width > 2 || r.height > 2) && itemCount(inv,"minecraft:crafting_table") == 0) {
        error = "Requires a crafting table (3 x 3)"; return false;
    }
    Inventory next = inv;
    // Backtracking prevents a broad tag from taking the only ingredient for a later narrow tag.
    std::function<bool(size_t)> take = [&](size_t i) {
        if (i == r.ingredients.size()) return true;
        for (auto& id : r.ingredients[i]) if (itemCount(next,id) > 0) {
            --next[id];
            if (take(i+1)) return true;
            ++next[id];
        }
        return false;
    };
    if (!take(0)) { error = "Not enough ingredients"; return false; }
    next[r.output] += r.count;
    if (usedSlots(next) > 36) { error = "Minecraft inventory full (36 slots)"; return false; }
    for (auto it = next.begin(); it != next.end();) it = it->second == 0 ? next.erase(it) : std::next(it);
    inv = std::move(next); error.clear(); return true;
}
bool Food::eatBread() {
    if (level >= 20) return false;
    level = std::min(level+5,20); saturation = std::min(saturation+6.f,static_cast<float>(level)); return true;
}
float Food::tick(float hp, int difficulty, bool regen) {
    if (exhaustion > 4.f) {
        exhaustion -= 4.f;
        if (saturation > 0) saturation = std::max(saturation-1.f,0.f);
        else if (difficulty != 0) level = std::max(level-1,0);
    }
    if (regen && saturation > 0 && level >= 20 && hp < 20.f) {
        if (++timer >= 10) { float f = std::min(saturation,6.f); exhaust(f); timer=0; return std::min(f/6.f,20.f-hp); }
    } else if (regen && level >= 18 && hp < 20.f) {
        if (++timer >= 80) { exhaust(6.f); timer=0; return std::min(1.f,20.f-hp); }
    } else if (level <= 0) {
        if (++timer >= 80) {
            timer=0;
            if (hp > 10 || difficulty==3 || (hp>1 && difficulty==2)) return -std::min(1.f,hp);
        }
    } else timer = 0;
    return 0;
}
Json saveState(const Inventory& inv, const Food& f) {
    return {{"schema",1},{"profile","lab"},{"minecraft","26.3"},{"inventory",inv},
            {"food",{{"level",f.level},{"saturation",f.saturation},{"exhaustion",f.exhaustion},{"timer",f.timer}}}};
}
void loadState(const Json& j, Inventory& inv, Food& food) {
    if (j.at("schema") != 1 || j.at("profile") != "lab" || j.at("minecraft") != "26.3")
        throw std::runtime_error("Incompatible mashup state");
    auto next = j.at("inventory").get<Inventory>();
    if (usedSlots(next) > 36) throw std::runtime_error("Invalid inventory size");
    for (auto& p:next) if (p.first.rfind("minecraft:",0)!=0) throw std::runtime_error("Invalid item namespace");
    auto& f = j.at("food");
    Food loaded; loaded.level=f.at("level"); loaded.saturation=f.at("saturation"); loaded.exhaustion=f.at("exhaustion"); loaded.timer=f.at("timer");
    if (loaded.level<0 || loaded.level>20 || !std::isfinite(loaded.saturation) || loaded.saturation<0 || loaded.saturation>loaded.level ||
        !std::isfinite(loaded.exhaustion) || loaded.exhaustion<0 || loaded.exhaustion>40 || loaded.timer<0 || loaded.timer>80)
        throw std::runtime_error("Invalid food data");
    inv=std::move(next); food=loaded;
}
}
