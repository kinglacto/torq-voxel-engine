#pragma once

#include "chunk_utility.h"
#include "vertex.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace torq {

using ChunkErrorCode = ::ChunkErrorCode;
using BlockData = ::blockData;

inline constexpr int BLOCKS_PER_CHUNK =
    BLOCK_X_SIZE * BLOCK_Y_SIZE * BLOCK_Z_SIZE;

inline constexpr std::uint32_t MAX_PENDING_EDITS_PER_CHUNK = 64;
inline constexpr std::uint32_t MAX_CHUNK_SLOTS = 4096;
inline constexpr std::uint32_t MAX_TRANSIENT_CHUNK_SLOTS = 512;
inline constexpr std::uint32_t MAX_CHUNK_INDEX_ENTRIES = 8192;
inline constexpr std::uint32_t MAX_RESULTS_PER_FRAME = 256;
inline constexpr std::uint32_t MAX_CHUNK_WORK_QUEUE_ENTRIES = 1024;
inline constexpr std::uint32_t MAX_CHUNK_RESULT_QUEUE_ENTRIES = 1024;
inline constexpr std::uint64_t INVALID_REVISION = UINT64_MAX;
inline constexpr std::uint32_t LOAD_RETRY_COOLDOWN_TICKS = 120;
inline constexpr int CHUNK_BORDER_BITS = BLOCK_Y_SIZE * BLOCK_X_SIZE;
inline constexpr int CHUNK_BORDER_WORDS = (CHUNK_BORDER_BITS + 63) / 64;

using SlotId = std::uint32_t;
inline constexpr SlotId INVALID_SLOT = UINT32_MAX;

struct ChunkCoord {
    int x{0};
    int z{0};

    friend bool operator==(const ChunkCoord&, const ChunkCoord&) = default;
};

struct RegionCoord {
    int x{0};
    int z{0};

    friend bool operator==(const RegionCoord&, const RegionCoord&) = default;
};

struct WorldBlockCoord {
    int x{0};
    int y{0};
    int z{0};

    friend bool operator==(const WorldBlockCoord&, const WorldBlockCoord&) = default;
};

struct LocalBlockCoord {
    int x{0};
    int y{0};
    int z{0};

    friend bool operator==(const LocalBlockCoord&, const LocalBlockCoord&) = default;
};

struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& coord) const noexcept;
};

struct RegionCoordHash {
    std::size_t operator()(const RegionCoord& coord) const noexcept;
};

struct ChunkCacheConfig {
    int active_radius{5};
    int render_distance{10};
    int keep_margin{2};
};

struct StreamBudget {
    int max_results{64};
    int max_load_jobs{8};
    int max_persist_jobs{4};
    int max_mesh_jobs{8};
    int max_clean_evictions{16};
};

struct ChunkData {
    std::array<BlockData, BLOCKS_PER_CHUNK> blocks{};
};

struct RegionData {
    RegionCoord coord{};
    std::array<ChunkData, CHUNKS_PER_REGION_SIDE * CHUNKS_PER_REGION_SIDE> chunks{};
};

struct CpuChunkMesh {
    std::vector<TextureVertex> vertices;
};

struct ChunkBorderMask {
    std::array<std::uint64_t, CHUNK_BORDER_WORDS> words{};
    bool present{false};
};

struct ChunkNeighborMasks {
    ChunkBorderMask neg_x{};
    ChunkBorderMask pos_x{};
    ChunkBorderMask neg_z{};
    ChunkBorderMask pos_z{};
};

[[nodiscard]] inline constexpr int chunkXBorderBitIndex(const int y,
                                                        const int z) noexcept {
    return y * BLOCK_Z_SIZE + z;
}

[[nodiscard]] inline constexpr int chunkZBorderBitIndex(const int y,
                                                        const int x) noexcept {
    return y * BLOCK_X_SIZE + x;
}

inline void setChunkBorderMaskBit(ChunkBorderMask& mask,
                                  const int bit) noexcept {
    mask.words[static_cast<std::size_t>(bit / 64)] |=
        (std::uint64_t{1} << static_cast<unsigned>(bit % 64));
}

[[nodiscard]] inline bool testChunkBorderMaskBit(const ChunkBorderMask& mask,
                                                 const int bit) noexcept {
    return (mask.words[static_cast<std::size_t>(bit / 64)] &
            (std::uint64_t{1} << static_cast<unsigned>(bit % 64))) != 0;
}

enum class BlockEditKind {
    SetBlock
};

struct BlockEditCommand {
    BlockEditKind kind{BlockEditKind::SetBlock};
    LocalBlockCoord local{};
    BlockData block{};
};

template <std::size_t N>
struct FixedEditBuffer {
    std::array<BlockEditCommand, N> commands{};
    std::uint16_t count{0};

    [[nodiscard]] bool full() const noexcept { return count >= N; }
    [[nodiscard]] bool empty() const noexcept { return count == 0; }
    void clear() noexcept { count = 0; }
};

enum class ChunkState {
    NotResident,
    Loading,
    Resident,
    PersistingForEviction,
    Failed
};

enum class IndexState : std::uint8_t {
    Empty,
    Occupied,
    Tombstone
};

struct ChunkSlot {
    SlotId id{INVALID_SLOT};
    std::uint32_t live_index{0};

    ChunkCoord coord{};
    RegionCoord region{};
    int region_local_x{0};
    int region_local_z{0};

    ChunkState state{ChunkState::NotResident};
    ChunkErrorCode last_error{NO_ERROR};

    std::unique_ptr<ChunkData> data;
    FixedEditBuffer<MAX_PENDING_EDITS_PER_CHUNK> pending_edits{};

    std::uint64_t current_revision{0};
    std::uint64_t rendered_revision{INVALID_REVISION};
    std::uint64_t mesh_job_revision{INVALID_REVISION};
    bool dirty_for_disk{false};

    std::uint64_t next_retry_tick{0};
};

struct ChunkSlotIndexEntry {
    ChunkCoord coord{};
    SlotId slot{INVALID_SLOT};
    IndexState state{IndexState::Empty};
};

struct ChunkSlotPool {
    std::array<ChunkSlot, MAX_CHUNK_SLOTS> slots{};
    std::array<SlotId, MAX_CHUNK_SLOTS> free_stack{};
    std::uint32_t free_count{0};

    std::array<SlotId, MAX_CHUNK_SLOTS> live_slots{};
    std::uint32_t live_count{0};
};

struct ChunkSlotIndex {
    std::array<ChunkSlotIndexEntry, MAX_CHUNK_INDEX_ENTRIES> entries{};
    std::uint32_t occupied_count{0};
    std::uint32_t tombstone_count{0};
};

struct ChunkCacheStorage {
    ChunkSlotPool pool{};
    ChunkSlotIndex index{};
    std::uint64_t current_stream_tick{0};
};

enum class JobType {
    LoadChunk,
    PersistChunk,
    BuildChunkMesh,
    ReleaseChunkData
};

struct Job {
    JobType type{JobType::LoadChunk};
    ChunkCoord chunk{};
    std::uint64_t revision{0};

    const ChunkData* read_data{nullptr};
    std::unique_ptr<ChunkData> owned_data;
    ChunkNeighborMasks neighbor_masks{};
};

enum class ResultType {
    ChunkLoaded,
    ChunkPersisted,
    ChunkMeshBuilt,
    Failed
};

struct Result {
    ResultType type{ResultType::Failed};
    JobType source_job{JobType::LoadChunk};
    ChunkCoord chunk{};
    ChunkErrorCode error{NO_ERROR};
    std::unique_ptr<ChunkData> data;
    std::uint64_t revision{0};
    CpuChunkMesh cpu_mesh{};
};

enum class BlockEditResult {
    Applied,
    Queued,
    NotResident,
    OutsideActiveRadius,
    EditQueueFull,
    BlockedByPlayer
};

struct ChunkCacheStats {
    std::uint32_t slots{0};
    std::uint32_t free_slots{0};
    std::uint32_t index_occupied{0};
    std::uint32_t index_tombstones{0};
    std::uint32_t resident{0};
    std::uint32_t loading{0};
    std::uint32_t failed{0};
    std::uint32_t dirty_for_render{0};
    std::uint32_t mesh_jobs_in_flight{0};
    std::uint32_t chunks_with_pending_edits{0};
    std::uint32_t dirty_for_disk{0};
    std::uint32_t persisting_for_eviction{0};

    std::uint64_t edits_applied{0};
    std::uint64_t edits_queued{0};
    std::uint64_t edit_queue_full{0};
    std::uint64_t slot_pool_full{0};
    std::uint64_t index_full{0};
    std::uint64_t load_jobs_submitted{0};
    std::uint64_t load_jobs_completed{0};
    std::uint64_t persist_jobs_submitted{0};
    std::uint64_t persist_jobs_completed{0};
    std::uint64_t mesh_jobs_submitted{0};
    std::uint64_t mesh_jobs_completed{0};
    std::uint64_t release_jobs_submitted{0};
    std::uint64_t mesh_uploads{0};
    std::uint64_t region_generations{0};
    std::uint64_t bytes_read{0};
    std::uint64_t bytes_written{0};
    std::uint64_t evictions{0};
    std::uint64_t failures{0};
};

int floorDiv(int value, int divisor) noexcept;
int positiveMod(int value, int divisor) noexcept;

ChunkCoord chunkCoordFromWorldBlock(WorldBlockCoord world) noexcept;
LocalBlockCoord localBlockCoordFromWorldBlock(WorldBlockCoord world) noexcept;
RegionCoord regionCoordFromChunk(ChunkCoord chunk) noexcept;
int regionLocalChunkX(ChunkCoord chunk) noexcept;
int regionLocalChunkZ(ChunkCoord chunk) noexcept;
int chebyshevDistance(ChunkCoord a, ChunkCoord b) noexcept;

} // namespace torq
