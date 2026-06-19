#pragma once

#include "chunk_streaming.h"
#include "shader.h"

#include <cstdint>

#include <glad/glad.h>

namespace torq {

class ChunkMesher {
public:
    static CpuChunkMesh build(const ChunkData& chunk,
                              const ChunkNeighborMasks& neighbor_masks);
};

class ChunkMesh {
public:
    ChunkMesh() = default;
    ~ChunkMesh();

    ChunkMesh(const ChunkMesh&) = delete;
    ChunkMesh& operator=(const ChunkMesh&) = delete;

    ChunkMesh(ChunkMesh&& other) noexcept;
    ChunkMesh& operator=(ChunkMesh&& other) noexcept;

    [[nodiscard]] bool upload(std::uint64_t revision, CpuChunkMesh&& mesh);
    void destroy() noexcept;
    void draw(Shader* shader) const;

    [[nodiscard]] bool allocated() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] std::uint32_t vertexCount() const noexcept;

private:
    GLuint vao{0};
    GLuint vbo{0};
    std::uint32_t vertex_count{0};
    std::uint64_t uploaded_revision{INVALID_REVISION};
};

} // namespace torq
