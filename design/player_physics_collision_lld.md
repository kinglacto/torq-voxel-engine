# Player Physics And Collision LLD

Status: implemented

This document describes the current `PlayerController` implementation in
`include/player_controller.h` and `src/physics/player_controller.cpp`.

## Ownership

`PlayerController` owns:

- feet-center position,
- velocity,
- grounded state.

It does not own camera orientation. `main.cpp` passes `camera.cameraFront` and
`camera.cameraRight` into `PlayerController::tick`.

`ChunkCache` remains the authoritative collision source through:

```cpp
ChunkCache::isSolidForPhysics(WorldBlockCoord)
```

The renderer and render meshes are never used for collision.

## Constants

Current constants:

```cpp
HALF_WIDTH = 0.30f
HEIGHT = 1.80f
EYE_HEIGHT = 1.62f
WALK_SPEED = 20.0f
JUMP_SPEED = 10.0f
GRAVITY = 24.0f
MAX_FALL_SPEED = 50.0f
COLLISION_SKIN = 0.001f
```

`position_` is feet-center, not eye position. The camera receives
`player.eyePosition()` after physics ticks.

## Input Contract

`processInput` returns `PlayerInputIntent`:

```cpp
move_forward
move_backward
move_left
move_right
jump
descend_or_crouch
```

In physics mode, `descend_or_crouch` is currently only part of the input struct;
it does not affect the AABB controller. In free-camera mode, `main.cpp` maps it
to camera-down movement.

## Frame Integration

`main.cpp` uses this order:

```cpp
const PlayerInputIntent input = processInput();

if (freeCameraMode) {
    updateFreeCamera(input);
}

chunkCache.setCenterChunk(worldPositionChunkCoord(streamCenter));
streamingPipeline.applyWorkerResults(streamBudget);
chunkCache.tick(streamBudget, chunkRenderer);

if (!freeCameraMode && !playerSpawnResolved) {
    tryFindSpawnAboveColumn(...);
}

if (!freeCameraMode && playerSpawnResolved) {
    player.tick(dt, input, camera.cameraFront, camera.cameraRight, chunkCache);
    camera.cameraPos = player.eyePosition();
}
```

`streamCenter` is `camera.cameraPos` in free-camera mode and
`player.feetPosition()` in physics mode.

## Movement

The controller projects camera front/right vectors onto the horizontal plane:

```cpp
direction.y = 0.0f;
normalize(direction)
```

Input adds/subtracts horizontal forward/right. The desired direction is
normalized so diagonal movement is not faster.

Horizontal velocity is direct:

```cpp
velocity_.x = wish_dir.x * WALK_SPEED;
velocity_.z = wish_dir.z * WALK_SPEED;
```

Jump applies only when grounded:

```cpp
if (input.jump && grounded_) {
    velocity_.y = JUMP_SPEED;
    grounded_ = false;
}
```

Gravity is applied every tick:

```cpp
velocity_.y = max(velocity_.y - GRAVITY * dt, -MAX_FALL_SPEED);
```

## Collision Shape

The player AABB is:

```cpp
min = feet + {-HALF_WIDTH, 0, -HALF_WIDTH}
max = feet + { HALF_WIDTH, HEIGHT, HALF_WIDTH}
```

Block AABBs are unit cubes from `{x,y,z}` to `{x+1,y+1,z+1}`.

## Collision Query

`findCollision` computes the integer block range overlapped by the candidate
AABB:

```cpp
min = floor(aabb.min)
max = floor(aabb.max - COLLISION_SKIN)
```

It scans every overlapped block and calls:

```cpp
chunk_cache.isSolidForPhysics({x, y, z})
```

The collision hit stores the min/max occupied block coordinates in the overlap.

`isSolidForPhysics` policy:

- `y < 0` is solid.
- `y >= BLOCK_Y_SIZE` is air.
- nonresident or unavailable block lookup is solid.
- resident non-air block is solid.

This prevents the player from falling through unloaded chunks.

## Axis Resolution

Movement resolves one axis at a time:

```cpp
moveAxis(0, velocity_.x * dt)
moveAxis(1, velocity_.y * dt)
moveAxis(2, velocity_.z * dt)
```

If the candidate AABB does not collide, the candidate position is accepted.

If it collides:

- X positive: place `position_.x` just before `hit.min_x`.
- X negative: place `position_.x` just after `hit.max_x + 1`.
- Y positive: place feet below `hit.min_y - HEIGHT`.
- Y negative: place feet above `hit.max_y + 1` and set grounded.
- Z positive/negative mirrors X.

The corresponding velocity component is zeroed after collision.

## Spawn Search

`tryFindSpawnAboveColumn` scans `feet_y` from `BLOCK_Y_SIZE` down to `1`.

A valid spawn requires:

- solid ground at `feet_y - 1`,
- no solid physics blocks at `feet_y`, `feet_y + 1`, `feet_y + 2`.

Output feet position is centered in the block column:

```cpp
{world_x + 0.5f, feet_y + COLLISION_SKIN, world_z + 0.5f}
```

## Placement Safety

`setBlockWithPlayerCollision` rejects non-air placement when the target unit
block intersects the player's current AABB:

```cpp
if (block.id != air && player.overlapsBlock(pos)) {
    return BlockEditResult::BlockedByPlayer;
}
```

It delegates to `ChunkCache::setBlock` after that check.
