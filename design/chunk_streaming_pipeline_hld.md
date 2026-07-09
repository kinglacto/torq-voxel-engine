# Chunk Streaming Pipeline HLD

Status: implemented baseline; keep updated as the architecture source of truth

This document defines Torq's chunk streaming, region persistence, CPU meshing, GPU mesh ownership, block editing, and gameplay-query pipeline.

The goal is a large sparse voxel world with simple gameplay and a small number of explicit systems. Disk IO, region generation, compression, decompression, and CPU mesh generation must not block the render loop. OpenGL work stays on the main thread.

## Execution Model

There are two execution domains.

`Main thread`

- Owns `ChunkCacheStorage`.
- Owns `ChunkRendererStorage`.
- Runs gameplay block queries and block edits.
- Applies completed worker results.
- Calls renderer upload/delete/draw functions.
- Performs every OpenGL call.

`Worker pool`

- Owns no long-lived chunk-cache state.
- Owns no renderer state.
- Executes jobs from one shared work queue.
- Calls `RegionStore` for persistence/generation work.
- Calls `ChunkMesher` for CPU mesh generation.

`RegionStore` is not a separate event-loop thread. It is a thread-safe service called by worker jobs. Its internal state is protected by per-region locks.

Cross-thread communication uses exactly two queues:

```cpp
work_queue:   main thread -> worker pool
result_queue: worker pool -> main thread
```

There is no renderer worker queue. The renderer is OpenGL-facing only:

```cpp
bool uploaded = renderer.uploadMesh(coord, revision, std::move(cpu_mesh));
renderer.deleteMesh(coord);
renderer.draw();
```

The cache and renderer interact directly only on the main thread through the frame pipeline. The renderer never loads chunks, never saves chunks, never builds CPU meshes, and never reads `ChunkData`.

Hard main-thread rules:

- The main thread never locks chunk block data.
- The main thread never waits for worker jobs.
- The main thread never copies chunk block arrays.
- The main thread never dynamically allocates chunk slots.
- The main thread never heap-allocates or heap-frees `ChunkData`.
- Main-owned streaming containers are fixed-capacity or fully preallocated before the game render loop starts.
- Work queues expose nonblocking `try_push` and bounded `try_pop` APIs.
- If a queue is full, the main thread skips submitting more work that frame.

RegionStore may use mutexes internally, but RegionStore is called only by workers. RegionStore locks must never be acquired by the main thread.

## World Constants

Keep the current dimensions:

```cpp
BLOCK_X_SIZE = 16
BLOCK_Y_SIZE = 16
BLOCK_Z_SIZE = 16
CHUNKS_PER_REGION_SIDE = 32
BLOCKS_PER_REGION_SIDE = 512
```

One chunk stores `4096` blocks.

One region stores `1024` chunks.

One region covers `512 x 512` horizontal blocks.

## Ownership Summary

`ChunkCache`

- Owns CPU block data for chunks near the player.
- Stores `ChunkSlot` records in a fixed pool with a fixed-capacity coordinate index.
- Submits load, persist, and mesh-build jobs.
- Applies worker results on the main thread.
- Provides block queries and block edits.

`ChunkRenderer`

- Owns GPU meshes only.
- Stores `ChunkMesh` objects in a fixed pool with a fixed-capacity coordinate index.
- Uploads CPU mesh buffers into OpenGL buffers.
- Deletes GPU meshes when chunks leave the cache.
- Draws uploaded meshes.

`RegionStore`

- Owns region-file IO and deterministic region generation.
- Caches only small region metadata: header, file existence, and per-region lock.
- Does not cache chunk block data.
- Does not own meshes.

`ChunkMesher`

- Pure CPU helper.
- Converts `ChunkData` into moved CPU mesh buffers.
- Runs inside worker jobs.
- Performs no OpenGL calls.

## Coordinates

Use explicit coordinate structs:

```cpp
struct ChunkCoord {
    int x;
    int z;
};

struct RegionCoord {
    int x;
    int z;
};

struct WorldBlockCoord {
    int x;
    int y;
    int z;
};

struct LocalBlockCoord {
    int x;
    int y;
    int z;
};
```

Conversions must support negative coordinates:

```cpp
chunk_x = floor(world_x / 16)
chunk_z = floor(world_z / 16)
local_x = positive_mod(world_x, 16)
local_z = positive_mod(world_z, 16)

region_x = floor(chunk_x / 32)
region_z = floor(chunk_z / 32)
region_local_chunk_x = positive_mod(chunk_x, 32)
region_local_chunk_z = positive_mod(chunk_z, 32)
```

Missing region file means "not generated/persisted yet", not "outside the world".

A chunk is never semantically missing. For any coordinate, data is either resident, persisted in a region file, generatable from seed, or unavailable because of an explicit error.

## Cache Configuration

Distances are fixed cache configuration:

```cpp
struct ChunkCacheConfig {
    int active_radius;    // block edit / physics / raycast radius
    int render_distance;  // chunks that must be loaded for rendering
    int keep_margin;      // retention margin outside render distance
};
```

Struct defaults:

```cpp
active_radius = 5
render_distance = 10
keep_margin = 2
```

Current `main.cpp` game configuration:

```cpp
active_radius = 3
render_distance = 20
keep_margin = 1
```

Startup validation:

```cpp
keep_radius = render_distance + keep_margin
required_resident_slots = (2 * keep_radius + 1) * (2 * keep_radius + 1)
required_slots = required_resident_slots + MAX_TRANSIENT_CHUNK_SLOTS

assert(required_slots <= MAX_CHUNK_SLOTS)
assert(MAX_CHUNK_SLOTS * 2 <= MAX_CHUNK_INDEX_ENTRIES)
assert(MAX_CHUNK_MESHES <= MAX_CHUNK_SLOTS)
```

Per frame, the cache receives only the player center chunk:

```cpp
void setCenterChunk(ChunkCoord center);
```

Distance uses Chebyshev distance:

```cpp
distance = max(abs(chunk.x - center.x), abs(chunk.z - center.z))
```

Bands:

```text
active window: distance <= active_radius
render window: distance <= render_distance
keep window:   distance <= render_distance + keep_margin
evict zone:    distance >  render_distance + keep_margin
```

Residency rules:

- Every chunk in the render window is a load target.
- The active window is a higher-priority subset of the render window.
- The keep window is a retention margin only.
- Chunks inside keep but outside render are not loaded just because they are inside keep.
- Resident chunks inside keep are retained to reduce load/evict churn.
- Chunks outside keep are evicted when clean, or persisted and then evicted when dirty.

## Chunk Data

Chunk block storage is worker-allocated heap data and uniquely owned by the chunk slot while resident:

```cpp
struct ChunkData {
    std::array<blockData, 4096> blocks;
};
```

Rules:

- `ChunkData` is stored as `std::unique_ptr<ChunkData>`.
- `ChunkData` is never copied.
- `ChunkData` heap allocation happens in worker jobs, not on the main thread.
- Main thread mutates `ChunkData` only when no worker is reading it.
- Mesh workers receive a borrowed `const ChunkData*`.
- Persist workers receive moved `std::unique_ptr<ChunkData>`.
- Release workers receive moved `std::unique_ptr<ChunkData>` and destroy it off the main thread.
- Slot state guarantees pointer lifetime and exclusive mutation.
- No mutex, spinlock, or shared lock exists inside `ChunkData`.

Worker mesh jobs read chunk data through a read lease. A read lease means:

```text
slot.data remains alive
slot.data is not moved
slot is not erased
main thread does not mutate slot.data
```

The read lease ends only when the mesh result is applied or dropped on the main thread.

## Pending Block Edits

Block edits submitted while a worker has a read lease on the chunk are not applied immediately. They are stored in a fixed-size per-slot edit buffer.

```cpp
enum class BlockEditKind {
    SetBlock
};

struct BlockEditCommand {
    BlockEditKind kind;
    LocalBlockCoord local;
    blockData block;
};

template<size_t N>
struct FixedEditBuffer {
    std::array<BlockEditCommand, N> commands;
    uint16_t count;
};
```

Initial capacity:

```cpp
MAX_PENDING_EDITS_PER_CHUNK = 64
```

Rules:

- If an edit is accepted, it is guaranteed to apply.
- If the buffer is full, `setBlock` returns `EditQueueFull` and changes nothing.
- Pending edits are applied only on the main thread.
- Pending edits are applied after the worker read lease ends.
- Main thread queries see the currently applied block data; accepted pending edits become visible after they are applied.

## ChunkCache Storage

Chunk slots are not allocated with `new`, `make_unique`, or `std::unordered_map` insertion during the render loop.

The cache owns a fixed slot pool and a fixed-capacity coordinate index:

```cpp
using SlotId = uint32_t;

inline constexpr SlotId INVALID_SLOT = UINT32_MAX;
inline constexpr uint32_t MAX_CHUNK_SLOTS = 4096;
inline constexpr uint32_t MAX_TRANSIENT_CHUNK_SLOTS = 512;
inline constexpr uint32_t MAX_CHUNK_INDEX_ENTRIES = 8192;

enum class IndexState : uint8_t {
    Empty,
    Occupied,
    Tombstone
};

struct ChunkSlotIndexEntry {
    ChunkCoord coord;
    SlotId slot;
    IndexState state;
};

struct ChunkSlotPool {
    std::array<ChunkSlot, MAX_CHUNK_SLOTS> slots;
    std::array<SlotId, MAX_CHUNK_SLOTS> free_stack;
    uint32_t free_count;

    std::array<SlotId, MAX_CHUNK_SLOTS> live_slots;
    uint32_t live_count;
};

struct ChunkSlotIndex {
    std::array<ChunkSlotIndexEntry, MAX_CHUNK_INDEX_ENTRIES> entries;
    uint32_t occupied_count;
    uint32_t tombstone_count;
};

struct ChunkCacheStorage {
    ChunkSlotPool pool;
    ChunkSlotIndex index;
    uint64_t current_stream_tick;
};
```

Header definition order must be: shared constants and ids, `ChunkState`, `ChunkSlot`, then `ChunkSlotPool` / `ChunkSlotIndex` / `ChunkCacheStorage`.

Initialization happens before the render loop:

```cpp
for id in 0..MAX_CHUNK_SLOTS:
    pool.free_stack[pool.free_count++] = id

for entry in index.entries:
    entry.state = IndexState::Empty
```

Lookup:

```cpp
SlotId id = index.find(coord);
ChunkSlot* slot = id != INVALID_SLOT ? &pool.slots[id] : nullptr;
```

Allocation:

```cpp
if pool.free_count == 0:
    return SlotPoolFull

SlotId id = pool.free_stack[--pool.free_count];
ChunkSlot* slot = &pool.slots[id];
reset slot fields
slot.id = id
slot.live_index = pool.live_count
pool.live_slots[pool.live_count++] = id
index.insert(coord, id)
```

Removal:

```cpp
assert(slot.data == nullptr)
assert(slot.mesh_job_revision == INVALID_REVISION)
assert(slot.pending_edits.count == 0)

freed_id = slot.id
index.erase(slot.coord)

remove_index = slot.live_index
last_id = pool.live_slots[pool.live_count - 1]
pool.live_count -= 1

if remove_index != pool.live_count:
    pool.live_slots[remove_index] = last_id
    pool.slots[last_id].live_index = remove_index

reset slot fields
pool.free_stack[pool.free_count++] = freed_id
```

Eviction scans iterate `pool.live_slots[0..live_count)`, not hash buckets.

The index uses open addressing with linear or Robin Hood probing. It does not allocate. If tombstones exceed a fixed threshold, the main thread rebuilds the index in-place over the fixed `entries` array.

There is no vector-backed chunk-slot storage and no per-slot heap allocation.

## ChunkSlot

```cpp
enum class ChunkState {
    NotResident,
    Loading,
    Resident,
    PersistingForEviction,
    Failed
};

inline constexpr uint64_t INVALID_REVISION = UINT64_MAX;

struct ChunkSlot {
    SlotId id;
    uint32_t live_index;

    ChunkCoord coord;
    RegionCoord region;
    int region_local_x;
    int region_local_z;

    ChunkState state;
    ChunkErrorCode last_error;

    std::unique_ptr<ChunkData> data;
    FixedEditBuffer<MAX_PENDING_EDITS_PER_CHUNK> pending_edits;

    uint64_t current_revision;
    uint64_t rendered_revision;
    uint64_t mesh_job_revision;
    bool dirty_for_disk;

    uint64_t next_retry_tick;
};
```

Field meaning:

- `state`: chunk residency/load/persist state.
- `id`: stable slot-pool id while the slot is allocated.
- `live_index`: position inside `pool.live_slots` for O(1) removal.
- `data`: owned block data while resident; null while loading, failed, or persisting after eviction has started.
- `pending_edits`: accepted edits waiting for a worker read lease to finish.
- `current_revision`: CPU mesh-source version, incremented when this chunk's own block data changes or when an adjacent boundary edit can change this chunk's visible faces.
- `rendered_revision`: GPU mesh version known uploaded by renderer.
- `mesh_job_revision`: revision currently queued/running for CPU meshing, or `INVALID_REVISION`.
- `dirty_for_disk`: true after block edits; false after load/generation or successful persist.
- `next_retry_tick`: cache-tick threshold before retrying a failed load.

`mesh_job_revision` is a job throttle, not another data version. Without it, the cache would submit the same mesh job every frame while an older mesh job is still running.

## Data Movement Rules

Large world data is pointer-moved or buffer-moved.

- `ChunkData` is owned through `std::unique_ptr<ChunkData>`.
- `RegionData` is temporary heap data during region generation.
- Load jobs return `std::unique_ptr<ChunkData>`.
- Persist jobs own moved `std::unique_ptr<ChunkData>`.
- Release jobs own moved `std::unique_ptr<ChunkData>`.
- Mesh jobs borrow `const ChunkData*` under a read lease.
- CPU mesh buffers are moved from worker result to renderer upload.
- Hot-path collection APIs write into caller-provided buffers.

No `ChunkData` or `RegionData` copy is part of this design.

## Block Edit Rule

Block edits are allowed only for resident active chunks:

```cpp
slot.state == ChunkState::Resident
distance(slot.coord, center_chunk) <= active_radius
```

If no worker has a read lease:

```cpp
modify slot.data->blocks
slot.current_revision += 1
slot.dirty_for_disk = true
mark affected resident neighbor meshes dirty
return EditApplied
```

If a mesh worker has a read lease:

```cpp
slot.mesh_job_revision != INVALID_REVISION

if slot.pending_edits has space:
    append BlockEditCommand
    return EditQueued
else:
    return EditQueueFull
```

Do not mutate `slot.data` while a worker has a read lease.

Queued edit application:

```cpp
for edit in slot.pending_edits:
    modify slot.data->blocks
    slot.current_revision += 1
    slot.dirty_for_disk = true
    mark affected resident neighbor meshes dirty

clear slot.pending_edits
```

An applied edit makes the edited chunk need both remesh and persist-on-evict:

```cpp
slot.current_revision != slot.rendered_revision
slot.dirty_for_disk == true
```

Boundary edits also mark neighboring resident meshes dirty:

- `local_x == 0`: neighbor `{chunk.x - 1, chunk.z}`
- `local_x == 15`: neighbor `{chunk.x + 1, chunk.z}`
- `local_z == 0`: neighbor `{chunk.x, chunk.z - 1}`
- `local_z == 15`: neighbor `{chunk.x, chunk.z + 1}`

For each affected resident neighbor:

```cpp
neighbor.current_revision += 1;
```

Do not modify the neighbor's `dirty_for_disk`. Its block data did not change, but its mesh source changed because one of its boundary faces may now be visible or hidden.

Do not modify the neighbor's `rendered_revision`. The old mesh may still be drawable while the new mesh is being built. The dirty condition is `neighbor.current_revision != neighbor.rendered_revision`.

Do not modify the neighbor's `mesh_job_revision`. If a mesh job is already reading that neighbor, `mesh_job_revision` is the active read lease and must remain valid until the worker result returns.

## Mesh Build Rule

A resident chunk needs a CPU mesh job when:

```cpp
slot.state == ChunkState::Resident &&
slot.pending_edits.count == 0 &&
slot.rendered_revision != slot.current_revision &&
slot.mesh_job_revision == INVALID_REVISION
```

Mesh job:

```cpp
struct BuildChunkMeshJob {
    ChunkCoord coord;
    const ChunkData* read_data;
    uint64_t revision;
};
```

Submission:

```cpp
Job job;
job.type = JobType::BuildChunkMesh;
job.chunk = slot.coord;
job.read_data = slot.data.get();
job.revision = slot.current_revision;

if work_queue.try_push(std::move(job)):
    slot.mesh_job_revision = slot.current_revision;
```

`mesh_job_revision != INVALID_REVISION` is the worker read lease. While this is true, `slot.data` must not be mutated, moved, or erased.

Worker flow:

```cpp
CpuChunkMesh cpu_mesh = ChunkMesher::build(*job.read_data);
return ChunkMeshBuilt { job.coord, job.revision, std::move(cpu_mesh) };
```

Main-thread completion:

```cpp
if index.find(result.coord) returns no slot:
    drop result.cpu_mesh

else if slot.mesh_job_revision != result.revision:
    drop result.cpu_mesh

else:
    slot.mesh_job_revision = INVALID_REVISION

    if slot.state == ChunkState::Resident &&
       renderer.uploadMesh(result.coord, result.revision, std::move(result.cpu_mesh)):
        slot.rendered_revision = result.revision
    else:
        drop result.cpu_mesh

    if slot.state == ChunkState::Resident &&
       slot.pending_edits.count > 0:
        apply pending edits on main thread
```

If a mesh result matches the active read lease, try to upload it even when newer accepted edits are waiting. A successful upload represents the newest completed visible revision. After the upload attempt, queued edits are applied immediately and `current_revision` advances.

If upload fails because renderer mesh capacity is exhausted, keep `rendered_revision` unchanged. The chunk remains dirty and the normal mesh scan can retry later.

No remesh is submitted directly from mesh-result completion. If `current_revision != rendered_revision` after pending edits are applied, the normal mesh scan in `cache.tick` submits the next mesh job using the same global mesh budget and scan order as every other dirty chunk. This avoids a second scheduling path and prevents edit-heavy chunks from starving ordinary remesh work.

The first two drop paths are defensive invalid-result paths. Under the read-lease invariant, a normal edit-contention result still has a matching `mesh_job_revision` and must be uploaded before pending edits are applied.

This design does not store edit history for surgical VBO patches. One block edit remeshes the affected chunk, plus resident neighbor chunks whose exposed faces may change. This keeps correctness simple and works with both simple face meshing and greedy meshing.

## Persist-On-Evict Rule

ChunkCache does not write active resident chunks to disk in the background.

Disk writes happen only when a dirty chunk is selected for eviction.

Dirty eviction is two-phase:

1. Start persist and keep the `ChunkSlot` allocated in the slot pool.
2. Wait for worker result.
3. Erase the slot only after persist success.
4. On persist failure, move the data pointer back into the slot and keep `dirty_for_disk = true`.

This prevents edited block data from being lost if region IO fails.

Start persist-on-evict when:

```cpp
slot.state == ChunkState::Resident
outside_keep_window(slot.coord)
slot.dirty_for_disk == true
slot.mesh_job_revision == INVALID_REVISION
slot.pending_edits.count == 0
```

Start operation:

```cpp
Job job;
job.type = JobType::PersistChunk;
job.chunk = slot.coord;
job.owned_data = std::move(slot.data);
job.revision = slot.current_revision;

if work_queue.try_push(std::move(job)):
    slot.state = ChunkState::PersistingForEviction;
else:
    slot.data = std::move(job.owned_data)
```

While persisting:

```cpp
slot remains allocated in the slot pool
slot.data == nullptr
slot.state == ChunkState::PersistingForEviction
```

The slot is not editable or renderable until the persist result is applied.

Persist success:

```cpp
slot.dirty_for_disk = false;

if outside_keep_window(slot.coord):
    if tryReleaseChunkData(result.data):
        renderer.deleteMesh(slot.coord)
        freeSlot(slot.id)
    else:
        slot.data = std::move(result.data)
        slot.state = ChunkState::Resident
else:
    slot.data = std::move(result.data)
    slot.state = ChunkState::Resident
```

Persist failure:

```cpp
slot.data = std::move(result.data);
slot.state = ChunkState::Resident;
slot.dirty_for_disk = true;
slot.last_error = result.error;
```

Dirty chunks are never dropped without being persisted.

## Loading Rule

A load request means "make this chunk resident".

Request path:

1. Main thread creates or fetches the `ChunkSlot`.
2. If state is `NotResident` or retryable `Failed`, try to submit a `LoadChunk` job.
3. Set state to `Loading` only after `work_queue.try_push` succeeds.
4. Worker calls `RegionStore::loadChunk(coord)`.
5. Main thread applies the result.

Load success:

```cpp
slot.state = ChunkState::Resident;
slot.data = std::move(result.data);
slot.current_revision = 0;
slot.rendered_revision = INVALID_REVISION;
slot.mesh_job_revision = INVALID_REVISION;
slot.pending_edits.count = 0;
slot.dirty_for_disk = false;
```

Load failure:

```cpp
slot.state = ChunkState::Failed;
slot.last_error = result.error;
slot.next_retry_tick = current_stream_tick + LOAD_RETRY_COOLDOWN_TICKS;
```

Failed chunks inside render distance retry after a cache-tick cooldown, not every frame.

Retry cooldown uses `current_stream_tick`, not wall-clock time. If FPS is low, retry happens later in real time, which intentionally reduces pressure while the system is overloaded.

## RegionStore

RegionStore is the persistence and generation layer.

It owns:

```cpp
struct RegionSlot {
    RegionCoord coord;
    std::array<HeaderEntry, 1024> header;
    bool header_loaded;
    bool file_exists;
    std::mutex file_mutex;
};

struct RegionStore {
    fs::path region_dir;
    std::mutex regions_mutex;
    std::unordered_map<RegionCoord, std::unique_ptr<RegionSlot>, RegionCoordHash> regions;
};
```

RegionStore caches only:

- parsed region headers,
- file existence status,
- per-region mutexes.

RegionStore does not cache:

- `ChunkData`,
- `RegionData`,
- `ChunkMesh`,
- resident chunks,
- render meshes.

Region file path:

```text
assets/chunks/r.<region_x>.<region_z>.region
```

Region file format:

```cpp
struct HeaderEntry {
    uint32_t offset;
    uint32_t length;
};

HeaderEntry header[1024];
compressed chunk payloads appended after header
```

Header index:

```cpp
index = region_local_chunk_x * 32 + region_local_chunk_z;
```

### LoadChunk In RegionStore

```cpp
std::unique_ptr<ChunkData> loadChunk(ChunkCoord coord);
```

Algorithm:

1. Convert chunk coord to region coord and local chunk index.
2. Get/create `RegionSlot` under `regions_mutex`.
3. Lock `RegionSlot::file_mutex`.
4. If header is not loaded:
   - if region file exists, read header into `RegionSlot::header`;
   - else generate and write the full region file, then store header.
5. Read compressed payload for requested chunk.
6. Decompress payload into `std::unique_ptr<ChunkData>`.
7. Return the chunk.

Missing region generation:

1. Allocate temporary `std::unique_ptr<RegionData>`.
2. Generate all 1024 chunks deterministically from seed.
3. Compress/write all 1024 chunks into the region file.
4. Store the generated header in `RegionSlot::header`.
5. Free the temporary `RegionData`.

### PersistChunk In RegionStore

```cpp
void persistChunk(ChunkCoord coord, const ChunkData& data);
```

Algorithm:

1. Convert chunk coord to region coord and local chunk index.
2. Get/create `RegionSlot` under `regions_mutex`.
3. Lock `RegionSlot::file_mutex`.
4. Ensure region file/header exists; generate missing region if necessary.
5. Compress `ChunkData`.
6. Append compressed payload to region file.
7. Update the matching header entry in memory.
8. Rewrite the header entry/header on disk.

The persist worker owns the moved `std::unique_ptr<ChunkData>` while calling `RegionStore::persistChunk`. RegionStore does not keep that pointer. After the call, the worker returns the same moved pointer to the main thread in the result.

Old payloads become dead bytes. Region compaction is not part of this design.

Use explicit reads/writes. Normal OS page cache handles file-page caching.

## Work Queues

Workers do:

- region file IO,
- compression,
- decompression,
- region generation,
- CPU chunk meshing,
- retired `ChunkData` destruction.

Workers do not:

- touch OpenGL,
- mutate `ChunkCacheStorage`,
- mutate `ChunkRendererStorage`.

Queue operations visible to the main thread are nonblocking:

```cpp
bool work_queue.try_push(Job&& job);
size_t result_queue.try_pop_many(std::span<Result> out);
```

`work_queue.try_push` must check capacity before moving from `job`. On failure, the caller still owns all pointers inside `job`.

Request:

```cpp
enum class JobType {
    LoadChunk,
    PersistChunk,
    BuildChunkMesh,
    ReleaseChunkData
};

struct Job {
    JobType type;
    ChunkCoord chunk;
    uint64_t revision;

    const ChunkData* read_data;              // BuildChunkMesh only
    std::unique_ptr<ChunkData> owned_data;   // PersistChunk / ReleaseChunkData only
};
```

Result:

```cpp
enum class ResultType {
    ChunkLoaded,
    ChunkPersisted,
    ChunkMeshBuilt,
    Failed
};

struct Result {
    ResultType type;
    JobType source_job;
    ChunkCoord chunk;
    ChunkErrorCode error;
    std::unique_ptr<ChunkData> data;
    uint64_t revision;
    CpuChunkMesh cpu_mesh;
};
```

`data` is used by load and persist results.

`cpu_mesh` is used by mesh-build results and is moved into renderer upload or dropped if stale.

Persist jobs return `data` on both success and failure. The cache either moves the pointer back into the slot or moves it into a `ReleaseChunkData` job before freeing the slot.

Job payload rules:

- `LoadChunk`: no input data; result owns `std::unique_ptr<ChunkData>`.
- `PersistChunk`: job owns `std::unique_ptr<ChunkData>`; result returns the same pointer.
- `BuildChunkMesh`: job borrows `const ChunkData*`; result owns `CpuChunkMesh`.
- `ReleaseChunkData`: job owns `std::unique_ptr<ChunkData>`; worker destroys it and returns no result.

Release helper:

```cpp
bool tryReleaseChunkData(std::unique_ptr<ChunkData>& data) {
    Job job;
    job.type = JobType::ReleaseChunkData;
    job.owned_data = std::move(data);

    if work_queue.try_push(std::move(job)):
        return true

    data = std::move(job.owned_data)
    return false
}
```

## Per-Frame Pipeline

API:

```cpp
void setCenterChunk(ChunkCoord center);
void tick(StreamBudget budget);
```

Budget:

```cpp
struct StreamBudget {
    int max_results;
    int max_load_jobs;
    int max_persist_jobs;
    int max_mesh_jobs;
    int max_clean_evictions;
};
```

Main-thread frame order:

```cpp
cache.setCenterChunk(center);
pipeline.applyWorkerResults(cache, renderer, budget);
cache.tick(budget);
renderer.draw();
```

`pipeline.applyWorkerResults` is main-thread orchestration:

```cpp
Result result_buffer[MAX_RESULTS_PER_FRAME];
result_limit = min(budget.max_results, MAX_RESULTS_PER_FRAME);
count = result_queue.try_pop_many(span(result_buffer).first(result_limit));

for result in result_buffer[0..count):
    if result is ChunkLoaded:
        cache.applyLoadedChunk(result)

    if result is ChunkPersisted:
        outcome = cache.applyPersistedChunk(result)
        if outcome.delete_mesh:
            renderer.deleteMesh(result.chunk)

    if result is ChunkMeshBuilt:
        pipeline.applyBuiltMeshResult(cache, renderer, result)

    if result is Failed:
        cache.applyFailedJob(result)
```

`cache.tick` order:

1. Increment `current_stream_tick`.
2. Request active-window chunks near-to-far.
3. Request remaining render-window chunks near-to-far.
4. Submit mesh jobs for resident chunks needing remesh.
5. Scan `pool.live_slots` for chunks outside keep window.
6. Submit persist jobs for dirty outside-keep chunks that have no read lease or pending edits.
7. Evict clean outside-keep chunks that have no read lease.

Each submission/removal phase stops when its matching budget counter is exhausted:

```text
load requests:    budget.max_load_jobs
mesh jobs:        budget.max_mesh_jobs
persist jobs:     budget.max_persist_jobs
clean evictions:  budget.max_clean_evictions
```

Near-to-far traversal uses Chebyshev rings.

Load scan:

```cpp
for coord in chebyshev_rings(center, active_radius):
    requestLoadIfNeeded(coord)

for coord in chebyshev_rings(center, render_distance):
    if distance(coord, center) > active_radius:
        requestLoadIfNeeded(coord)
```

`requestLoadIfNeeded(coord)`:

```cpp
slot = findSlot(coord)

if slot does not exist:
    slot = allocateSlot(coord)
    if allocation failed:
        return SlotPoolFull

if slot.state == NotResident:
    Job job;
    job.type = JobType::LoadChunk;
    job.chunk = coord;

    if work_queue.try_push(std::move(job)):
        slot.state = Loading

if slot.state == Failed and current_stream_tick >= slot.next_retry_tick:
    Job retry_job;
    retry_job.type = JobType::LoadChunk;
    retry_job.chunk = coord;

    if work_queue.try_push(std::move(retry_job)):
        slot.state = Loading
```

Mesh job scan:

```cpp
for each slot_id in pool.live_slots[0..pool.live_count):
    slot = &pool.slots[slot_id]
    if resident and needs remesh and no pending edits and no active mesh read lease:
        submit BuildChunkMesh
```

Eviction scan:

```cpp
for each slot_id in pool.live_slots[0..pool.live_count):
    slot = &pool.slots[slot_id]
    if outside_keep_window(slot.coord):
        process eviction/persist-on-evict rules
```

This is linear in the number of live chunk slots, not the size of the world or the hash-index capacity. With render distance 20, the known set is bounded by the moving window plus temporary loading/persisting slots, so this scan is acceptable for this engine.

## Eviction

Clean resident chunks outside keep are erased:

```cpp
slot.state == ChunkState::Resident
outside_keep_window(slot.coord)
slot.dirty_for_disk == false
slot.mesh_job_revision == INVALID_REVISION
slot.pending_edits.count == 0
```

Evicting a clean resident chunk does:

```cpp
if tryReleaseChunkData(slot.data):
    renderer.deleteMesh(slot.coord)
    freeSlot(slot.id)
```

Dirty resident chunks outside keep start persist-on-evict:

```cpp
slot.state == ChunkState::Resident
outside_keep_window(slot.coord)
slot.dirty_for_disk == true
slot.mesh_job_revision == INVALID_REVISION
slot.pending_edits.count == 0
```

They are not freed from the slot pool at this point. They transition to `PersistingForEviction` and remain there until the persist result is applied.

Failed or not-resident slots outside keep may be erased:

```cpp
slot.state == ChunkState::Failed ||
slot.state == ChunkState::NotResident
```

Do not evict:

- `Loading`,
- `PersistingForEviction`,
- chunks with an active mesh read lease,
- chunks with pending edits,
- dirty resident chunks before persist succeeds.

The cache must not erase a slot while `mesh_job_revision != INVALID_REVISION`, because a mesh worker may hold a borrowed pointer into that slot's `ChunkData`.

## Renderer Storage

Renderer owns GPU meshes in fixed CPU-side storage:

```cpp
using MeshId = uint32_t;

inline constexpr MeshId INVALID_MESH = UINT32_MAX;
inline constexpr uint32_t MAX_CHUNK_MESHES = MAX_CHUNK_SLOTS;
inline constexpr uint32_t MAX_MESH_INDEX_ENTRIES = MAX_CHUNK_INDEX_ENTRIES;

struct ChunkMeshIndexEntry {
    ChunkCoord coord;
    MeshId mesh;
    IndexState state;
};

struct ChunkMeshPool {
    std::array<ChunkMesh, MAX_CHUNK_MESHES> meshes;
    std::array<MeshId, MAX_CHUNK_MESHES> free_stack;
    uint32_t free_count;

    std::array<MeshId, MAX_CHUNK_MESHES> live_meshes;
    uint32_t live_count;
};

struct ChunkMeshIndex {
    std::array<ChunkMeshIndexEntry, MAX_MESH_INDEX_ENTRIES> entries;
    uint32_t occupied_count;
    uint32_t tombstone_count;
};

struct ChunkRendererStorage {
    ChunkMeshPool pool;
    ChunkMeshIndex index;
};
```

Renderer API:

```cpp
bool uploadMesh(ChunkCoord coord, uint64_t revision, CpuChunkMesh&& mesh);
void deleteMesh(ChunkCoord coord);
void draw();
```

Renderer flow:

1. Main thread accepts a `ChunkMeshBuilt` result that matches the active read lease.
2. Main thread calls `renderer.uploadMesh(...)`.
3. Renderer takes a free fixed mesh slot if needed, then uploads/replaces OpenGL buffers for that coord.
4. Main thread calls `renderer.deleteMesh(coord)` when cache eviction removes a chunk.
5. Renderer draws uploaded meshes.

`uploadMesh` returns false only if renderer fixed storage is full or the OpenGL upload fails. On failure, the cache does not advance `rendered_revision`.

`deleteMesh` removes the coord from the fixed mesh index and returns its mesh slot to the renderer free stack. It does not call `ChunkCache`.

Renderer does not call `ChunkCache`.

Renderer does not queue disk IO.

Renderer does not build CPU mesh data.

## Gameplay And Physics API

```cpp
bool tryGetBlock(WorldBlockCoord pos, blockData* out);
bool isSolidForPhysics(WorldBlockCoord pos);

enum class BlockEditResult {
    Applied,
    Queued,
    NotResident,
    OutsideActiveRadius,
    EditQueueFull
};

BlockEditResult setBlock(WorldBlockCoord pos, blockData block);
```

Policy:

- `tryGetBlock` succeeds only for resident chunks.
- `setBlock` succeeds only for resident active-window chunks.
- `isSolidForPhysics` treats non-resident chunks in the collision query area as solid.

These calls are main-thread calls into `ChunkCache`. They are intentionally not queued because movement, jumping, collision, block placement, and block deletion need immediate answers from resident chunks.

`setBlock` may return `Queued` when the target chunk currently has an active mesh read lease. A queued edit is guaranteed to apply later. `setBlock` returns `EditQueueFull` instead of blocking when the fixed edit buffer is full.

## Stats

```cpp
struct ChunkCacheStats {
    uint32_t slots;
    uint32_t free_slots;
    uint32_t index_occupied;
    uint32_t index_tombstones;
    uint32_t resident;
    uint32_t loading;
    uint32_t failed;
    uint32_t dirty_for_render;
    uint32_t mesh_jobs_in_flight;
    uint32_t chunks_with_pending_edits;
    uint32_t dirty_for_disk;
    uint32_t persisting_for_eviction;

    uint64_t edits_applied;
    uint64_t edits_queued;
    uint64_t edit_queue_full;
    uint64_t slot_pool_full;
    uint64_t index_full;
    uint64_t load_jobs_submitted;
    uint64_t load_jobs_completed;
    uint64_t persist_jobs_submitted;
    uint64_t persist_jobs_completed;
    uint64_t mesh_jobs_submitted;
    uint64_t mesh_jobs_completed;
    uint64_t release_jobs_submitted;
    uint64_t mesh_uploads;
    uint64_t region_generations;
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t evictions;
    uint64_t failures;
};
```

## Implementation Plan

1. Add coordinate structs and hash helpers.
2. Implement the fixed `ChunkSlotPool`, fixed `ChunkSlotIndex`, free-slot stack, and live-slot array.
3. Replace `ChunkData` ownership with chunk-level `std::unique_ptr<ChunkData>` and state-based worker read leases.
4. Implement `RegionStore` with header metadata cache, `regions_mutex`, and per-region file locks.
5. Add the worker job system with `LoadChunk`, `PersistChunk`, `BuildChunkMesh`, and `ReleaseChunkData`.
6. Replace renderer-owned load request sets with `ChunkCacheStorage`.
7. Implement load job submission and result application.
8. Implement CPU mesh job submission, stale-result dropping, and renderer upload.
9. Implement block edit path and boundary mesh invalidation.
10. Implement persist-on-evict and clean eviction without copying or main-thread freeing `ChunkData`.
11. Wire stats.

## Legacy File Policy

The existing resource-layer chunk code is not the source of truth for the new implementation. This HLD is the source of truth.

The old loader filename has been removed. Implement the new architecture in these files:

- `include/chunk_cache.h`
- `src/resources/chunk_cache.cpp`
- `include/region_store.h`
- `src/resources/region_store.cpp`
- `include/chunk_renderer.h`
- `src/resources/chunk_renderer.cpp`
- `include/chunk_mesh.h`
- `src/resources/chunk_mesh.cpp`

The removed legacy renderer/loader implementation contained stale concepts rejected by this HLD: `ActiveChunk`, `std::shared_ptr` chunk ownership, renderer-owned loading, renderer-owned remesh queues, `std::map` active chunk storage, and synchronous cache/renderer coupling.

`include/active_chunk.h` and `src/resources/active_chunk.cpp` are intentionally removed and must not be reintroduced.

Review these files and reuse only low-level code that still fits the HLD:

- `include/thread_pool.h`
- `src/resources/thread_pool.cpp`
- `include/thread_safe_queue.h`

`chunk_mesh` may keep useful vertex/face generation code, but ownership and scheduling must move to `ChunkMesher` worker jobs. `thread_pool` and `thread_safe_queue` may be reused only if they provide nonblocking main-thread `try_push` / bounded result draining semantics; otherwise replace them.

Do not use stale source code to override this document. If code and HLD disagree, implement the HLD.

`main.cpp` has the old streaming callsite disabled. During implementation, wire it to `ChunkCache`, `ChunkStreamingPipeline`, and `ChunkRenderer`; do not restore the old `ChunkLoader` wiring.
