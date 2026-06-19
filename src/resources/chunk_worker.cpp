#include "chunk_worker.h"

#include "chunk_mesh.h"
#include "region_store.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <utility>

namespace torq {
namespace {

void initializeResultFromJob(Result& result, const Job& job) {
    result = Result{};
    result.source_job = job.type;
    result.chunk = job.chunk;
    result.revision = job.revision;
}

} // namespace

ChunkStreamingJobExecutor::ChunkStreamingJobExecutor(RegionStore& region_store)
    : region_store_{region_store} {
}

bool ChunkStreamingJobExecutor::execute(Job&& job, Result* out_result) {
    if (out_result == nullptr) {
        return false;
    }

    initializeResultFromJob(*out_result, job);

    if (job.type == JobType::ReleaseChunkData) {
        return false;
    }

    if (job.type == JobType::BuildChunkMesh) {
        if (job.read_data == nullptr) {
            out_result->type = ResultType::Failed;
            out_result->error = CHUNK_CORRUPTED;
            return true;
        }

        out_result->cpu_mesh =
            ChunkMesher::build(*job.read_data, job.neighbor_masks);
        out_result->type = ResultType::ChunkMeshBuilt;
        out_result->error = NO_ERROR;
        return true;
    }

    if (job.type == JobType::PersistChunk) {
        if (!job.owned_data) {
            out_result->type = ResultType::Failed;
            out_result->error = CHUNK_CORRUPTED;
            return true;
        }

        try {
            region_store_.persistChunk(job.chunk, *job.owned_data);
            out_result->type = ResultType::ChunkPersisted;
            out_result->error = NO_ERROR;
        } catch (const RegionStoreException& e) {
            out_result->type = ResultType::Failed;
            out_result->error = e.code();
        } catch (const std::exception&) {
            out_result->type = ResultType::Failed;
            out_result->error = FILE_ERROR;
        }

        out_result->data = std::move(job.owned_data);
        return true;
    }

    if (job.type != JobType::LoadChunk) {
        out_result->type = ResultType::Failed;
        out_result->error = FILE_ERROR;
        return true;
    }

    try {
        out_result->data = region_store_.loadChunk(job.chunk);
        out_result->type = ResultType::ChunkLoaded;
        out_result->error = NO_ERROR;
    } catch (const RegionStoreException& e) {
        out_result->type = ResultType::Failed;
        out_result->error = e.code();
    } catch (const std::exception&) {
        out_result->type = ResultType::Failed;
        out_result->error = FILE_ERROR;
    }

    return true;
}

ChunkWorkerPool::ChunkWorkerPool(const std::size_t worker_count,
                                 ChunkJobExecutor& executor)
    : executor_{executor} {
    const std::size_t count = std::max<std::size_t>(worker_count, 1);
    workers_.reserve(count);
    for (std::size_t i = 0; i < count; i++) {
        workers_.emplace_back(&ChunkWorkerPool::workerLoop, this);
    }
}

ChunkWorkerPool::~ChunkWorkerPool() {
    shutdown();
}

bool ChunkWorkerPool::tryPush(Job&& job) {
    if (!running_.load(std::memory_order_acquire)) {
        return false;
    }

    if (!work_queue_.try_push(std::move(job))) {
        return false;
    }

    work_available_.notify_one();
    return true;
}

std::size_t ChunkWorkerPool::tryPopMany(std::span<Result> out) {
    return result_queue_.try_pop_many(out);
}

void ChunkWorkerPool::shutdown() noexcept {
    const bool was_running = running_.exchange(false, std::memory_order_acq_rel);
    if (!was_running) {
        return;
    }

    work_queue_.close();
    work_available_.notify_all();

    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    result_queue_.close();
}

void ChunkWorkerPool::workerLoop() {
    for (;;) {
        Job job{};
        if (work_queue_.try_pop(job)) {
            Result result{};
            bool has_result = false;
            try {
                has_result = executor_.execute(std::move(job), &result);
            } catch (const std::exception& e) {
                std::cerr << "Chunk worker job failed: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Chunk worker job failed with an unknown exception" << std::endl;
            }

            if (has_result) {
                publishResult(std::move(result));
            }
            continue;
        }

        if (!running_.load(std::memory_order_acquire) && !work_queue_.has_items()) {
            break;
        }

        std::unique_lock<std::mutex> lock(wake_mutex_);
        work_available_.wait(lock, [this] {
            return !running_.load(std::memory_order_acquire) ||
                   work_queue_.has_items();
        });
    }
}

void ChunkWorkerPool::publishResult(Result&& result) {
    while (running_.load(std::memory_order_acquire)) {
        if (result_queue_.try_push(std::move(result))) {
            return;
        }

        std::this_thread::yield();
    }
}

} // namespace torq
