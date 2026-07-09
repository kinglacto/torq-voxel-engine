# Neighbor-Aware Chunk Meshing HLD

Status: implemented extension

This document extends `design/chunk_streaming_pipeline_hld.md`. The existing
streaming HLD remains the source of truth for ownership, queues, worker/main
thread separation, and renderer ownership. This document only changes how CPU
mesh jobs determine whether faces on chunk boundaries are visible. The current
low-level implementation also uses binary occupancy masks and greedy quad
merging; see `design/binary_greedy_meshing_lld.md`.

## Problem

The current `ChunkMesher::build(const ChunkData& chunk)` can only inspect one
chunk. For any neighbor coordinate outside `[0, 15]`, it treats the neighbor as
air:

```cpp
if (!inBounds(x, y, z)) {
    return false;
}
```

That means solid blocks on chunk edges always emit boundary faces, even when the
adjacent resident chunk has solid blocks touching those faces. This wastes
vertices and fragments, and can create unnecessary overdraw along every loaded
chunk seam.

The fix must preserve the existing HLD constraints:

- workers do not own or lock cache state,
- the main thread does not copy full chunk arrays,
- mesh workers borrow only the target chunk through the existing read lease,
- no main-thread waits,
- no extra queues,
- renderer ownership does not change.

## Design Choice

Use fixed-size neighbor border occupancy masks.

Do not pass full neighbor `ChunkData*` pointers to mesh jobs. Full neighbor
pointers would require multi-chunk read leases and more complex mutation and
eviction rules. A border mask is enough because boundary face visibility only
needs one block column from the adjacent chunk.

For a `16 x 16 x 16` chunk, each horizontal border has `16 * 16 = 256` blocks.
Four horizontal sides require `4 * 256` bits, or `128` bytes, plus validity
flags.

If a neighbor is resident, the main thread snapshots that neighbor's touching
border into the mesh job. If a neighbor is not resident, the mask is marked
absent and the mesher treats that side as air. This preserves current behavior
for missing/unloaded neighbors while removing hidden boundary faces when the
neighbor is known.

## Data Model

Add a fixed mask type to `include/chunk_streaming.h`:

```cpp
inline constexpr int CHUNK_BORDER_BITS = BLOCK_Y_SIZE * BLOCK_X_SIZE;
inline constexpr int CHUNK_BORDER_WORDS = (CHUNK_BORDER_BITS + 63) / 64;

struct ChunkBorderMask {
    std::array<std::uint64_t, CHUNK_BORDER_WORDS> words{};
    bool present{false};
};

struct ChunkNeighborMasks {
    // x masks are indexed by (y, z).
    ChunkBorderMask neg_x; // neighbor {chunk.x - 1, chunk.z}, neighbor local x = 15
    ChunkBorderMask pos_x; // neighbor {chunk.x + 1, chunk.z}, neighbor local x = 0

    // z masks are indexed by (y, x).
    ChunkBorderMask neg_z; // neighbor {chunk.x, chunk.z - 1}, neighbor local z = 15
    ChunkBorderMask pos_z; // neighbor {chunk.x, chunk.z + 1}, neighbor local z = 0
};
```

Add this field to `Job`:

```cpp
ChunkNeighborMasks neighbor_masks{};
```

This is copied/moved through the existing bounded work queue as fixed-size job
payload data. It does not allocate and does not copy chunk block arrays.

## Mask Indexing

For x-side masks:

```cpp
bit_index = y * BLOCK_Z_SIZE + z;
```

For z-side masks:

```cpp
bit_index = y * BLOCK_X_SIZE + x;
```

A set bit means "the neighbor border block is solid".

Helpers:

```cpp
void setMaskBit(ChunkBorderMask& mask, int bit);
bool testMaskBit(const ChunkBorderMask& mask, int bit);
```

## Mesh Job Submission

In `ChunkCache::submitMeshJobs`, before pushing `BuildChunkMesh`, build masks
from currently resident horizontal neighbors:

```cpp
Job job;
job.type = JobType::BuildChunkMesh;
job.chunk = slot.coord;
job.read_data = slot.data.get();
job.revision = slot.current_revision;
job.neighbor_masks = buildNeighborMasks(slot.coord);

if (work_queue.tryPush(std::move(job))) {
    slot.mesh_job_revision = slot.current_revision;
}
```

`buildNeighborMasks(coord)` runs only on the main thread and only reads
main-owned resident slots.

A neighbor contributes a mask only when:

```cpp
neighbor.state == ChunkState::Resident &&
neighbor.data != nullptr
```

The neighbor may have an active mesh read lease. That is still safe because
mask construction is a read-only operation and active mesh leases prevent
main-thread mutation of that neighbor's `ChunkData`.

The neighbor may have pending edits. Pending edits are not visible until they
are applied under the base HLD rules, so the mask snapshots currently applied
block data only.

## Mesher Rule

Change the worker-side mesh call from:

```cpp
ChunkMesher::build(*job.read_data);
```

to:

```cpp
ChunkMesher::build(*job.read_data, job.neighbor_masks);
```

The mesher keeps normal in-chunk solid checks. Only out-of-bounds horizontal
checks use masks:

```cpp
bool isSolidWithNeighbors(chunk, masks, x, y, z) {
    if (y < 0 || y >= BLOCK_Y_SIZE) {
        return false;
    }

    if (x < 0) {
        return masks.neg_x.present &&
               testMaskBit(masks.neg_x, y * BLOCK_Z_SIZE + z);
    }

    if (x >= BLOCK_X_SIZE) {
        return masks.pos_x.present &&
               testMaskBit(masks.pos_x, y * BLOCK_Z_SIZE + z);
    }

    if (z < 0) {
        return masks.neg_z.present &&
               testMaskBit(masks.neg_z, y * BLOCK_X_SIZE + x);
    }

    if (z >= BLOCK_Z_SIZE) {
        return masks.pos_z.present &&
               testMaskBit(masks.pos_z, y * BLOCK_X_SIZE + x);
    }

    return chunk.blocks[blockIndex(x, y, z)].id != BlockMap::air;
}
```

If a side mask is not present, the neighbor is treated as air. This is
important: meshing must never wait for a neighbor load.

## Invalidation Rules

The mask is only a snapshot. Correctness comes from revision invalidation when
neighbor state or neighbor boundary blocks change.

### Boundary Block Edits

Block deletion is represented as `setBlock(..., air)`. Block placement and
block deletion use the same invalidation rule.

When an edit is applied to an edited chunk:

```cpp
slot.data[local] = new_block;
slot.current_revision += 1;
slot.dirty_for_disk = true;
```

If the edited block is on a horizontal chunk boundary, mark the corresponding
resident neighbor mesh dirty:

```cpp
neighbor.current_revision += 1;
```

This is required for both placement and deletion:

- placement at a boundary can hide a neighbor face,
- deletion at a boundary can reveal a neighbor face.

Do not modify the neighbor's `dirty_for_disk`; the neighbor's block data did
not change.

Do not modify the neighbor's `mesh_job_revision`; an in-flight neighbor mesh
job must keep its read lease until its result returns.

Queued edits follow the same rule when they are later applied on the main
thread. Pending edits do not change masks until they become applied block data.

### Chunk Load

When a chunk becomes resident, resident horizontal neighbors must remesh because
their old meshes may have treated this side as air.

On successful `applyLoadedChunk(result)`:

```cpp
slot.state = Resident;
slot.data = std::move(result.data);
slot.current_revision = 0;
slot.rendered_revision = INVALID_REVISION;

markResidentHorizontalNeighborsDirty(slot.coord);
```

The loaded chunk already needs its first mesh because `rendered_revision` is
invalid. Neighbor chunks need a revision bump so their next mesh job includes
this new border mask.

### Chunk Eviction Or Resident-State Loss

When a resident chunk stops being usable as a mask source, resident horizontal
neighbors must remesh because their old meshes may have hidden faces against
this chunk.

Mark neighbors dirty before or at the point the chunk leaves `Resident`.

This applies to:

- clean resident eviction,
- dirty resident transition to `PersistingForEviction`,
- final dirty eviction after persist success if the transition was not already
  dirtied,
- any future resident-to-nonresident path.

Rule:

```cpp
if (slot.state == Resident && slot.data != nullptr && outside_keep_window(slot.coord)) {
    markResidentHorizontalNeighborsDirty(slot.coord);
    // then move/free/persist slot data according to base HLD eviction rules
}
```

If persist fails and the chunk returns to `Resident`, mark resident neighbors
dirty again. The neighbor now has a mask source again.

### Neighbor Edits While Target Mesh Is In Flight

If target chunk A has a mesh job in flight and neighbor chunk B changes a
boundary block touching A:

1. B applies or later applies its edit on the main thread.
2. B increments A's `current_revision` if A is resident.
3. A's in-flight mesh result may still upload because it matches
   `mesh_job_revision`.
4. A remains dirty because `current_revision != rendered_revision`.
5. The normal mesh scan submits a newer A mesh job later.

This matches the base HLD's stale-result strategy and does not need a second
scheduling path.

## New Cache Helpers

Add private helpers to `ChunkCache`:

```cpp
ChunkNeighborMasks buildNeighborMasks(ChunkCoord coord) const noexcept;
void markResidentHorizontalNeighborsDirty(ChunkCoord coord);
```

Mask fill helpers:

```cpp
void fillNegXMask(ChunkBorderMask& out, const ChunkData& neighbor); // local x = 15
void fillPosXMask(ChunkBorderMask& out, const ChunkData& neighbor); // local x = 0
void fillNegZMask(ChunkBorderMask& out, const ChunkData& neighbor); // local z = 15
void fillPosZMask(ChunkBorderMask& out, const ChunkData& neighbor); // local z = 0
```

These helpers do not allocate.

## File-Level Implementation Plan

1. `include/chunk_streaming.h`
   - Add `ChunkBorderMask`.
   - Add `ChunkNeighborMasks`.
   - Add `neighbor_masks` to `Job`.

2. `include/chunk_mesh.h`
   - Change `ChunkMesher::build(const ChunkData&)` to
     `ChunkMesher::build(const ChunkData&, const ChunkNeighborMasks&)`.

3. `src/resources/chunk_mesh.cpp`
   - Replace local-only `isSolid` with neighbor-aware solid checks.
   - Keep missing masks as air.

4. `include/chunk_cache.h`
   - Add private declarations for mask building and neighbor dirty marking.

5. `src/resources/chunk_cache.cpp`
   - Build masks in `submitMeshJobs`.
   - Mark resident neighbors dirty after successful chunk load.
   - Mark resident neighbors dirty when a resident chunk leaves resident mask
     availability during eviction/persisting.
   - Mark resident neighbors dirty if persist failure restores a chunk to
     resident.
   - Keep existing boundary edit invalidation, but treat both placement and
     deletion identically.

6. `src/resources/chunk_worker.cpp`
   - Pass `job.neighbor_masks` into `ChunkMesher::build`.

## Verification Plan

Add focused non-OpenGL tests or temporary harnesses:

1. Two adjacent full solid chunks:
   - Mesh left chunk with no neighbor mask: right boundary faces exist.
   - Mesh left chunk with `pos_x` mask present and solid: right boundary faces
     are removed.

2. Missing neighbor:
   - No mask present keeps current behavior and emits boundary faces.

3. Boundary delete:
   - Set boundary block to air in chunk A.
   - Resident neighbor B gets `current_revision += 1`.
   - B's next mesh uses A's mask and exposes the face.

4. Boundary placement:
   - Set boundary block to grass in chunk A.
   - Resident neighbor B gets `current_revision += 1`.
   - B's next mesh uses A's mask and hides the face.

5. Chunk load:
   - Load chunk A next to resident chunk B.
   - B revision increments.

6. Chunk eviction:
   - Evict or start persist for resident chunk A next to resident B.
   - B revision increments.

7. In-flight target mesh:
   - Submit mesh for A.
   - Edit boundary in B.
   - A current revision increments while `mesh_job_revision` remains old.
   - Old A result can upload, but A remains dirty for a later remesh.

## Tradeoffs

This reduces boundary vertex waste without changing renderer ownership or draw
call structure.

It does not solve:

- one draw call per chunk,
- faces hidden by blocks beyond the immediate neighbor border,
- greedy face merging,
- frustum culling.

Those remain separate renderer/mesher optimization stages.
