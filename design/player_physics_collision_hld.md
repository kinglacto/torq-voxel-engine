# Player Physics And Collision HLD

Status: implemented extension

This document extends `design/chunk_streaming_pipeline_hld.md`. The streaming
HLD remains the source of truth for ownership, threading, cache residency, and
main-thread constraints. This document only defines first-person player
movement, gravity, and block collision. The implemented low-level behavior is
documented in `design/player_physics_collision_lld.md`.

## Problem

Movement is currently free-fly camera movement. `processInput()` calls
`Camera::updateCameraPos(...)`, which directly mutates `camera.cameraPos`.
There is no gravity, no grounded state, and no collision against voxel blocks.

Before block placement and deletion become gameplay features, the player should
have predictable collision against the current chunk cache. Otherwise block
edits can create immediate bad states: falling through terrain, moving through
newly placed blocks, or getting stuck without a clear collision owner.

The fix must preserve the existing HLD constraints:

- physics runs on the main thread,
- workers do not read or own player state,
- the renderer does not run physics,
- the renderer does not read `ChunkData`,
- physics performs no OpenGL calls,
- physics performs no heap allocation in the frame loop,
- physics does not lock chunk data,
- missing or nonresident chunks must not allow the player to fall through the
  world.

## Design Choice

Add a small main-thread `PlayerController` that owns player movement state and
queries `ChunkCache` for solid blocks.

Do not introduce a physics engine. The world is axis-aligned voxels, so an
axis-aligned bounding box controller is enough for the first implementation.

Do not use renderer meshes or meshing bitmasks for collision. Render meshes are
an output artifact and may be stale while remesh jobs are pending. Neighbor
border masks only describe chunk boundary occupancy for meshing; they are not a
complete collision representation. Collision must query authoritative resident
block data through `ChunkCache`.

The first implementation should use:

```cpp
bool solid = chunkCache.isSolidForPhysics(world_block);
```

`ChunkCache::isSolidForPhysics(...)` currently treats failed block lookups as
solid. That policy is correct for the player controller because unloaded chunks
become temporary walls/floors instead of holes.

## Ownership

`PlayerController`

- Owns player body position.
- Owns velocity.
- Owns grounded state.
- Converts input intent into movement.
- Performs AABB collision queries against `ChunkCache`.
- Produces the camera eye position.

`Camera`

- Owns view orientation: yaw, pitch, front/right/up vectors, zoom.
- Does not own physical movement once physics is enabled.
- Receives `camera.cameraPos = player.eyePosition()` after physics.

`ChunkCache`

- Remains authoritative for CPU block data.
- Provides `isSolidForPhysics(WorldBlockCoord)`.
- Does not know about player state.

`ChunkRenderer`

- Owns GPU meshes only.
- Does not participate in physics.

## Proposed Files

Add:

```text
include/player_controller.h
src/physics/player_controller.cpp
```

Modify:

```text
main.cpp
src/IO/camera.cpp
src/IO/camera.h
```

`Camera::updateCameraPos(...)` may remain for a future debug fly mode, but the
normal game loop should stop using it for WASD movement.

## Data Model

Input should be separated from movement application:

```cpp
struct PlayerInputIntent {
    bool move_forward{false};
    bool move_backward{false};
    bool move_left{false};
    bool move_right{false};
    bool jump{false};
    bool descend_or_crouch{false};
};
```

Player state:

```cpp
class PlayerController {
public:
    explicit PlayerController(glm::vec3 feet_position);

    void tick(float dt,
              const PlayerInputIntent& input,
              glm::vec3 camera_front,
              glm::vec3 camera_right,
              const torq::ChunkCache& chunk_cache);

    [[nodiscard]] glm::vec3 eyePosition() const noexcept;
    [[nodiscard]] glm::vec3 feetPosition() const noexcept;
    [[nodiscard]] bool grounded() const noexcept;

private:
    glm::vec3 position_; // feet-center position
    glm::vec3 velocity_;
    bool grounded_{false};
};
```

Initial constants:

```cpp
half_width = 0.30f;     // player AABB x/z half extent
height = 1.80f;         // feet to top of head
eye_height = 1.62f;     // feet to camera
walk_speed = 20.0f;     // blocks per second
jump_speed = 10.0f;     // blocks per second
gravity = 24.0f;        // blocks per second squared
max_fall_speed = 50.0f; // terminal clamp
skin = 0.001f;          // small separation from block faces
```

`Camera::speed` remains `90` for free-fly debug movement. Player movement uses
the separate `PlayerController` constants above.

## Frame Order

Recommended frame order:

```cpp
const PlayerInputIntent input = readPlayerInputIntent();
updateCameraLookFromMouse();

chunkCache.setCenterChunk(playerChunkCoord());
streamingPipeline.applyWorkerResults(streamBudget);
chunkCache.tick(streamBudget, chunkRenderer);

player.tick(deltaTime, input, camera, chunkCache);
camera.cameraPos = player.eyePosition();

view = camera.getViewMatrix();
render();
```

The cache center uses the current player position from the start of the frame.
If physics moves the player into a new chunk, the cache center catches up next
frame. That avoids double cache updates and keeps the streaming pipeline simple.

Physics runs after worker results are applied so it sees the freshest available
resident chunks for that frame.

## Movement

Horizontal movement should use camera yaw but ignore camera pitch:

```cpp
forward = normalize(vec3(camera.cameraFront.x, 0, camera.cameraFront.z));
right = normalize(vec3(camera.cameraRight.x, 0, camera.cameraRight.z));
```

Build a desired horizontal direction from input:

```cpp
wish_dir = 0;
if (forward)  wish_dir += forward;
if (backward) wish_dir -= forward;
if (right)    wish_dir += right;
if (left)     wish_dir -= right;
```

Normalize `wish_dir` when nonzero so diagonal movement is not faster.

For the first implementation, horizontal velocity can be direct and arcade-like:

```cpp
velocity_.x = wish_dir.x * walk_speed;
velocity_.z = wish_dir.z * walk_speed;
```

Jump:

```cpp
if (input.jump && grounded_) {
    velocity_.y = jump_speed;
    grounded_ = false;
}
```

Gravity:

```cpp
velocity_.y = max(velocity_.y - gravity * dt, -max_fall_speed);
```

## Collision Shape

Use a standing AABB:

```cpp
min = position_ + vec3(-half_width, 0.0f, -half_width);
max = position_ + vec3( half_width, height,  half_width);
```

`position_` is the feet-center position, not the eye position.

Collision samples every integer block coordinate overlapped by the candidate
AABB:

```cpp
min_block_x = floor(aabb.min.x);
max_block_x = floor(aabb.max.x);
min_block_y = floor(aabb.min.y);
max_block_y = floor(aabb.max.y);
min_block_z = floor(aabb.min.z);
max_block_z = floor(aabb.max.z);
```

For each block:

```cpp
if (chunk_cache.isSolidForPhysics({x, y, z})) {
    collision = true;
}
```

This is cheap. A normal player AABB overlaps only a small fixed number of
blocks: roughly `2 x 3 x 2`, with occasional boundary cases.

## Axis Resolution

Move and resolve one axis at a time:

```cpp
moveAxis(X, velocity_.x * dt);
moveAxis(Y, velocity_.y * dt);
moveAxis(Z, velocity_.z * dt);
```

Axis separation avoids corner ambiguity and keeps the implementation simple.

For a positive X move:

1. Build candidate AABB at `position_.x + dx`.
2. If no overlap with solid blocks, apply movement.
3. If blocked, clamp player X to just before the nearest blocking block face
   and set `velocity_.x = 0`.

For negative movement, clamp to just after the blocking block face.

For Y:

- If moving down and blocked, set `grounded_ = true` and `velocity_.y = 0`.
- If moving up and blocked, set `velocity_.y = 0`.
- If no downward collision, set `grounded_ = false`.

The first version can use small incremental candidate clamping based on the
overlapped block range. Swept continuous collision is not required if movement
speed is conservative and `dt` is clamped, which `main.cpp` already does with:

```cpp
deltaTime = min(frameDelta, 0.05f);
```

## Missing Chunks

Policy:

```text
missing or nonresident chunk == solid for physics
```

This is already implemented by `ChunkCache::isSolidForPhysics(...)`, because
`tryGetBlock(...)` failure returns `true` for solid.

Behavior:

- If terrain is not loaded yet, the player cannot fall through it.
- At high movement speed, the player may temporarily hit an invisible wall
  until streaming catches up.
- This is acceptable for the first implementation and safer than treating
  missing chunks as air.

## Spawn

The player must not start inside a solid block.

Initial MVP:

```cpp
PlayerController player(glm::vec3(0.0f, 40.0f, 200.0f));
```

This preserves the current camera neighborhood and gives gravity a chance to
settle the player.

Better follow-up:

```cpp
findSpawnAboveColumn(x, z, chunk_cache)
```

Search downward or upward for:

```text
solid ground at y - 1
air at y
air at y + 1
air at y + 2
```

This helper should run only after the spawn chunk is resident. Until then, the
player can remain in a temporary no-physics waiting state or use the high spawn
fallback.

## Block Edit Interaction

Block placement/deletion can use the existing `ChunkCache::setBlock(...)`.

Physics automatically sees applied edits because it queries `ChunkCache`.

Rules for future block edits:

- If a block is deleted under the player, the next physics tick sees air and
  gravity starts falling.
- If a block is placed intersecting the player AABB, either reject placement or
  allow it and let collision push/clamp the player next tick.
- Recommended first rule: reject placement if the target block overlaps the
  player AABB.

This avoids needing player depenetration for the initial block-edit feature.

## Why Not Use Bitmasks First

The mesher's binary masks are optimized for face visibility, not arbitrary AABB
physics:

- They are built inside `ChunkMesher`, often on workers.
- They are not stored as authoritative cache state.
- Neighbor masks only contain four horizontal borders.
- They do not cover all interior blocks needed by collision.
- They can be stale relative to pending remeshes.

If profiling later shows `isSolidForPhysics(...)` is expensive, add a dedicated
physics occupancy representation to `ChunkSlot`, such as:

```cpp
std::array<std::uint16_t, BLOCK_Y_SIZE * BLOCK_Z_SIZE> x_rows;
```

That should be cache-owned, updated with block edits, and read only on the main
thread. It is not needed for the first implementation.

## Main-Thread Constraints

The physics tick must:

- allocate no heap memory,
- use only stack locals and fixed loops,
- perform no locks,
- perform no OpenGL calls,
- perform no worker queue operations,
- not copy chunk arrays,
- only query individual blocks through `ChunkCache`.

The only per-frame collision loops should be over the small block coordinate
ranges overlapped by the player's AABB.

## Implementation Stages

### Stage 1: Player Controller Skeleton

Files:

```text
include/player_controller.h
src/physics/player_controller.cpp
main.cpp
```

Add `PlayerInputIntent`, `PlayerController`, constants, and camera eye sync.
Keep collision disabled initially or use vertical-only gravity check for a
minimal compile checkpoint.

### Stage 2: AABB Collision

Implement:

```cpp
bool overlapsSolid(const Aabb& aabb, const torq::ChunkCache& cache);
void moveAxis(Axis axis, float delta, const torq::ChunkCache& cache);
```

Wire full X/Y/Z movement and grounded state.

### Stage 3: Input Behavior

Change `processInput()` so it only fills `PlayerInputIntent` and updates camera
look/zoom. Stop calling `camera.updateCameraPos(...)` in normal gameplay.

### Stage 4: Spawn Handling

Start with a high fixed spawn. Add `findSpawnAboveColumn(...)` once needed.

### Stage 5: Block Edit Safety

Before placement, reject target blocks that overlap the player AABB. Deletion
requires no special case.

## Verification

Minimum non-window tests or harnesses:

- AABB over air does not collide.
- AABB overlapping one solid block collides.
- Missing chunk query blocks movement.
- Falling onto a flat chunk sets `grounded = true`.
- Jump clears grounded and later lands again.
- X/Z movement stops at a wall.
- Diagonal movement is normalized.

Manual runtime checks:

- Spawn above terrain and fall onto ground.
- Walk into terrain wall and stop.
- Walk across chunk boundaries.
- Jump under a low ceiling.
- Move while chunks are still streaming; player should block instead of falling
  through unloaded chunks.

## Open Tradeoffs

- Treating missing chunks as solid can create temporary invisible walls. This
  is preferable to falling through unloaded chunks.
- Simple axis-separated collision can catch on sharp block corners. This is
  acceptable for the first pass and can be improved later with step-up logic.
- No crouch, swimming, ladders, slopes, or stepping are included.
- Movement speed must stay modest until continuous swept collision exists.
