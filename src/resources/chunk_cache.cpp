#include "chunk_cache.h"

#include "chunk_renderer.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <span>

namespace torq {
namespace {

constexpr std::uint32_t INDEX_REBUILD_TOMBSTONE_THRESHOLD =
    MAX_CHUNK_INDEX_ENTRIES / 4;

[[nodiscard]] std::uint64_t packSignedPair(const int x, const int z) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
           static_cast<std::uint32_t>(z);
}

[[nodiscard]] std::uint64_t mix64(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] std::size_t hashCoord(const int x, const int z) noexcept {
    return static_cast<std::size_t>(mix64(packSignedPair(x, z)));
}

[[nodiscard]] std::uint32_t initialProbe(const ChunkCoord coord) noexcept {
    return static_cast<std::uint32_t>(
        ChunkCoordHash{}(coord) % MAX_CHUNK_INDEX_ENTRIES
    );
}

[[nodiscard]] SlotId findInIndex(const ChunkSlotIndex& index,
                                 const ChunkCoord coord) noexcept {
    std::uint32_t probe = initialProbe(coord);
    for (std::uint32_t checked = 0; checked < MAX_CHUNK_INDEX_ENTRIES; checked++) {
        const ChunkSlotIndexEntry& entry = index.entries[probe];
        if (entry.state == IndexState::Empty) {
            return INVALID_SLOT;
        }

        if (entry.state == IndexState::Occupied && entry.coord == coord) {
            return entry.slot;
        }

        probe = (probe + 1U) % MAX_CHUNK_INDEX_ENTRIES;
    }

    return INVALID_SLOT;
}

[[nodiscard]] bool insertIntoIndexNoRebuild(ChunkSlotIndex& index,
                                            const ChunkCoord coord,
                                            const SlotId slot) noexcept {
    std::uint32_t probe = initialProbe(coord);
    std::uint32_t first_tombstone = MAX_CHUNK_INDEX_ENTRIES;

    for (std::uint32_t checked = 0; checked < MAX_CHUNK_INDEX_ENTRIES; checked++) {
        ChunkSlotIndexEntry& entry = index.entries[probe];

        if (entry.state == IndexState::Occupied) {
            if (entry.coord == coord) {
                entry.slot = slot;
                return true;
            }
        } else if (entry.state == IndexState::Tombstone) {
            if (first_tombstone == MAX_CHUNK_INDEX_ENTRIES) {
                first_tombstone = probe;
            }
        } else {
            const std::uint32_t target =
                first_tombstone != MAX_CHUNK_INDEX_ENTRIES ? first_tombstone : probe;
            ChunkSlotIndexEntry& target_entry = index.entries[target];
            if (target_entry.state == IndexState::Tombstone) {
                assert(index.tombstone_count > 0);
                index.tombstone_count--;
            }

            target_entry.coord = coord;
            target_entry.slot = slot;
            target_entry.state = IndexState::Occupied;
            index.occupied_count++;
            return true;
        }

        probe = (probe + 1U) % MAX_CHUNK_INDEX_ENTRIES;
    }

    if (first_tombstone != MAX_CHUNK_INDEX_ENTRIES) {
        ChunkSlotIndexEntry& target_entry = index.entries[first_tombstone];
        assert(index.tombstone_count > 0);
        index.tombstone_count--;
        target_entry.coord = coord;
        target_entry.slot = slot;
        target_entry.state = IndexState::Occupied;
        index.occupied_count++;
        return true;
    }

    return false;
}

void rebuildIndex(ChunkCacheStorage& storage) noexcept {
    for (ChunkSlotIndexEntry& entry : storage.index.entries) {
        entry = ChunkSlotIndexEntry{};
    }
    storage.index.occupied_count = 0;
    storage.index.tombstone_count = 0;

    for (std::uint32_t i = 0; i < storage.pool.live_count; i++) {
        const SlotId slot_id = storage.pool.live_slots[i];
        const ChunkSlot& slot = storage.pool.slots[slot_id];
        const bool inserted =
            insertIntoIndexNoRebuild(storage.index, slot.coord, slot_id);
        assert(inserted);
    }
}

[[nodiscard]] bool insertIntoIndex(ChunkCacheStorage& storage,
                                   const ChunkCoord coord,
                                   const SlotId slot) noexcept {
    if (storage.index.tombstone_count >= INDEX_REBUILD_TOMBSTONE_THRESHOLD) {
        rebuildIndex(storage);
    }

    if (insertIntoIndexNoRebuild(storage.index, coord, slot)) {
        return true;
    }

    if (storage.index.tombstone_count > 0) {
        rebuildIndex(storage);
        return insertIntoIndexNoRebuild(storage.index, coord, slot);
    }

    return false;
}

[[nodiscard]] bool eraseFromIndex(ChunkSlotIndex& index,
                                  const ChunkCoord coord) noexcept {
    std::uint32_t probe = initialProbe(coord);
    for (std::uint32_t checked = 0; checked < MAX_CHUNK_INDEX_ENTRIES; checked++) {
        ChunkSlotIndexEntry& entry = index.entries[probe];
        if (entry.state == IndexState::Empty) {
            return false;
        }

        if (entry.state == IndexState::Occupied && entry.coord == coord) {
            entry.slot = INVALID_SLOT;
            entry.state = IndexState::Tombstone;
            assert(index.occupied_count > 0);
            index.occupied_count--;
            index.tombstone_count++;
            return true;
        }

        probe = (probe + 1U) % MAX_CHUNK_INDEX_ENTRIES;
    }

    return false;
}

void updateInstantStats(ChunkCacheStats& stats,
                        const ChunkCacheStorage& storage) noexcept {
    stats.slots = storage.pool.live_count;
    stats.free_slots = storage.pool.free_count;
    stats.index_occupied = storage.index.occupied_count;
    stats.index_tombstones = storage.index.tombstone_count;

    stats.resident = 0;
    stats.loading = 0;
    stats.failed = 0;
    stats.dirty_for_render = 0;
    stats.mesh_jobs_in_flight = 0;
    stats.chunks_with_pending_edits = 0;
    stats.dirty_for_disk = 0;
    stats.persisting_for_eviction = 0;

    for (std::uint32_t i = 0; i < storage.pool.live_count; i++) {
        const ChunkSlot& slot = storage.pool.slots[storage.pool.live_slots[i]];
        switch (slot.state) {
        case ChunkState::Resident:
            stats.resident++;
            break;
        case ChunkState::Loading:
            stats.loading++;
            break;
        case ChunkState::PersistingForEviction:
            stats.persisting_for_eviction++;
            break;
        case ChunkState::Failed:
            stats.failed++;
            break;
        case ChunkState::NotResident:
            break;
        }

        if (slot.current_revision != slot.rendered_revision) {
            stats.dirty_for_render++;
        }
        if (slot.mesh_job_revision != INVALID_REVISION) {
            stats.mesh_jobs_in_flight++;
        }
        if (slot.pending_edits.count > 0) {
            stats.chunks_with_pending_edits++;
        }
        if (slot.dirty_for_disk) {
            stats.dirty_for_disk++;
        }
    }
}

[[nodiscard]] bool isValidLocalBlock(const LocalBlockCoord local) noexcept {
    return local.x >= 0 && local.x < BLOCK_X_SIZE &&
           local.y >= 0 && local.y < BLOCK_Y_SIZE &&
           local.z >= 0 && local.z < BLOCK_Z_SIZE;
}

[[nodiscard]] std::size_t blockIndex(const LocalBlockCoord local) noexcept {
    assert(isValidLocalBlock(local));
    return static_cast<std::size_t>(
        (local.y * BLOCK_X_SIZE + local.x) * BLOCK_Z_SIZE + local.z
    );
}

[[nodiscard]] bool isSolidBlock(const ChunkData& chunk,
                                const LocalBlockCoord local) noexcept {
    return chunk.blocks[blockIndex(local)].id != BlockMap::air;
}

void fillXBorderMask(ChunkBorderMask& out,
                     const ChunkData& chunk,
                     const int local_x) noexcept {
    assert(local_x == 0 || local_x == BLOCK_X_SIZE - 1);

    out.present = true;
    for (int y = 0; y < BLOCK_Y_SIZE; y++) {
        for (int z = 0; z < BLOCK_Z_SIZE; z++) {
            if (isSolidBlock(chunk, LocalBlockCoord{local_x, y, z})) {
                setChunkBorderMaskBit(out, chunkXBorderBitIndex(y, z));
            }
        }
    }
}

void fillZBorderMask(ChunkBorderMask& out,
                     const ChunkData& chunk,
                     const int local_z) noexcept {
    assert(local_z == 0 || local_z == BLOCK_Z_SIZE - 1);

    out.present = true;
    for (int y = 0; y < BLOCK_Y_SIZE; y++) {
        for (int x = 0; x < BLOCK_X_SIZE; x++) {
            if (isSolidBlock(chunk, LocalBlockCoord{x, y, local_z})) {
                setChunkBorderMaskBit(out, chunkZBorderBitIndex(y, x));
            }
        }
    }
}

template<typename Visitor>
bool visitChebyshevRings(const ChunkCoord center,
                         const int radius,
                         Visitor&& visitor) {
    if (radius < 0) {
        return true;
    }

    if (!visitor(center)) {
        return false;
    }

    for (int ring = 1; ring <= radius; ring++) {
        const int min_x = center.x - ring;
        const int max_x = center.x + ring;
        const int min_z = center.z - ring;
        const int max_z = center.z + ring;

        for (int x = min_x; x <= max_x; x++) {
            if (!visitor(ChunkCoord{x, min_z})) {
                return false;
            }
            if (!visitor(ChunkCoord{x, max_z})) {
                return false;
            }
        }

        for (int z = min_z + 1; z <= max_z - 1; z++) {
            if (!visitor(ChunkCoord{min_x, z})) {
                return false;
            }
            if (!visitor(ChunkCoord{max_x, z})) {
                return false;
            }
        }
    }

    return true;
}

} // namespace

std::size_t ChunkCoordHash::operator()(const ChunkCoord& coord) const noexcept {
    return hashCoord(coord.x, coord.z);
}

std::size_t RegionCoordHash::operator()(const RegionCoord& coord) const noexcept {
    return hashCoord(coord.x, coord.z);
}

int floorDiv(const int value, const int divisor) noexcept {
    assert(divisor > 0);

    int quotient = value / divisor;
    const int remainder = value % divisor;
    if (remainder != 0 && value < 0) {
        quotient--;
    }

    return quotient;
}

int positiveMod(const int value, const int divisor) noexcept {
    assert(divisor > 0);

    const int remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

ChunkCoord chunkCoordFromWorldBlock(const WorldBlockCoord world) noexcept {
    return {
        floorDiv(world.x, BLOCK_X_SIZE),
        floorDiv(world.z, BLOCK_Z_SIZE)
    };
}

LocalBlockCoord localBlockCoordFromWorldBlock(const WorldBlockCoord world) noexcept {
    return {
        positiveMod(world.x, BLOCK_X_SIZE),
        world.y,
        positiveMod(world.z, BLOCK_Z_SIZE)
    };
}

RegionCoord regionCoordFromChunk(const ChunkCoord chunk) noexcept {
    return {
        floorDiv(chunk.x, CHUNKS_PER_REGION_SIDE),
        floorDiv(chunk.z, CHUNKS_PER_REGION_SIDE)
    };
}

int regionLocalChunkX(const ChunkCoord chunk) noexcept {
    return positiveMod(chunk.x, CHUNKS_PER_REGION_SIDE);
}

int regionLocalChunkZ(const ChunkCoord chunk) noexcept {
    return positiveMod(chunk.z, CHUNKS_PER_REGION_SIDE);
}

int chebyshevDistance(const ChunkCoord a, const ChunkCoord b) noexcept {
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

ChunkCache::ChunkCache(ChunkCacheConfig config, ChunkWorkQueue& work_queue)
    : config_{config},
      work_queue_{work_queue} {
    assert(config_.active_radius >= 0);
    assert(config_.render_distance >= 0);
    assert(config_.keep_margin >= 0);
    assert(config_.active_radius <= config_.render_distance);

    const int keep_radius = config_.render_distance + config_.keep_margin;
    const std::int64_t side = static_cast<std::int64_t>(2 * keep_radius + 1);
    const std::int64_t required_resident_slots = side * side;
    const std::int64_t required_slots =
        required_resident_slots + MAX_TRANSIENT_CHUNK_SLOTS;

    assert(required_slots <= MAX_CHUNK_SLOTS);
    assert(MAX_CHUNK_SLOTS * 2 <= MAX_CHUNK_INDEX_ENTRIES);

    for (SlotId id = 0; id < MAX_CHUNK_SLOTS; id++) {
        storage_.pool.free_stack[storage_.pool.free_count++] = id;
    }

    for (ChunkSlotIndexEntry& entry : storage_.index.entries) {
        entry.state = IndexState::Empty;
    }

    for (SlotId& id : storage_.pool.live_slots) {
        id = INVALID_SLOT;
    }

    updateInstantStats(stats_, storage_);
}

void ChunkCache::setCenterChunk(const ChunkCoord center) noexcept {
    center_chunk_ = center;
}

void ChunkCache::tick(const StreamBudget budget, ChunkRenderer& renderer) {
    storage_.current_stream_tick++;

    int remaining_load_budget = std::max(0, budget.max_load_jobs);
    visitChebyshevRings(center_chunk_, config_.active_radius,
        [this, &remaining_load_budget](const ChunkCoord coord) {
            if (remaining_load_budget <= 0) {
                return false;
            }

            requestLoadIfNeeded(coord, remaining_load_budget);
            return remaining_load_budget > 0;
        });

    if (remaining_load_budget > 0) {
        visitChebyshevRings(center_chunk_, config_.render_distance,
            [this, &remaining_load_budget](const ChunkCoord coord) {
                if (remaining_load_budget <= 0) {
                    return false;
                }

                if (chebyshevDistance(coord, center_chunk_) > config_.active_radius) {
                    requestLoadIfNeeded(coord, remaining_load_budget);
                }

                return remaining_load_budget > 0;
            });
    }

    int remaining_mesh_budget = std::max(0, budget.max_mesh_jobs);
    submitMeshJobs(remaining_mesh_budget);
    processEvictions(budget, renderer);
    updateInstantStats(stats_, storage_);
}

void ChunkCache::applyLoadedChunk(Result&& result) {
    if (result.source_job == JobType::LoadChunk) {
        stats_.load_jobs_completed++;
    }

    ChunkSlot* slot = findSlot(result.chunk);
    if (slot == nullptr || slot->state != ChunkState::Loading) {
        updateInstantStats(stats_, storage_);
        return;
    }

    if (result.type != ResultType::ChunkLoaded || result.error != NO_ERROR || !result.data) {
        slot->state = ChunkState::Failed;
        slot->last_error = result.error != NO_ERROR ? result.error : FILE_ERROR;
        slot->next_retry_tick =
            storage_.current_stream_tick + LOAD_RETRY_COOLDOWN_TICKS;
        stats_.failures++;
        updateInstantStats(stats_, storage_);
        return;
    }

    slot->state = ChunkState::Resident;
    slot->last_error = NO_ERROR;
    slot->data = std::move(result.data);
    slot->pending_edits.clear();
    slot->current_revision = 0;
    slot->rendered_revision = INVALID_REVISION;
    slot->mesh_job_revision = INVALID_REVISION;
    slot->dirty_for_disk = false;
    slot->next_retry_tick = 0;
    markResidentHorizontalNeighborsDirty(slot->coord);

    updateInstantStats(stats_, storage_);
}

void ChunkCache::applyPersistedChunk(Result&& result, ChunkRenderer& renderer) {
    if (result.source_job == JobType::PersistChunk) {
        stats_.persist_jobs_completed++;
    }

    ChunkSlot* slot = findSlot(result.chunk);
    if (slot == nullptr || slot->state != ChunkState::PersistingForEviction) {
        if (result.data) {
            const bool release_submitted = trySubmitRelease(result.data);
            assert(release_submitted);
            (void)release_submitted;
        }
        updateInstantStats(stats_, storage_);
        return;
    }

    if (result.type == ResultType::ChunkPersisted &&
        result.error == NO_ERROR &&
        result.data) {
        slot->dirty_for_disk = false;
        slot->last_error = NO_ERROR;

        if (outsideKeepWindow(slot->coord)) {
            if (trySubmitRelease(result.data)) {
                renderer.deleteMesh(slot->coord);
                freeSlot(slot->id);
                stats_.evictions++;
                updateInstantStats(stats_, storage_);
                return;
            }

            slot->data = std::move(result.data);
            slot->state = ChunkState::Resident;
            markResidentHorizontalNeighborsDirty(slot->coord);
        } else {
            slot->data = std::move(result.data);
            slot->state = ChunkState::Resident;
            markResidentHorizontalNeighborsDirty(slot->coord);
        }

        updateInstantStats(stats_, storage_);
        return;
    }

    slot->data = std::move(result.data);
    slot->state = ChunkState::Resident;
    slot->dirty_for_disk = true;
    slot->last_error = result.error != NO_ERROR ? result.error : FILE_ERROR;
    markResidentHorizontalNeighborsDirty(slot->coord);
    stats_.failures++;
    updateInstantStats(stats_, storage_);
}

void ChunkCache::applyFailedJob(const Result& result) {
    if (result.source_job == JobType::LoadChunk) {
        stats_.load_jobs_completed++;
    }

    ChunkSlot* slot = findSlot(result.chunk);
    if (slot == nullptr) {
        stats_.failures++;
        updateInstantStats(stats_, storage_);
        return;
    }

    if (result.source_job == JobType::LoadChunk && slot->state == ChunkState::Loading) {
        slot->state = ChunkState::Failed;
        slot->last_error = result.error != NO_ERROR ? result.error : FILE_ERROR;
        slot->next_retry_tick =
            storage_.current_stream_tick + LOAD_RETRY_COOLDOWN_TICKS;
    }

    stats_.failures++;
    updateInstantStats(stats_, storage_);
}

void ChunkCache::applyBuiltMeshResult(Result&& result, ChunkRenderer& renderer) {
    if (result.source_job == JobType::BuildChunkMesh) {
        stats_.mesh_jobs_completed++;
    }

    ChunkSlot* slot = findSlot(result.chunk);
    if (slot == nullptr) {
        updateInstantStats(stats_, storage_);
        return;
    }

    if (slot->mesh_job_revision != result.revision) {
        updateInstantStats(stats_, storage_);
        return;
    }

    slot->mesh_job_revision = INVALID_REVISION;

    if (slot->state == ChunkState::Resident &&
        renderer.uploadMesh(result.chunk, result.revision, std::move(result.cpu_mesh))) {
        slot->rendered_revision = result.revision;
        stats_.mesh_uploads++;
    }

    if (slot->state == ChunkState::Resident && slot->pending_edits.count > 0) {
        applyPendingEdits(*slot);
    }

    updateInstantStats(stats_, storage_);
}

const ChunkCacheStorage& ChunkCache::storage() const noexcept {
    return storage_;
}

const ChunkCacheStats& ChunkCache::stats() const noexcept {
    return stats_;
}

ChunkSlot* ChunkCache::findSlot(const ChunkCoord coord) noexcept {
    const SlotId id = findInIndex(storage_.index, coord);
    return id != INVALID_SLOT ? &storage_.pool.slots[id] : nullptr;
}

const ChunkSlot* ChunkCache::findSlot(const ChunkCoord coord) const noexcept {
    const SlotId id = findInIndex(storage_.index, coord);
    return id != INVALID_SLOT ? &storage_.pool.slots[id] : nullptr;
}

ChunkSlot* ChunkCache::allocateSlot(const ChunkCoord coord) {
    if (ChunkSlot* existing = findSlot(coord)) {
        return existing;
    }

    if (storage_.pool.free_count == 0) {
        stats_.slot_pool_full++;
        updateInstantStats(stats_, storage_);
        return nullptr;
    }

    const SlotId id = storage_.pool.free_stack[storage_.pool.free_count - 1];
    if (!insertIntoIndex(storage_, coord, id)) {
        stats_.index_full++;
        updateInstantStats(stats_, storage_);
        return nullptr;
    }

    storage_.pool.free_count--;

    ChunkSlot& slot = storage_.pool.slots[id];
    slot = ChunkSlot{};
    slot.id = id;
    slot.live_index = storage_.pool.live_count;
    slot.coord = coord;
    slot.region = regionCoordFromChunk(coord);
    slot.region_local_x = regionLocalChunkX(coord);
    slot.region_local_z = regionLocalChunkZ(coord);
    slot.state = ChunkState::NotResident;
    slot.last_error = NO_ERROR;
    slot.current_revision = 0;
    slot.rendered_revision = INVALID_REVISION;
    slot.mesh_job_revision = INVALID_REVISION;

    storage_.pool.live_slots[storage_.pool.live_count++] = id;
    updateInstantStats(stats_, storage_);
    return &slot;
}

void ChunkCache::freeSlot(const SlotId id) {
    assert(id < MAX_CHUNK_SLOTS);

    ChunkSlot& slot = storage_.pool.slots[id];
    assert(slot.id == id);
    assert(slot.data == nullptr);
    assert(slot.mesh_job_revision == INVALID_REVISION);
    assert(slot.pending_edits.count == 0);

    const bool erased = eraseFromIndex(storage_.index, slot.coord);
    assert(erased);

    const std::uint32_t remove_index = slot.live_index;
    assert(remove_index < storage_.pool.live_count);

    const std::uint32_t last_index = storage_.pool.live_count - 1;
    const SlotId last_id = storage_.pool.live_slots[last_index];
    storage_.pool.live_count--;

    if (remove_index != last_index) {
        storage_.pool.live_slots[remove_index] = last_id;
        storage_.pool.slots[last_id].live_index = remove_index;
    }

    storage_.pool.live_slots[last_index] = INVALID_SLOT;
    slot = ChunkSlot{};
    storage_.pool.free_stack[storage_.pool.free_count++] = id;

    updateInstantStats(stats_, storage_);
}

bool ChunkCache::outsideKeepWindow(const ChunkCoord coord) const noexcept {
    const int keep_radius = config_.render_distance + config_.keep_margin;
    return chebyshevDistance(coord, center_chunk_) > keep_radius;
}

bool ChunkCache::insideActiveWindow(const ChunkCoord coord) const noexcept {
    return chebyshevDistance(coord, center_chunk_) <= config_.active_radius;
}

void ChunkCache::requestLoadIfNeeded(const ChunkCoord coord,
                                     int& remaining_load_budget) {
    if (remaining_load_budget <= 0) {
        return;
    }

    ChunkSlot* slot = findSlot(coord);
    if (slot == nullptr) {
        slot = allocateSlot(coord);
        if (slot == nullptr) {
            return;
        }
    }

    const bool should_submit =
        slot->state == ChunkState::NotResident ||
        (slot->state == ChunkState::Failed &&
         storage_.current_stream_tick >= slot->next_retry_tick);
    if (!should_submit) {
        return;
    }

    Job job{};
    job.type = JobType::LoadChunk;
    job.chunk = coord;

    if (work_queue_.tryPush(std::move(job))) {
        slot->state = ChunkState::Loading;
        slot->last_error = NO_ERROR;
        stats_.load_jobs_submitted++;
        remaining_load_budget--;
        updateInstantStats(stats_, storage_);
    }
}

void ChunkCache::submitMeshJobs(int& remaining_mesh_budget) {
    if (remaining_mesh_budget <= 0) {
        return;
    }

    for (std::uint32_t i = 0;
         i < storage_.pool.live_count && remaining_mesh_budget > 0;
         i++) {
        ChunkSlot& slot = storage_.pool.slots[storage_.pool.live_slots[i]];
        if (slot.state != ChunkState::Resident ||
            !slot.data ||
            slot.pending_edits.count != 0 ||
            slot.rendered_revision == slot.current_revision ||
            slot.mesh_job_revision != INVALID_REVISION) {
            continue;
        }

        Job job{};
        job.type = JobType::BuildChunkMesh;
        job.chunk = slot.coord;
        job.read_data = slot.data.get();
        job.revision = slot.current_revision;
        job.neighbor_masks = buildNeighborMasks(slot.coord);

        if (work_queue_.tryPush(std::move(job))) {
            slot.mesh_job_revision = slot.current_revision;
            stats_.mesh_jobs_submitted++;
            remaining_mesh_budget--;
            updateInstantStats(stats_, storage_);
        }
    }
}

void ChunkCache::processEvictions(StreamBudget budget, ChunkRenderer& renderer) {
    int remaining_persist_budget = std::max(0, budget.max_persist_jobs);
    int remaining_clean_budget = std::max(0, budget.max_clean_evictions);

    for (std::uint32_t i = 0; i < storage_.pool.live_count;) {
        const SlotId slot_id = storage_.pool.live_slots[i];
        ChunkSlot& slot = storage_.pool.slots[slot_id];

        if (!outsideKeepWindow(slot.coord)) {
            i++;
            continue;
        }

        if (slot.state == ChunkState::Failed ||
            slot.state == ChunkState::NotResident) {
            if (remaining_clean_budget <= 0) {
                i++;
                continue;
            }

            renderer.deleteMesh(slot.coord);
            freeSlot(slot_id);
            stats_.evictions++;
            remaining_clean_budget--;
            continue;
        }

        if (slot.state != ChunkState::Resident ||
            slot.mesh_job_revision != INVALID_REVISION ||
            slot.pending_edits.count != 0) {
            i++;
            continue;
        }

        if (!slot.data) {
            i++;
            continue;
        }

        if (slot.dirty_for_disk) {
            if (remaining_persist_budget <= 0) {
                i++;
                continue;
            }

            Job job{};
            job.type = JobType::PersistChunk;
            job.chunk = slot.coord;
            job.revision = slot.current_revision;
            job.owned_data = std::move(slot.data);

            if (work_queue_.tryPush(std::move(job))) {
                markResidentHorizontalNeighborsDirty(slot.coord);
                slot.state = ChunkState::PersistingForEviction;
                stats_.persist_jobs_submitted++;
                remaining_persist_budget--;
            } else {
                slot.data = std::move(job.owned_data);
            }

            i++;
            continue;
        }

        if (remaining_clean_budget <= 0) {
            i++;
            continue;
        }

        if (trySubmitRelease(slot.data)) {
            markResidentHorizontalNeighborsDirty(slot.coord);
            renderer.deleteMesh(slot.coord);
            freeSlot(slot_id);
            stats_.evictions++;
            remaining_clean_budget--;
            continue;
        }

        i++;
    }

    updateInstantStats(stats_, storage_);
}

bool ChunkCache::tryGetBlock(const WorldBlockCoord pos, BlockData* out) const {
    if (out == nullptr) {
        return false;
    }

    const LocalBlockCoord local = localBlockCoordFromWorldBlock(pos);
    if (!isValidLocalBlock(local)) {
        return false;
    }

    const ChunkSlot* slot = findSlot(chunkCoordFromWorldBlock(pos));
    if (slot == nullptr || slot->state != ChunkState::Resident || !slot->data) {
        return false;
    }

    *out = slot->data->blocks[blockIndex(local)];
    return true;
}

bool ChunkCache::isSolidForPhysics(const WorldBlockCoord pos) const {
    if (pos.y < 0) {
        return true;
    }

    if (pos.y >= BLOCK_Y_SIZE) {
        return false;
    }

    BlockData block{};
    if (!tryGetBlock(pos, &block)) {
        return true;
    }

    return block.id != 0;
}

BlockEditResult ChunkCache::setBlock(const WorldBlockCoord pos,
                                     const BlockData block) {
    const ChunkCoord coord = chunkCoordFromWorldBlock(pos);
    const LocalBlockCoord local = localBlockCoordFromWorldBlock(pos);
    if (!isValidLocalBlock(local)) {
        return BlockEditResult::NotResident;
    }

    ChunkSlot* slot = findSlot(coord);
    if (slot == nullptr || slot->state != ChunkState::Resident || !slot->data) {
        return BlockEditResult::NotResident;
    }

    if (!insideActiveWindow(coord)) {
        return BlockEditResult::OutsideActiveRadius;
    }

    if (slot->mesh_job_revision != INVALID_REVISION) {
        if (slot->pending_edits.full()) {
            stats_.edit_queue_full++;
            updateInstantStats(stats_, storage_);
            return BlockEditResult::EditQueueFull;
        }

        BlockEditCommand& command =
            slot->pending_edits.commands[slot->pending_edits.count++];
        command.kind = BlockEditKind::SetBlock;
        command.local = local;
        command.block = block;
        stats_.edits_queued++;
        updateInstantStats(stats_, storage_);
        return BlockEditResult::Queued;
    }

    slot->data->blocks[blockIndex(local)] = block;
    slot->current_revision++;
    slot->dirty_for_disk = true;
    markBoundaryNeighborsDirty(coord, local);

    stats_.edits_applied++;
    updateInstantStats(stats_, storage_);
    return BlockEditResult::Applied;
}

void ChunkCache::applyPendingEdits(ChunkSlot& slot) {
    if (slot.pending_edits.empty() || slot.state != ChunkState::Resident || !slot.data) {
        slot.pending_edits.clear();
        return;
    }

    for (std::uint16_t i = 0; i < slot.pending_edits.count; i++) {
        const BlockEditCommand& command = slot.pending_edits.commands[i];
        if (command.kind != BlockEditKind::SetBlock ||
            !isValidLocalBlock(command.local)) {
            continue;
        }

        slot.data->blocks[blockIndex(command.local)] = command.block;
        slot.current_revision++;
        slot.dirty_for_disk = true;
        markBoundaryNeighborsDirty(slot.coord, command.local);
        stats_.edits_applied++;
    }

    slot.pending_edits.clear();
}

void ChunkCache::markBoundaryNeighborsDirty(const ChunkCoord coord,
                                            const LocalBlockCoord local) {
    const auto mark_neighbor = [this](const ChunkCoord neighbor_coord) {
        ChunkSlot* neighbor = findSlot(neighbor_coord);
        if (neighbor == nullptr ||
            neighbor->state != ChunkState::Resident ||
            !neighbor->data) {
            return;
        }

        neighbor->current_revision++;
    };

    if (local.x == 0) {
        mark_neighbor(ChunkCoord{coord.x - 1, coord.z});
    }
    if (local.x == BLOCK_X_SIZE - 1) {
        mark_neighbor(ChunkCoord{coord.x + 1, coord.z});
    }
    if (local.z == 0) {
        mark_neighbor(ChunkCoord{coord.x, coord.z - 1});
    }
    if (local.z == BLOCK_Z_SIZE - 1) {
        mark_neighbor(ChunkCoord{coord.x, coord.z + 1});
    }
}

void ChunkCache::markResidentHorizontalNeighborsDirty(const ChunkCoord coord) {
    const auto mark_neighbor = [this](const ChunkCoord neighbor_coord) {
        ChunkSlot* neighbor = findSlot(neighbor_coord);
        if (neighbor == nullptr ||
            neighbor->state != ChunkState::Resident ||
            !neighbor->data) {
            return;
        }

        neighbor->current_revision++;
    };

    mark_neighbor(ChunkCoord{coord.x - 1, coord.z});
    mark_neighbor(ChunkCoord{coord.x + 1, coord.z});
    mark_neighbor(ChunkCoord{coord.x, coord.z - 1});
    mark_neighbor(ChunkCoord{coord.x, coord.z + 1});
}

ChunkNeighborMasks ChunkCache::buildNeighborMasks(const ChunkCoord coord) const noexcept {
    ChunkNeighborMasks masks{};

    if (const ChunkSlot* neighbor = findSlot(ChunkCoord{coord.x - 1, coord.z});
        neighbor != nullptr &&
        neighbor->state == ChunkState::Resident &&
        neighbor->data) {
        fillXBorderMask(masks.neg_x, *neighbor->data, BLOCK_X_SIZE - 1);
    }

    if (const ChunkSlot* neighbor = findSlot(ChunkCoord{coord.x + 1, coord.z});
        neighbor != nullptr &&
        neighbor->state == ChunkState::Resident &&
        neighbor->data) {
        fillXBorderMask(masks.pos_x, *neighbor->data, 0);
    }

    if (const ChunkSlot* neighbor = findSlot(ChunkCoord{coord.x, coord.z - 1});
        neighbor != nullptr &&
        neighbor->state == ChunkState::Resident &&
        neighbor->data) {
        fillZBorderMask(masks.neg_z, *neighbor->data, BLOCK_Z_SIZE - 1);
    }

    if (const ChunkSlot* neighbor = findSlot(ChunkCoord{coord.x, coord.z + 1});
        neighbor != nullptr &&
        neighbor->state == ChunkState::Resident &&
        neighbor->data) {
        fillZBorderMask(masks.pos_z, *neighbor->data, 0);
    }

    return masks;
}

bool ChunkCache::trySubmitRelease(std::unique_ptr<ChunkData>& data) {
    if (!data) {
        return true;
    }

    Job job{};
    job.type = JobType::ReleaseChunkData;
    job.owned_data = std::move(data);

    if (work_queue_.tryPush(std::move(job))) {
        stats_.release_jobs_submitted++;
        return true;
    }

    data = std::move(job.owned_data);
    return false;
}

ChunkStreamingPipeline::ChunkStreamingPipeline(ChunkCache& cache,
                                               ChunkRenderer& renderer,
                                               ChunkResultQueue& result_queue)
    : cache_{cache},
      renderer_{renderer},
      result_queue_{result_queue} {
}

void ChunkStreamingPipeline::applyWorkerResults(const StreamBudget budget) {
    const std::size_t result_limit = std::min<std::size_t>(
        static_cast<std::size_t>(std::max(0, budget.max_results)),
        MAX_RESULTS_PER_FRAME
    );
    if (result_limit == 0) {
        return;
    }

    std::array<Result, MAX_RESULTS_PER_FRAME> result_buffer{};
    const std::size_t count =
        result_queue_.tryPopMany(std::span<Result>{result_buffer}.first(result_limit));

    for (std::size_t i = 0; i < count; i++) {
        Result& result = result_buffer[i];
        switch (result.type) {
        case ResultType::ChunkLoaded:
            cache_.applyLoadedChunk(std::move(result));
            break;
        case ResultType::ChunkPersisted:
            cache_.applyPersistedChunk(std::move(result), renderer_);
            break;
        case ResultType::ChunkMeshBuilt:
            cache_.applyBuiltMeshResult(std::move(result), renderer_);
            break;
        case ResultType::Failed:
            if (result.source_job == JobType::PersistChunk) {
                cache_.applyPersistedChunk(std::move(result), renderer_);
            } else {
                cache_.applyFailedJob(result);
            }
            break;
        }
    }
}

} // namespace torq
