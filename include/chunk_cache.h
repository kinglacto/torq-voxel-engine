#pragma once

#include "chunk_streaming.h"

#include <memory>
#include <span>

namespace torq {

class ChunkRenderer;

class ChunkWorkQueue {
public:
    virtual ~ChunkWorkQueue() = default;
    [[nodiscard]] virtual bool tryPush(Job&& job) = 0;
};

class ChunkResultQueue {
public:
    virtual ~ChunkResultQueue() = default;
    virtual std::size_t tryPopMany(std::span<Result> out) = 0;
};

class ChunkCache {
public:
    ChunkCache(ChunkCacheConfig config, ChunkWorkQueue& work_queue);

    void setCenterChunk(ChunkCoord center) noexcept;
    void tick(StreamBudget budget, ChunkRenderer& renderer);

    void applyLoadedChunk(Result&& result);
    void applyPersistedChunk(Result&& result, ChunkRenderer& renderer);
    void applyFailedJob(const Result& result);
    void applyBuiltMeshResult(Result&& result, ChunkRenderer& renderer);

    [[nodiscard]] bool tryGetBlock(WorldBlockCoord pos, BlockData* out) const;
    [[nodiscard]] bool isSolidForPhysics(WorldBlockCoord pos) const;
    [[nodiscard]] BlockEditResult setBlock(WorldBlockCoord pos, BlockData block);

    [[nodiscard]] const ChunkCacheStorage& storage() const noexcept;
    [[nodiscard]] const ChunkCacheStats& stats() const noexcept;

private:
    [[nodiscard]] ChunkSlot* findSlot(ChunkCoord coord) noexcept;
    [[nodiscard]] const ChunkSlot* findSlot(ChunkCoord coord) const noexcept;
    [[nodiscard]] ChunkSlot* allocateSlot(ChunkCoord coord);
    void freeSlot(SlotId id);

    [[nodiscard]] bool outsideKeepWindow(ChunkCoord coord) const noexcept;
    [[nodiscard]] bool insideActiveWindow(ChunkCoord coord) const noexcept;

    void requestLoadIfNeeded(ChunkCoord coord, int& remaining_load_budget);
    void submitMeshJobs(int& remaining_mesh_budget);
    void processEvictions(StreamBudget budget, ChunkRenderer& renderer);
    void applyPendingEdits(ChunkSlot& slot);
    void markBoundaryNeighborsDirty(ChunkCoord coord, LocalBlockCoord local);
    void markResidentHorizontalNeighborsDirty(ChunkCoord coord);
    [[nodiscard]] ChunkNeighborMasks buildNeighborMasks(ChunkCoord coord) const noexcept;

    [[nodiscard]] bool trySubmitRelease(std::unique_ptr<ChunkData>& data);

    ChunkCacheConfig config_{};
    ChunkCoord center_chunk_{};
    ChunkWorkQueue& work_queue_;
    ChunkCacheStorage storage_{};
    ChunkCacheStats stats_{};
};

class ChunkStreamingPipeline {
public:
    ChunkStreamingPipeline(ChunkCache& cache,
                           ChunkRenderer& renderer,
                           ChunkResultQueue& result_queue);

    void applyWorkerResults(StreamBudget budget);

private:
    ChunkCache& cache_;
    ChunkRenderer& renderer_;
    ChunkResultQueue& result_queue_;
};

} // namespace torq
