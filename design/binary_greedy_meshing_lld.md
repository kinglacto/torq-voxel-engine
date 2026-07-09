# Binary Greedy Meshing LLD

Status: implemented

This document describes the current low-level CPU meshing implementation in
`src/resources/chunk_mesh.cpp`. It extends
`design/neighbor_aware_meshing_hld.md`.

## Scope

The mesher converts one `ChunkData` plus four optional horizontal neighbor
border masks into a `CpuChunkMesh`.

It runs only in worker jobs. It performs no OpenGL calls and owns no cache or
renderer state.

## Chunk Size Assumption

The current mesher is intentionally specialized:

```cpp
static_assert(BLOCK_X_SIZE == 16 &&
              BLOCK_Y_SIZE == 16 &&
              BLOCK_Z_SIZE == 16);
```

All binary rows are `uint16_t`, one bit per block along the row axis.

## Occupancy Masks

`buildOccupancyMasks` scans the chunk once and builds three axis-specific views:

```cpp
z_rows[y, x] -> 16 z bits
x_rows[y, z] -> 16 x bits
y_rows[z, x] -> 16 y bits
```

A set bit means the corresponding block is non-air.

This scan is the only full block traversal in meshing. Later face extraction is
bit-row based.

## Face Visibility

For each row, the mesher derives visible negative-axis and positive-axis faces:

```cpp
previousAxisVisibleBits(solid)
nextAxisVisibleBits(solid)
```

`previousAxisVisibleBits` emits faces where the previous neighbor in the row is
air. `nextAxisVisibleBits` emits faces where the next neighbor in the row is
air.

Horizontal chunk-boundary faces are additionally masked by
`ChunkNeighborMasks`:

- `neg_x` hides left faces at `x = 0`.
- `pos_x` hides right faces at `x = 15`.
- `neg_z` hides front faces at `z = 0`.
- `pos_z` hides back faces at `z = 15`.

If a neighbor mask is absent, that side is treated as air. This prevents mesh
jobs from waiting for neighbor loads.

Vertical chunk neighbors are not represented because the world currently uses a
single fixed chunk height.

## Face Slices

Visible faces are collected into `FaceSlice` arrays.

```cpp
struct FaceSlice {
    std::array<BitRow, 16> rows;
    std::array<textureLayerType, 16 * 16> layers;
};
```

Each slice corresponds to one face plane. Rows store face occupancy; `layers`
stores the texture layer for each visible face cell. Texture layers are part of
the merge key, so only cells with the same texture merge.

## Greedy Quad Merge

`appendGreedySlice` consumes one `FaceSlice`:

1. Find the first set bit.
2. Grow width along the row while visible bits and texture layer match.
3. Grow height while every row contains the same width mask and matching
   texture layers.
4. Emit one quad with six vertices.
5. Clear the consumed rectangle from the temporary row copy.

The original `FaceSlice` is not mutated; the function copies the 16 row masks
locally.

## Vertex Output

`appendGreedyQuad` emits six `TextureVertex` values per merged quad. Vertex
positions are chunk-local and use the existing `-0.5f` / `+0.5f` block-centered
coordinate convention. The renderer later applies a per-chunk model transform:

```cpp
translate(coord.x * BLOCK_X_SIZE, 0, coord.z * BLOCK_Z_SIZE)
```

The output mesh is a flat vertex list, not indexed geometry.

## Texture Selection

Texture layer selection uses:

```cpp
blockTexMap[block.id].texId[face]
```

Invalid or missing texture mappings fall back to `TexMap::side_dirt`.

## CPU/GPU Boundary

`ChunkMesher::build` returns:

```cpp
CpuChunkMesh{ std::vector<TextureVertex> vertices }
```

The worker moves this vector into the result. The main thread later uploads it
through `ChunkMesh::upload`.

`ChunkMesh::upload`:

- creates a fresh VAO/VBO,
- uploads the vertex vector with `GL_STATIC_DRAW`,
- installs attributes 0-3,
- deletes the previous VAO/VBO only after the new upload succeeds.

If the CPU mesh is empty, the GPU mesh is destroyed and the revision is still
recorded as uploaded.

## Correctness Rules

- The mesher never reads cache state.
- The mesher never follows neighbor pointers.
- Neighbor visibility is entirely captured by the fixed-size mask payload in
  the job.
- Boundary edits and chunk resident-state changes must bump affected neighbor
  revisions in `ChunkCache`; the mesher does not own invalidation.
