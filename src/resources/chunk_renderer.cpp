#include "chunk_renderer.h"

#include <array>
#include <cassert>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace torq {
namespace {

constexpr std::uint32_t INDEX_REBUILD_TOMBSTONE_THRESHOLD =
    MAX_MESH_INDEX_ENTRIES / 4;

[[nodiscard]] glm::vec4 matrixRow(const glm::mat4& matrix,
                                  const int row) noexcept {
    return {
        matrix[0][row],
        matrix[1][row],
        matrix[2][row],
        matrix[3][row]
    };
}

[[nodiscard]] glm::vec4 normalizePlane(const glm::vec4 plane) noexcept {
    const glm::vec3 normal{plane.x, plane.y, plane.z};
    const float length = glm::length(normal);
    if (length <= 0.0f) {
        return plane;
    }

    return plane / length;
}

[[nodiscard]] bool intersectsFrustum(const ChunkCoord coord,
                                     const ChunkFrustum& frustum) noexcept {
    const glm::vec3 center{
        static_cast<float>(coord.x * BLOCK_X_SIZE) +
            static_cast<float>(BLOCK_X_SIZE - 1) * 0.5f,
        static_cast<float>(BLOCK_Y_SIZE - 1) * 0.5f,
        static_cast<float>(coord.z * BLOCK_Z_SIZE) +
            static_cast<float>(BLOCK_Z_SIZE - 1) * 0.5f
    };
    const glm::vec3 extents{
        static_cast<float>(BLOCK_X_SIZE) * 0.5f,
        static_cast<float>(BLOCK_Y_SIZE) * 0.5f,
        static_cast<float>(BLOCK_Z_SIZE) * 0.5f
    };

    for (const glm::vec4& plane : frustum.planes) {
        const float distance =
            plane.x * center.x +
            plane.y * center.y +
            plane.z * center.z +
            plane.w;
        const float radius =
            std::abs(plane.x) * extents.x +
            std::abs(plane.y) * extents.y +
            std::abs(plane.z) * extents.z;

        if (distance + radius < 0.0f) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::uint32_t initialProbe(const ChunkCoord coord) noexcept {
    return static_cast<std::uint32_t>(
        ChunkCoordHash{}(coord) % MAX_MESH_INDEX_ENTRIES
    );
}

[[nodiscard]] MeshId findInIndex(const ChunkMeshIndex& index,
                                 const ChunkCoord coord) noexcept {
    std::uint32_t probe = initialProbe(coord);
    for (std::uint32_t checked = 0; checked < MAX_MESH_INDEX_ENTRIES; checked++) {
        const ChunkMeshIndexEntry& entry = index.entries[probe];
        if (entry.state == IndexState::Empty) {
            return INVALID_MESH;
        }
        if (entry.state == IndexState::Occupied && entry.coord == coord) {
            return entry.mesh;
        }

        probe = (probe + 1U) % MAX_MESH_INDEX_ENTRIES;
    }

    return INVALID_MESH;
}

[[nodiscard]] bool insertIntoIndexNoRebuild(ChunkMeshIndex& index,
                                            const ChunkCoord coord,
                                            const MeshId mesh) noexcept {
    std::uint32_t probe = initialProbe(coord);
    std::uint32_t first_tombstone = MAX_MESH_INDEX_ENTRIES;

    for (std::uint32_t checked = 0; checked < MAX_MESH_INDEX_ENTRIES; checked++) {
        ChunkMeshIndexEntry& entry = index.entries[probe];

        if (entry.state == IndexState::Occupied) {
            if (entry.coord == coord) {
                entry.mesh = mesh;
                return true;
            }
        } else if (entry.state == IndexState::Tombstone) {
            if (first_tombstone == MAX_MESH_INDEX_ENTRIES) {
                first_tombstone = probe;
            }
        } else {
            const std::uint32_t target =
                first_tombstone != MAX_MESH_INDEX_ENTRIES ? first_tombstone : probe;
            ChunkMeshIndexEntry& target_entry = index.entries[target];
            if (target_entry.state == IndexState::Tombstone) {
                assert(index.tombstone_count > 0);
                index.tombstone_count--;
            }

            target_entry.coord = coord;
            target_entry.mesh = mesh;
            target_entry.state = IndexState::Occupied;
            index.occupied_count++;
            return true;
        }

        probe = (probe + 1U) % MAX_MESH_INDEX_ENTRIES;
    }

    if (first_tombstone != MAX_MESH_INDEX_ENTRIES) {
        ChunkMeshIndexEntry& target_entry = index.entries[first_tombstone];
        assert(index.tombstone_count > 0);
        index.tombstone_count--;
        target_entry.coord = coord;
        target_entry.mesh = mesh;
        target_entry.state = IndexState::Occupied;
        index.occupied_count++;
        return true;
    }

    return false;
}

void rebuildIndex(ChunkMeshIndex& index) noexcept {
    const std::array<ChunkMeshIndexEntry, MAX_MESH_INDEX_ENTRIES> old_entries =
        index.entries;

    for (ChunkMeshIndexEntry& entry : index.entries) {
        entry = ChunkMeshIndexEntry{};
    }
    index.occupied_count = 0;
    index.tombstone_count = 0;

    for (const ChunkMeshIndexEntry& entry : old_entries) {
        if (entry.state == IndexState::Occupied) {
            const bool inserted =
                insertIntoIndexNoRebuild(index, entry.coord, entry.mesh);
            assert(inserted);
        }
    }
}

[[nodiscard]] bool insertIntoIndex(ChunkMeshIndex& index,
                                   const ChunkCoord coord,
                                   const MeshId mesh) noexcept {
    if (index.tombstone_count >= INDEX_REBUILD_TOMBSTONE_THRESHOLD) {
        rebuildIndex(index);
    }

    if (insertIntoIndexNoRebuild(index, coord, mesh)) {
        return true;
    }

    if (index.tombstone_count > 0) {
        rebuildIndex(index);
        return insertIntoIndexNoRebuild(index, coord, mesh);
    }

    return false;
}

[[nodiscard]] bool eraseFromIndex(ChunkMeshIndex& index,
                                  const ChunkCoord coord) noexcept {
    std::uint32_t probe = initialProbe(coord);
    for (std::uint32_t checked = 0; checked < MAX_MESH_INDEX_ENTRIES; checked++) {
        ChunkMeshIndexEntry& entry = index.entries[probe];
        if (entry.state == IndexState::Empty) {
            return false;
        }

        if (entry.state == IndexState::Occupied && entry.coord == coord) {
            entry.mesh = INVALID_MESH;
            entry.state = IndexState::Tombstone;
            assert(index.occupied_count > 0);
            index.occupied_count--;
            index.tombstone_count++;
            return true;
        }

        probe = (probe + 1U) % MAX_MESH_INDEX_ENTRIES;
    }

    return false;
}

} // namespace

ChunkFrustum makeChunkFrustum(const glm::mat4& view_projection) noexcept {
    const glm::vec4 row0 = matrixRow(view_projection, 0);
    const glm::vec4 row1 = matrixRow(view_projection, 1);
    const glm::vec4 row2 = matrixRow(view_projection, 2);
    const glm::vec4 row3 = matrixRow(view_projection, 3);

    ChunkFrustum frustum{};
    frustum.planes[0] = normalizePlane(row3 + row0); // Left
    frustum.planes[1] = normalizePlane(row3 - row0); // Right
    frustum.planes[2] = normalizePlane(row3 + row1); // Bottom
    frustum.planes[3] = normalizePlane(row3 - row1); // Top
    frustum.planes[4] = normalizePlane(row3 + row2); // Near
    frustum.planes[5] = normalizePlane(row3 - row2); // Far
    return frustum;
}

ChunkRenderer::ChunkRenderer() {
    initialize();
}

ChunkRenderer::~ChunkRenderer() {
    shutdown();
}

void ChunkRenderer::initialize() {
    shutdown();

    storage_.pool.free_count = 0;
    storage_.pool.live_count = 0;
    storage_.index.occupied_count = 0;
    storage_.index.tombstone_count = 0;
    for (MeshId id = 0; id < MAX_CHUNK_MESHES; id++) {
        storage_.pool.free_stack[storage_.pool.free_count++] = id;
        storage_.pool.live_meshes[id] = INVALID_MESH;
    }

    for (ChunkMeshIndexEntry& entry : storage_.index.entries) {
        entry = ChunkMeshIndexEntry{};
    }
}

void ChunkRenderer::shutdown() noexcept {
    for (std::uint32_t i = 0; i < storage_.pool.live_count; i++) {
        const MeshId mesh_id = storage_.pool.live_meshes[i];
        if (mesh_id != INVALID_MESH) {
            storage_.pool.meshes[mesh_id].destroy();
        }
    }

    for (MeshId id = 0; id < MAX_CHUNK_MESHES; id++) {
        storage_.pool.live_meshes[id] = INVALID_MESH;
    }
    for (ChunkMeshIndexEntry& entry : storage_.index.entries) {
        entry = ChunkMeshIndexEntry{};
    }

    storage_.pool.live_count = 0;
    storage_.pool.free_count = 0;
    storage_.index.occupied_count = 0;
    storage_.index.tombstone_count = 0;
}

bool ChunkRenderer::uploadMesh(const ChunkCoord coord,
                               const std::uint64_t revision,
                               CpuChunkMesh&& mesh) {
    MeshId mesh_id = findInIndex(storage_.index, coord);
    if (mesh_id == INVALID_MESH) {
        if (storage_.pool.free_count == 0) {
            return false;
        }

        mesh_id = storage_.pool.free_stack[--storage_.pool.free_count];

        if (!insertIntoIndex(storage_.index, coord, mesh_id)) {
            storage_.pool.free_stack[storage_.pool.free_count++] = mesh_id;
            return false;
        }

        storage_.pool.mesh_coords[mesh_id] = coord;
        storage_.pool.live_meshes[storage_.pool.live_count++] = mesh_id;
    } else {
        storage_.pool.mesh_coords[mesh_id] = coord;
    }

    if (!storage_.pool.meshes[mesh_id].upload(revision, std::move(mesh))) {
        if (storage_.pool.meshes[mesh_id].revision() == INVALID_REVISION) {
            deleteMesh(coord);
        }
        return false;
    }

    return true;
}

void ChunkRenderer::deleteMesh(const ChunkCoord coord) noexcept {
    const MeshId mesh_id = findInIndex(storage_.index, coord);
    if (mesh_id == INVALID_MESH) {
        return;
    }

    storage_.pool.meshes[mesh_id].destroy();
    const bool erased = eraseFromIndex(storage_.index, coord);
    assert(erased);

    for (std::uint32_t i = 0; i < storage_.pool.live_count; i++) {
        if (storage_.pool.live_meshes[i] == mesh_id) {
            const std::uint32_t last_index = storage_.pool.live_count - 1;
            storage_.pool.live_meshes[i] = storage_.pool.live_meshes[last_index];
            storage_.pool.live_meshes[last_index] = INVALID_MESH;
            storage_.pool.live_count--;
            break;
        }
    }

    storage_.pool.free_stack[storage_.pool.free_count++] = mesh_id;
    storage_.pool.mesh_coords[mesh_id] = ChunkCoord{};
}

void ChunkRenderer::draw(Shader* shader) const {
    if (shader == nullptr) {
        return;
    }

    for (std::uint32_t i = 0; i < storage_.pool.live_count; i++) {
        const MeshId mesh_id = storage_.pool.live_meshes[i];
        if (mesh_id == INVALID_MESH) {
            continue;
        }

        const ChunkCoord coord = storage_.pool.mesh_coords[mesh_id];
        const glm::mat4 model = glm::translate(
            glm::mat4{1.0f},
            glm::vec3{
                static_cast<float>(coord.x * BLOCK_X_SIZE),
                0.0f,
                static_cast<float>(coord.z * BLOCK_Z_SIZE)
            }
        );
        shader->setMat4("model", model);
        storage_.pool.meshes[mesh_id].draw(shader);
    }
}

void ChunkRenderer::draw(Shader* shader, const ChunkFrustum& frustum) const {
    if (shader == nullptr) {
        return;
    }

    for (std::uint32_t i = 0; i < storage_.pool.live_count; i++) {
        const MeshId mesh_id = storage_.pool.live_meshes[i];
        if (mesh_id == INVALID_MESH) {
            continue;
        }

        const ChunkCoord coord = storage_.pool.mesh_coords[mesh_id];
        if (!intersectsFrustum(coord, frustum)) {
            continue;
        }

        const glm::mat4 model = glm::translate(
            glm::mat4{1.0f},
            glm::vec3{
                static_cast<float>(coord.x * BLOCK_X_SIZE),
                0.0f,
                static_cast<float>(coord.z * BLOCK_Z_SIZE)
            }
        );
        shader->setMat4("model", model);
        storage_.pool.meshes[mesh_id].draw(shader);
    }
}

const ChunkRendererStorage& ChunkRenderer::storage() const noexcept {
    return storage_;
}

} // namespace torq
