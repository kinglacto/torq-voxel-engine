# Runtime Block Editing LLD

Status: implemented

This document describes the current block placement/deletion path in
`main.cpp`, `ChunkCache::setBlock`, and `PlayerController` placement safety.

## Controls

Current controls:

- `R`: delete targeted solid block.
- Right mouse button: place hardcoded grass block.
- Left mouse button is reserved for camera look.

The selected place block is currently hardcoded:

```cpp
BlockData grass{};
grass.id = BlockMap::grass;
```

Deletion sets the target block to air.

## Raycast

`raycastEditableBlock` is a sampled camera ray:

```cpp
origin = camera.cameraPos
direction = camera.cameraFront
maxDistance = BLOCK_EDIT_REACH     // 6.0f
step = BLOCK_EDIT_RAY_STEP         // 0.05f
```

Every sampled point is floored to a `WorldBlockCoord`. Repeated samples inside
the same block are skipped.

Block state is queried through:

```cpp
ChunkCache::tryGetBlock
```

The edit ray intentionally does not use `isSolidForPhysics`, because physics
treats nonresident chunks as solid. Editing should not target invisible,
unloaded blocks.

Raycast result fields:

- `hit`: first resident solid block was found.
- `delete_target`: the first resident solid block.
- `place_target`: the last resident air block visited before a solid hit, or
  the farthest resident air block reached within range.
- `has_place_target`: true once any resident air block was found.

If a sampled block is below world height or not resident, the raycast stops and
returns the best result gathered so far.

## Delete Flow

On `R` key down:

1. Raycast from the camera.
2. Require `hit == true`.
3. Build `BlockData air`.
4. Call `chunkCache.setBlock(hit.delete_target, air)`.
5. Print applied/queued result or failure reason.

Deletion does not need a player-overlap guard.

## Placement Flow

On right mouse down:

1. Raycast from the camera.
2. Require `has_place_target == true`.
3. Build `BlockData grass`.
4. If in free-camera mode, call `chunkCache.setBlock`.
5. If in player mode, call `setBlockWithPlayerCollision`.
6. Print applied/queued result or failure reason.

Free-camera placement bypasses player collision because there is no active
physical player body attached to the camera in that mode.

## Cache Edit Contract

`ChunkCache::setBlock` validates:

- local coordinate is inside the 16x16x16 chunk,
- target chunk is resident and owns data,
- target chunk is inside `active_radius`.

Possible results:

```cpp
Applied
Queued
NotResident
OutsideActiveRadius
EditQueueFull
BlockedByPlayer
```

`BlockedByPlayer` is returned only by `setBlockWithPlayerCollision`.

## Immediate Edit

If no mesh job is reading the target chunk:

```cpp
slot.data->blocks[blockIndex(local)] = block;
slot.current_revision++;
slot.dirty_for_disk = true;
markBoundaryNeighborsDirty(coord, local);
```

The next mesh scan sees `current_revision != rendered_revision` and submits a
new mesh job under the normal mesh budget.

## Queued Edit

If `slot.mesh_job_revision != INVALID_REVISION`, a mesh worker has a read lease
on the chunk data. The edit is appended to the fixed per-slot edit buffer:

```cpp
FixedEditBuffer<MAX_PENDING_EDITS_PER_CHUNK>
```

No heap allocation is performed. If the buffer is full, the edit returns
`EditQueueFull`.

Queued edits are applied in `applyPendingEdits` after the mesh result returns
and the read lease is cleared.

## Boundary Invalidation

When an applied edit touches `local.x == 0`, `local.x == 15`, `local.z == 0`,
or `local.z == 15`, the corresponding resident horizontal neighbor gets:

```cpp
neighbor.current_revision++;
```

Neighbor block data and `dirty_for_disk` are not modified. Only the mesh source
changed because a face may now be hidden or revealed across the boundary.

## Persistence

Any applied edit sets:

```cpp
slot.dirty_for_disk = true;
```

Dirty chunks are persisted through the normal eviction path with
`PersistChunk` worker jobs and zlib-compressed region storage.
