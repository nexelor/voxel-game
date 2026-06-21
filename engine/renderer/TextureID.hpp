#pragma once
 
#include <string>
#include <functional>
#include <stdexcept>
 
// ─────────────────────────────────────────────
//  TextureID
//
//  A namespaced resource location, e.g. "minecraft:stone"
//  or "myaddon:glowing_ore". Mirrors Minecraft's own
//  resource-location convention so the texture source
//  (content/assets/textures/<namespace>/) is always
//  explicit and unambiguous at the call site.
//
//  Construction:
//      TextureID id("minecraft:stone");
//      TextureID id2("minecraft", "dirt");
//
//  Used as:
//    - A key into TextureAtlas's lookup table (after Build()).
//    - The value stored per-face in BlockDef.
//
//  File location on disk for a given ID:
//      content/assets/textures/<ns>/<name>.png
// ─────────────────────────────────────────────

struct TextureID {
    std::string ns; // e.g. "minecraft"
    std::string name; // e.g. "stone"

    TextureID() = default;

    TextureID(std::string ns_, std::string name_)
        : ns(std::move(ns_)), name(std::move(name_)) {}

    // Parses "namespace:name". Throws if the ':' separator is missing -
    // resource locations are always fully explicit in this engine, so a
    // malformed ID is a content/registration bug we want to catch loudly
    // and immediately rather than silently falling back to a default.
    explicit TextureID(const std::string& combined) {
        const auto sep = combined.find(':');
        if (sep == std::string::npos) {
            throw std::runtime_error(
                "TextureID: missing ':' in resource location \"" + combined +
                "\" (expected \"namespace:name\")");
        }
        ns = combined.substr(0, sep);
        name = combined.substr(sep + 1);
 
        if (ns.empty() || name.empty()) {
            throw std::runtime_error(
                "TextureID: empty namespace or name in \"" + combined + "\"");
        }
    }

    std::string ToString() const { return ns + ":" + name; }
 
    // Relative path under the textures root, e.g. "minecraft/stone.png"
    std::string RelativePath() const { return ns + "/" + name + ".png"; }
 
    bool operator==(const TextureID& other) const {
        return ns == other.ns && name == other.name;
    }
    bool operator!=(const TextureID& other) const { return !(*this == other); }
};

// Hash so TextureID can be used directly as an unordered_map key.
struct TextureIDHash {
    std::size_t operator()(const TextureID& id) const noexcept {
        const std::size_t h1 = std::hash<std::string>{}(id.ns);
        const std::size_t h2 = std::hash<std::string>{}(id.name);
        // Same Fibonacci-mixing approach as IVec3Hash (ChunkManager.hpp) —
        // keeps consistency across the codebase for hash combination.
        std::size_t h = 0;
        h ^= h1 + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= h2 + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};