#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

using texIdType = uint8_t;

enum TexMap: texIdType {
    side_dirt = 0,
    grass_cover,
    texture_count
};

inline constexpr std::size_t TEXTURE_COUNT = static_cast<std::size_t>(TexMap::texture_count);

inline constexpr std::size_t textureIndex(TexMap id) {
    return static_cast<std::size_t>(id);
}

inline constexpr std::array<const char*, TEXTURE_COUNT> TEXTURE_FILES = [] {
    std::array<const char*, TEXTURE_COUNT> files{};
    files[textureIndex(TexMap::side_dirt)] = "block.png";
    files[textureIndex(TexMap::grass_cover)] = "surface.png";
    return files;
}();

inline constexpr bool allTextureFilesDefined() {
    for (const char* fileName : TEXTURE_FILES) {
        if (fileName == nullptr) {
            return false;
        }
    }

    return true;
}

static_assert(allTextureFilesDefined(), "Every TexMap entry must have a texture file.");

inline constexpr bool isValidTextureId(TexMap id) {
    return textureIndex(id) < TEXTURE_COUNT;
}

inline constexpr const char* textureFileName(TexMap id) {
    return TEXTURE_FILES[textureIndex(id)];
}
