# Rendering And Runtime Controls LLD

Status: implemented

This document describes implemented rendering/runtime features that are not
part of the core chunk streaming HLD: frustum culling, fog/sun shader uniforms,
free-camera mode, FPS printing, and screenshot capture hooks.

## Free Camera Toggle

`main.cpp` exposes:

```cpp
bool freeCameraMode = true;
```

Mode behavior:

- `true`: `updateFreeCamera` moves `camera.cameraPos` directly.
- `false`: `PlayerController` owns physical movement and writes
  `camera.cameraPos = player.eyePosition()`.

In free-camera mode:

- stream center is `camera.cameraPos`,
- block placement calls `ChunkCache::setBlock` directly,
- no player AABB placement rejection is applied.

In player mode:

- stream center is `player.feetPosition()`,
- spawn resolution waits for resident chunk data via
  `tryFindSpawnAboveColumn`,
- player physics runs after streaming worker results are applied,
- placement calls `setBlockWithPlayerCollision`.

## Free Camera Movement

`updateFreeCamera` maps the shared input intent to `Camera::updateCameraPos`:

- W/S: forward/back along `cameraFront`.
- A/D: left/right along `cameraRight`.
- Space: up along `cameraUp`.
- Left Shift: down along `cameraUp`.

`Camera::speed` is currently `90.0f`, separate from player movement speed.

## Camera Look

Mouse look only updates while left mouse is held:

```cpp
if ((mouse_dx != 0 || mouse_dy != 0) &&
    mouse::button(GLFW_MOUSE_BUTTON_LEFT)) {
    camera.updateCameraDirection(mouse_dx, mouse_dy);
}
```

Mouse wheel changes zoom/FOV through `Camera::updateCameraZoom`.

## FPS Printing

`main.cpp` exposes:

```cpp
bool printFPS = false;
```

When enabled, the main loop prints once per second:

```text
FPS: <frames / elapsed seconds>
```

The counter uses `glfwGetTime()` and counts presented game-loop iterations.

## Automated FPS Test

For noninteractive runs, `TORQ_FPS_TEST_SECONDS` enables a timed run:

```bash
TORQ_FPS_TEST_SECONDS=5 ./torq
```

The loop tracks:

- elapsed seconds,
- frame count,
- average FPS,
- min instantaneous FPS,
- max instantaneous FPS.

When the elapsed time reaches the requested duration, the game closes itself.

## Screenshot Capture Hook

Environment variables:

```text
TORQ_CAPTURE_FRAME
TORQ_CAPTURE_PATH
TORQ_CAPTURE_MIN_MESHES
TORQ_EXIT_AFTER_CAPTURE
```

When `frameIndex >= TORQ_CAPTURE_FRAME` and the live mesh count is at least
`TORQ_CAPTURE_MIN_MESHES`, the game saves the back buffer to PNG using
`saveFramebufferScreenshot`.

`saveFramebufferScreenshot` performs synchronous `glReadPixels`, vertically
flips RGB data, and writes PNG with `stbi_write_png`. It is for screenshots and
tests, not per-frame video capture.

## Frustum Culling

`main.cpp` computes:

```cpp
ChunkFrustum frustum = makeChunkFrustum(projection * view);
chunkRenderer.draw(shader, frustum);
```

`makeChunkFrustum` extracts six normalized planes from the view-projection
matrix:

- left,
- right,
- bottom,
- top,
- near,
- far.

`ChunkRenderer::draw(shader, frustum)` tests each live chunk mesh against an
axis-aligned chunk AABB before issuing the draw call.

The AABB is:

```cpp
center = {
    coord.x * BLOCK_X_SIZE + (BLOCK_X_SIZE - 1) * 0.5f,
    (BLOCK_Y_SIZE - 1) * 0.5f,
    coord.z * BLOCK_Z_SIZE + (BLOCK_Z_SIZE - 1) * 0.5f
}
extents = {BLOCK_X_SIZE * 0.5f, BLOCK_Y_SIZE * 0.5f, BLOCK_Z_SIZE * 0.5f}
```

Plane test:

```cpp
distance = dot(plane.xyz, center) + plane.w
radius = dot(abs(plane.xyz), extents)
cull if distance + radius < 0
```

## Fog And Sun/Ambient Lighting

`assets/shaders/basic_texture.fs` applies:

1. Texture-array sampling.
2. Hemisphere ambient fill.
3. Wrapped/smoothed directional sun diffuse.
4. Max-light clamp.
5. Distance fog.

Uniforms set in `main.cpp`:

```cpp
fogColor
fogStart
fogEnd
sunDirection
sunColor
skyAmbientColor
groundAmbientColor
hemisphereStrength
sunStrength
sunWrap
maxLight
```

Fog distances derive from render distance:

```cpp
renderDistanceBlocks = render_distance * BLOCK_X_SIZE
fogStart = renderDistanceBlocks * 0.75f
fogEnd = renderDistanceBlocks * 1.35f
```

Lighting is intentionally single-pass and shadowless. Shadow mapping is not
implemented.
