#pragma once

#include "chunk_mesh.h"

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

namespace torq {

using MeshId = std::uint32_t;

inline constexpr MeshId INVALID_MESH = UINT32_MAX;
inline constexpr std::uint32_t MAX_CHUNK_MESHES = MAX_CHUNK_SLOTS;
inline constexpr std::uint32_t MAX_MESH_INDEX_ENTRIES = MAX_CHUNK_INDEX_ENTRIES;

struct ChunkMeshIndexEntry {
    ChunkCoord coord{};
    MeshId mesh{INVALID_MESH};
    IndexState state{IndexState::Empty};
};

struct ChunkMeshPool {
    std::array<ChunkMesh, MAX_CHUNK_MESHES> meshes{};
    std::array<ChunkCoord, MAX_CHUNK_MESHES> mesh_coords{};
    std::array<MeshId, MAX_CHUNK_MESHES> free_stack{};
    std::uint32_t free_count{0};

    std::array<MeshId, MAX_CHUNK_MESHES> live_meshes{};
    std::uint32_t live_count{0};
};

struct ChunkMeshIndex {
    std::array<ChunkMeshIndexEntry, MAX_MESH_INDEX_ENTRIES> entries{};
    std::uint32_t occupied_count{0};
    std::uint32_t tombstone_count{0};
};

struct ChunkRendererStorage {
    ChunkMeshPool pool{};
    ChunkMeshIndex index{};
};

struct ChunkFrustum {
    std::array<glm::vec4, 6> planes{};
};

[[nodiscard]] ChunkFrustum makeChunkFrustum(const glm::mat4& view_projection) noexcept;

class ChunkRenderer {
public:
    ChunkRenderer();
    ~ChunkRenderer();

    ChunkRenderer(const ChunkRenderer&) = delete;
    ChunkRenderer& operator=(const ChunkRenderer&) = delete;

    ChunkRenderer(ChunkRenderer&&) noexcept = default;
    ChunkRenderer& operator=(ChunkRenderer&&) noexcept = default;

    void initialize();
    void shutdown() noexcept;

    [[nodiscard]] bool uploadMesh(ChunkCoord coord,
                                  std::uint64_t revision,
                                  CpuChunkMesh&& mesh);
    void deleteMesh(ChunkCoord coord) noexcept;
    void draw(Shader* shader) const;
    void draw(Shader* shader, const ChunkFrustum& frustum) const;

    [[nodiscard]] const ChunkRendererStorage& storage() const noexcept;

private:
    ChunkRendererStorage storage_{};
};

} // namespace torq
