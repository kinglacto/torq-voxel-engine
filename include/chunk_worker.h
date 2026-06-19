#pragma once

#include "chunk_cache.h"
#include "thread_safe_queue.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace torq {

class RegionStore;

class ChunkJobExecutor {
public:
    virtual ~ChunkJobExecutor() = default;

    [[nodiscard]] virtual bool execute(Job&& job, Result* out_result) = 0;
};

class ChunkStreamingJobExecutor final : public ChunkJobExecutor {
public:
    explicit ChunkStreamingJobExecutor(RegionStore& region_store);

    [[nodiscard]] bool execute(Job&& job, Result* out_result) override;

private:
    RegionStore& region_store_;
};

class ChunkWorkerPool final : public ChunkWorkQueue, public ChunkResultQueue {
public:
    explicit ChunkWorkerPool(std::size_t worker_count, ChunkJobExecutor& executor);
    ~ChunkWorkerPool() override;

    ChunkWorkerPool(const ChunkWorkerPool&) = delete;
    ChunkWorkerPool& operator=(const ChunkWorkerPool&) = delete;

    [[nodiscard]] bool tryPush(Job&& job) override;
    std::size_t tryPopMany(std::span<Result> out) override;

    void shutdown() noexcept;

private:
    void workerLoop();
    void publishResult(Result&& result);

    ChunkJobExecutor& executor_;
    BoundedMpmcQueue<Job, MAX_CHUNK_WORK_QUEUE_ENTRIES> work_queue_{};
    BoundedMpmcQueue<Result, MAX_CHUNK_RESULT_QUEUE_ENTRIES> result_queue_{};
    std::atomic<bool> running_{true};
    std::mutex wake_mutex_;
    std::condition_variable work_available_;
    std::vector<std::thread> workers_;
};

} // namespace torq
