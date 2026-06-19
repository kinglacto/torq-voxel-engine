#include "region_store.h"

#include "worldgen.hpp"

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>

namespace torq {
namespace fs = std::filesystem;

namespace {

constexpr std::size_t REGION_CHUNK_COUNT =
    CHUNKS_PER_REGION_SIDE * CHUNKS_PER_REGION_SIDE;
constexpr std::size_t REGION_HEADER_BYTES =
    REGION_CHUNK_COUNT * sizeof(HeaderEntry);
constexpr std::size_t CHUNK_PAYLOAD_BYTES =
    BLOCKS_PER_CHUNK * sizeof(BlockData);

[[nodiscard]] std::size_t chunkIndex(const int local_x, const int local_z) noexcept {
    assert(local_x >= 0 && local_x < CHUNKS_PER_REGION_SIDE);
    assert(local_z >= 0 && local_z < CHUNKS_PER_REGION_SIDE);
    return static_cast<std::size_t>(
        local_x * CHUNKS_PER_REGION_SIDE + local_z
    );
}

[[nodiscard]] std::size_t blockIndex(const int x, const int y, const int z) noexcept {
    assert(x >= 0 && x < BLOCK_X_SIZE);
    assert(y >= 0 && y < BLOCK_Y_SIZE);
    assert(z >= 0 && z < BLOCK_Z_SIZE);
    return static_cast<std::size_t>((y * BLOCK_X_SIZE + x) * BLOCK_Z_SIZE + z);
}

[[nodiscard]] std::uint64_t streamSize(std::ifstream& file) {
    file.seekg(0, std::ios::end);
    const std::streampos end = file.tellg();
    if (end < 0) {
        throw RegionStoreException(FILE_ERROR, "failed to determine region file size");
    }
    file.seekg(0, std::ios::beg);
    return static_cast<std::uint64_t>(end);
}

void throwIfFileBad(const std::ios& file, const ChunkErrorCode code, const char* message) {
    if (!file) {
        throw RegionStoreException(code, message);
    }
}

[[nodiscard]] std::vector<Bytef> compressChunkBlocks(const ChunkData& chunk) {
    uLongf compressed_size = compressBound(CHUNK_PAYLOAD_BYTES);
    std::vector<Bytef> compressed(compressed_size);

    const int zret = compress(
        compressed.data(),
        &compressed_size,
        reinterpret_cast<const Bytef*>(chunk.blocks.data()),
        static_cast<uLong>(CHUNK_PAYLOAD_BYTES)
    );

    if (zret != Z_OK) {
        throw RegionStoreException(COMPRESSION_ERROR, "failed to compress chunk payload");
    }

    compressed.resize(static_cast<std::size_t>(compressed_size));
    return compressed;
}

void decompressChunkBlocks(std::span<const Bytef> compressed, ChunkData& out_chunk) {
    uLongf decompressed_size = CHUNK_PAYLOAD_BYTES;
    const int zret = uncompress(
        reinterpret_cast<Bytef*>(out_chunk.blocks.data()),
        &decompressed_size,
        compressed.data(),
        static_cast<uLong>(compressed.size())
    );

    if (zret != Z_OK) {
        throw RegionStoreException(DECOMPRESSION_ERROR, "failed to decompress chunk payload");
    }

    if (decompressed_size != CHUNK_PAYLOAD_BYTES) {
        throw RegionStoreException(CHUNK_CORRUPTED, "decompressed chunk payload has wrong size");
    }
}

void generateRegionData(const RegionCoord coord, RegionData& region) {
    region.coord = coord;

    const long x_offset = static_cast<long>(coord.x) * BLOCKS_PER_REGION_SIDE;
    const long z_offset = static_cast<long>(coord.z) * BLOCKS_PER_REGION_SIDE;

    BlockData solid{};
    solid.id = BlockMap::grass;
    BlockData air{};
    air.id = BlockMap::air;

    for (int block_x = 0; block_x < BLOCKS_PER_REGION_SIDE; block_x++) {
        for (int block_z = 0; block_z < BLOCKS_PER_REGION_SIDE; block_z++) {
            const float height =
                WorldGen::getHeight(x_offset + block_x, z_offset + block_z);
            const int chunk_x = block_x / BLOCK_X_SIZE;
            const int chunk_z = block_z / BLOCK_Z_SIZE;
            const int local_x = block_x % BLOCK_X_SIZE;
            const int local_z = block_z % BLOCK_Z_SIZE;
            const int surface_height = static_cast<int>(height * BLOCK_Y_SIZE);

            ChunkData& chunk = region.chunks[chunkIndex(chunk_x, chunk_z)];
            for (int y = 0; y < BLOCK_Y_SIZE; y++) {
                chunk.blocks[blockIndex(local_x, y, local_z)] =
                    y > surface_height ? air : solid;
            }
        }
    }
}

} // namespace

RegionStoreException::RegionStoreException(const ChunkErrorCode code,
                                           const char* message)
    : std::runtime_error{message},
      code_{code} {
}

ChunkErrorCode RegionStoreException::code() const noexcept {
    return code_;
}

RegionStore::RegionStore(fs::path region_dir)
    : region_dir_{std::move(region_dir)} {
    std::error_code ec;
    fs::create_directories(region_dir_, ec);
    if (ec) {
        throw RegionStoreException(DIRECTORY_CREATION_FAILED,
                                   "failed to create region directory");
    }
}

std::unique_ptr<ChunkData> RegionStore::loadChunk(const ChunkCoord coord) {
    const RegionCoord region_coord = regionCoordFromChunk(coord);
    const int local_x = regionLocalChunkX(coord);
    const int local_z = regionLocalChunkZ(coord);
    const std::size_t header_index = chunkIndex(local_x, local_z);

    RegionSlot& slot = getOrCreateRegionSlot(region_coord);
    std::lock_guard<std::mutex> file_lock{slot.file_mutex};
    ensureHeaderLoaded(slot);

    const HeaderEntry entry = slot.header[header_index];
    if (entry.offset == 0 || entry.length == 0) {
        throw RegionStoreException(CHUNK_EMPTY, "chunk header entry is empty");
    }
    if (entry.offset < REGION_HEADER_BYTES) {
        throw RegionStoreException(CHUNK_CORRUPTED, "chunk payload starts inside region header");
    }

    std::ifstream file{getRegionFilePath(region_coord), std::ios::binary};
    throwIfFileBad(file, FILE_ERROR, "failed to open region file for chunk load");

    const std::uint64_t file_size = streamSize(file);
    const std::uint64_t payload_end =
        static_cast<std::uint64_t>(entry.offset) + entry.length;
    if (payload_end > file_size) {
        throw RegionStoreException(CHUNK_CORRUPTED, "chunk payload extends past region file");
    }

    std::vector<Bytef> compressed(entry.length);
    file.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
    file.read(reinterpret_cast<char*>(compressed.data()),
              static_cast<std::streamsize>(compressed.size()));
    if (file.gcount() != static_cast<std::streamsize>(compressed.size())) {
        throw RegionStoreException(FILE_ERROR, "failed to read complete chunk payload");
    }

    auto chunk = std::make_unique<ChunkData>();
    decompressChunkBlocks(compressed, *chunk);
    return chunk;
}

void RegionStore::persistChunk(const ChunkCoord coord, const ChunkData& data) {
    const RegionCoord region_coord = regionCoordFromChunk(coord);
    const int local_x = regionLocalChunkX(coord);
    const int local_z = regionLocalChunkZ(coord);
    const std::size_t header_index = chunkIndex(local_x, local_z);

    RegionSlot& slot = getOrCreateRegionSlot(region_coord);
    std::lock_guard<std::mutex> file_lock{slot.file_mutex};
    ensureHeaderLoaded(slot);

    std::vector<Bytef> compressed = compressChunkBlocks(data);

    std::fstream file{getRegionFilePath(region_coord),
                      std::ios::in | std::ios::out | std::ios::binary};
    throwIfFileBad(file, FILE_ERROR, "failed to open region file for chunk persist");

    file.seekp(0, std::ios::end);
    const std::streampos end = file.tellp();
    if (end < 0) {
        throw RegionStoreException(FILE_ERROR, "failed to seek region file for append");
    }

    if (static_cast<std::uint64_t>(end) >
        std::numeric_limits<chunk_header_offset_type>::max()) {
        throw RegionStoreException(FILE_ERROR, "region file offset exceeds header capacity");
    }
    if (compressed.size() >
        std::numeric_limits<chunk_header_length_type>::max()) {
        throw RegionStoreException(COMPRESSION_ERROR,
                                   "compressed chunk exceeds header length capacity");
    }

    const HeaderEntry new_entry{
        static_cast<chunk_header_offset_type>(end),
        static_cast<chunk_header_length_type>(compressed.size())
    };

    file.write(reinterpret_cast<const char*>(compressed.data()),
               static_cast<std::streamsize>(compressed.size()));
    throwIfFileBad(file, FILE_ERROR, "failed to append chunk payload");

    std::array<HeaderEntry, REGION_CHUNK_COUNT> header = slot.header;
    header[header_index] = new_entry;

    file.seekp(0, std::ios::beg);
    file.write(reinterpret_cast<const char*>(header.data()),
               static_cast<std::streamsize>(REGION_HEADER_BYTES));
    throwIfFileBad(file, FILE_ERROR, "failed to rewrite region header");

    slot.header = header;
    slot.header_loaded = true;
    slot.file_exists = true;
}

fs::path RegionStore::getRegionFilePath(const RegionCoord coord) const {
    const std::string name =
        std::string(REGION_FILE_PREAMBLE) + REGION_FILE_SEPARATOR +
        std::to_string(coord.x) + REGION_FILE_SEPARATOR +
        std::to_string(coord.z) + REGION_FILE_SEPARATOR +
        REGION_FILE_EXTENSION;
    return region_dir_ / name;
}

RegionSlot& RegionStore::getOrCreateRegionSlot(const RegionCoord coord) {
    std::lock_guard<std::mutex> lock{regions_mutex_};

    auto found = regions_.find(coord);
    if (found != regions_.end()) {
        return *found->second;
    }

    auto slot = std::make_unique<RegionSlot>();
    slot->coord = coord;
    slot->file_exists = fs::exists(getRegionFilePath(coord));

    RegionSlot& ref = *slot;
    regions_.emplace(coord, std::move(slot));
    return ref;
}

void RegionStore::ensureHeaderLoaded(RegionSlot& slot) {
    if (slot.header_loaded) {
        return;
    }

    const fs::path path = getRegionFilePath(slot.coord);
    slot.file_exists = fs::exists(path);
    if (!slot.file_exists) {
        generateAndWriteRegion(slot);
        return;
    }

    std::ifstream file{path, std::ios::binary};
    throwIfFileBad(file, FILE_ERROR, "failed to open region file for header read");

    const std::uint64_t file_size = streamSize(file);
    if (file_size < REGION_HEADER_BYTES) {
        throw RegionStoreException(HEADER_CORRUPTED, "region file is smaller than header");
    }

    file.read(reinterpret_cast<char*>(slot.header.data()),
              static_cast<std::streamsize>(REGION_HEADER_BYTES));
    if (file.gcount() != static_cast<std::streamsize>(REGION_HEADER_BYTES)) {
        throw RegionStoreException(HEADER_CORRUPTED, "failed to read complete region header");
    }

    slot.header_loaded = true;
    slot.file_exists = true;
}

void RegionStore::generateAndWriteRegion(RegionSlot& slot) {
    auto region = std::make_unique<RegionData>();
    generateRegionData(slot.coord, *region);

    std::array<HeaderEntry, REGION_CHUNK_COUNT> header{};
    std::vector<Bytef> payload;
    payload.reserve(REGION_CHUNK_COUNT * CHUNK_PAYLOAD_BYTES / 2);

    std::uint64_t next_offset = REGION_HEADER_BYTES;
    for (int local_x = 0; local_x < CHUNKS_PER_REGION_SIDE; local_x++) {
        for (int local_z = 0; local_z < CHUNKS_PER_REGION_SIDE; local_z++) {
            const std::size_t index = chunkIndex(local_x, local_z);
            std::vector<Bytef> compressed = compressChunkBlocks(region->chunks[index]);

            if (next_offset >
                std::numeric_limits<chunk_header_offset_type>::max()) {
                throw RegionStoreException(FILE_ERROR,
                                           "generated region offset exceeds header capacity");
            }
            if (compressed.size() >
                std::numeric_limits<chunk_header_length_type>::max()) {
                throw RegionStoreException(COMPRESSION_ERROR,
                                           "generated chunk exceeds header length capacity");
            }

            header[index].offset = static_cast<chunk_header_offset_type>(next_offset);
            header[index].length =
                static_cast<chunk_header_length_type>(compressed.size());
            next_offset += compressed.size();

            payload.insert(payload.end(), compressed.begin(), compressed.end());
        }
    }

    std::ofstream file{getRegionFilePath(slot.coord),
                       std::ios::binary | std::ios::trunc};
    throwIfFileBad(file, FILE_ERROR, "failed to create generated region file");

    file.write(reinterpret_cast<const char*>(header.data()),
               static_cast<std::streamsize>(REGION_HEADER_BYTES));
    throwIfFileBad(file, FILE_ERROR, "failed to write generated region header");

    file.write(reinterpret_cast<const char*>(payload.data()),
               static_cast<std::streamsize>(payload.size()));
    throwIfFileBad(file, FILE_ERROR, "failed to write generated region payload");

    slot.header = header;
    slot.header_loaded = true;
    slot.file_exists = true;
}

} // namespace torq
