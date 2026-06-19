#pragma once

#include "chunk_streaming.h"

#include <array>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#define REGION_FILE_EXTENSION "region"
#define REGION_FILE_PREAMBLE "r"
#define REGION_FILE_SEPARATOR "."
#define REGION_FILE_SEPARATOR_CHAR '.'

namespace torq {

class RegionStoreException final : public std::runtime_error {
public:
    RegionStoreException(ChunkErrorCode code, const char* message);

    [[nodiscard]] ChunkErrorCode code() const noexcept;

private:
    ChunkErrorCode code_;
};

struct RegionSlot {
    RegionCoord coord{};
    std::array<HeaderEntry, CHUNKS_PER_REGION_SIDE * CHUNKS_PER_REGION_SIDE> header{};
    bool header_loaded{false};
    bool file_exists{false};
    std::mutex file_mutex;
};

class RegionStore {
public:
    explicit RegionStore(std::filesystem::path region_dir);

    [[nodiscard]] std::unique_ptr<ChunkData> loadChunk(ChunkCoord coord);
    void persistChunk(ChunkCoord coord, const ChunkData& data);

    [[nodiscard]] std::filesystem::path getRegionFilePath(RegionCoord coord) const;

private:
    RegionSlot& getOrCreateRegionSlot(RegionCoord coord);
    void ensureHeaderLoaded(RegionSlot& slot);
    void generateAndWriteRegion(RegionSlot& slot);

    std::filesystem::path region_dir_;
    std::mutex regions_mutex_;
    std::unordered_map<RegionCoord, std::unique_ptr<RegionSlot>, RegionCoordHash> regions_;
};

} // namespace torq
