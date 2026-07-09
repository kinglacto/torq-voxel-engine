# Chunk Streaming, Cache, And Region Store LLD

Status: implemented

This document is the low-level companion to
`design/chunk_streaming_pipeline_hld.md`. It describes the current code paths
in `include/chunk_streaming.h`, `include/chunk_cache.h`,
`src/resources/chunk_cache.cpp`, `include/region_store.h`,
`src/resources/region_store.cpp`, and `src/resources/chunk_worker.cpp`.

## Current Runtime Configuration

`main.cpp` constructs the cache with:

```cpp
ChunkCacheConfig{
    .active_radius = 3,
    .render_distance = 20,
    .keep_margin = 1
};
```

The current render window is `(2 * 20 + 1)^2 = 1681` chunk coordinates. The
keep window radius is `21`, so clean/dirty eviction is considered outside that
window.

Per-frame worker budgets in `main.cpp` are:

```cpp
StreamBudget{
    .max_results = 128,
    .max_load_jobs = 16,
    .max_persist_jobs = 4,
    .max_mesh_jobs = 16,
    .max_clean_evictions = 16
};
```

## Fixed Storage

Core fixed capacities live in `include/chunk_streaming.h`:

```cpp
MAX_CHUNK_SLOTS = 4096
MAX_TRANSIENT_CHUNK_SLOTS = 512
MAX_CHUNK_INDEX_ENTRIES = 8192
MAX_RESULTS_PER_FRAME = 256
MAX_CHUNK_WORK_QUEUE_ENTRIES = 1024
MAX_CHUNK_RESULT_QUEUE_ENTRIES = 1024
```

`ChunkCacheStorage` owns:

- `ChunkSlotPool`: fixed slot array, free stack, live slot list.
- `ChunkSlotIndex`: open-addressed coordinate index.
- `current_stream_tick`: monotonically increasing tick for retry cooldowns.

`ChunkSlot` is the authoritative resident CPU chunk record:

- `coord`, `region`, and region-local indices.
- `state`: `NotResident`, `Loading`, `Resident`, `PersistingForEviction`, or
  `Failed`.
- `std::unique_ptr<ChunkData> data`.
- `FixedEditBuffer<MAX_PENDING_EDITS_PER_CHUNK> pending_edits`.
- `current_revision`, `rendered_revision`, `mesh_job_revision`.
- `dirty_for_disk`.
- `next_retry_tick`.

## Frame Order

The game loop in `main.cpp` executes the streaming systems in this order:

```cpp
chunkCache.setCenterChunk(worldPositionChunkCoord(streamCenter));
streamingPipeline.applyWorkerResults(streamBudget);
chunkCache.tick(streamBudget, chunkRenderer);
```

`streamCenter` is `camera.cameraPos` in free-camera mode and
`player.feetPosition()` in physics-player mode.

`ChunkCache::tick` performs:

1. Increment stream tick.
2. Visit active/render Chebyshev rings and submit load jobs under budget.
3. Submit mesh jobs for resident dirty chunks under budget.
4. Process evictions outside the keep window.
5. Refresh instant stats.

All OpenGL upload/delete/draw ownership remains in `ChunkRenderer`; cache calls
renderer only from the main thread.

## Loading

`requestLoadIfNeeded` allocates a slot for a coordinate only if:

- no resident/loading slot exists,
- load budget remains,
- failed slot cooldown has expired if the coord previously failed,
- fixed slot pool and index have capacity.

On successful queue push:

```cpp
slot.state = ChunkState::Loading;
stats_.load_jobs_submitted++;
```

Worker execution:

```cpp
RegionStore::loadChunk(job.chunk)
```

The result returns either `ChunkLoaded` with `unique_ptr<ChunkData>` or `Failed`
with a `ChunkErrorCode`.

## Mesh Jobs And Read Lease

`submitMeshJobs` queues `BuildChunkMesh` only when:

```cpp
slot.state == Resident
slot.data != nullptr
slot.pending_edits.count == 0
slot.rendered_revision != slot.current_revision
slot.mesh_job_revision == INVALID_REVISION
```

The job stores:

```cpp
job.read_data = slot.data.get();
job.revision = slot.current_revision;
job.neighbor_masks = buildNeighborMasks(slot.coord);
```

After successful queue push:

```cpp
slot.mesh_job_revision = slot.current_revision;
```

That field is the read lease. While it is not `INVALID_REVISION`, the main
thread must not mutate, move, or erase `slot.data`.

Worker execution:

```cpp
ChunkMesher::build(*job.read_data, job.neighbor_masks)
```

The worker returns a moved `CpuChunkMesh`. OpenGL upload happens later on the
main thread through `ChunkRenderer::uploadMesh`.

## Applying Mesh Results

`ChunkCache::applyBuiltMeshResult` drops stale results when the slot is missing,
not resident, has no data, or the returned revision does not match the active
`mesh_job_revision`.

For a matching result:

1. Clear `mesh_job_revision`.
2. Upload the CPU mesh into `ChunkRenderer`.
3. On upload success, set `rendered_revision = result.revision`.
4. Apply pending edits after the lease ends.

If edits were queued while the mesh job was running, the just-uploaded mesh may
immediately become stale. That is intentional: a later normal mesh scan submits
the next remesh without creating a second scheduling path.

If a mesh build fails, the worker returns a failed result with the original chunk
revision. `ChunkCache::applyFailedJob` clears the matching read lease and applies
any queued edits, so shutdown persistence is not blocked behind a dead mesh job.

## Persistence And Eviction

Eviction is considered only outside the keep window.

Dirty resident chunks are persisted before slot free:

```cpp
job.type = PersistChunk;
job.owned_data = std::move(slot.data);
slot.state = PersistingForEviction;
```

Persist jobs return the moved data in the result. On success the cache deletes
the renderer mesh and frees the slot. On failure, the data is restored to the
slot so edits are not lost.

Clean resident chunks submit `ReleaseChunkData`, which moves the `ChunkData`
unique pointer to the worker queue so heap destruction does not happen on the
main thread. If that queue push fails, eviction is skipped for the frame.

When a chunk loses resident data through persist or clean eviction, resident
horizontal neighbors are marked dirty because their meshes may have hidden faces
against the now-missing mask source.

## Shutdown Persistence Flush

Normal eviction is intentionally window-bound, so dirty chunks inside the keep
window are not persisted merely because the process is about to exit. Shutdown
therefore runs an explicit cache-level flush before `ChunkWorkerPool::shutdown`.

The game loop exits, then `main.cpp` calls:

```cpp
flushChunkPersistenceForShutdown(streamingPipeline, chunkCache);
chunkWorkers.shutdown();
```

The flush loop uses a shutdown-only budget:

```cpp
StreamBudget{
    .max_results = MAX_RESULTS_PER_FRAME,
    .max_load_jobs = 0,
    .max_persist_jobs = 64,
    .max_mesh_jobs = 0,
    .max_clean_evictions = 0
};
```

This does not submit new load jobs, mesh jobs, or clean evictions. It only:

1. Drains completed worker results while workers are still running.
2. Waits for in-flight mesh jobs to clear their read leases.
3. Applies pending block edits after those leases end.
4. Submits persist jobs for every dirty resident chunk, including chunks still
   inside the keep window.
5. Drains persist results until no dirty resident chunk, pending edit, mesh read
   lease, or persist job remains.

`ChunkCache::submitShutdownPersistJobs` reuses the same persist job path as
eviction:

```cpp
job.type = PersistChunk;
job.owned_data = std::move(slot.data);
slot.state = PersistingForEviction;
```

`ChunkCache::applyPersistedChunk` restores the moved data to the resident slot
when the chunk is still inside the keep window, and clears `dirty_for_disk` only
after the region write succeeds.

If persistence repeatedly fails or the flush cannot complete within the shutdown
iteration cap, `main.cpp` prints the remaining dirty/persisting/mesh/pending
counts. That is intentionally noisy because silent exit would otherwise look
like a successful save. Under normal operation, worker shutdown now happens only
after dirty live chunks have been persisted and returned through the result
queue.

## Region Store

`RegionStore` is a worker-called service, not a dedicated thread.

The region path is:

```text
<CACHE_DIR>/chunks/r.<region_x>.<region_z>.region
```

`RegionStore` owns a small `regions_` metadata map protected by
`regions_mutex_`. Each `RegionSlot` has:

- region coordinate,
- parsed 1024-entry header,
- header-loaded flag,
- per-region mutex.

`loadChunk`:

1. Convert chunk coordinate to region coordinate and local index.
2. Get or create the `RegionSlot`.
3. Ensure the header/region file exists, generating the full region if needed.
4. Read the compressed payload.
5. Decompress into a worker-allocated `ChunkData`.

`persistChunk`:

1. Ensure the region exists.
2. Compress the chunk block payload with zlib.
3. Append the compressed payload.
4. Rewrite the header entry for that local chunk.

Old compressed payloads are not compacted in-place. The current store favors
simple append-and-header-update semantics over region file compaction.

## Main-Thread Constraints

The implemented path preserves the HLD constraints:

- No main-thread waiting for workers.
- No renderer loading or meshing.
- No main-thread chunk data locks.
- No chunk block array copies on the main thread.
- No dynamic slot allocation in the render loop.
- Bounded worker result draining.
- Queue-full cases skip work instead of blocking.
