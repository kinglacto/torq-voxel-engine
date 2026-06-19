#include "chunk_mesh.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace torq {
namespace {

static_assert(BLOCK_X_SIZE == 16 && BLOCK_Y_SIZE == 16 && BLOCK_Z_SIZE == 16,
              "The binary greedy mesher currently expects 16x16x16 chunks.");

using BitRow = std::uint16_t;

constexpr BitRow CHUNK_ROW_MASK = 0xFFFFU;
constexpr int FACE_AXIS_SIZE = 16;

struct AxisOccupancyMasks {
    std::array<BitRow, FACE_AXIS_SIZE * FACE_AXIS_SIZE> z_rows{};
    std::array<BitRow, FACE_AXIS_SIZE * FACE_AXIS_SIZE> x_rows{};
    std::array<BitRow, FACE_AXIS_SIZE * FACE_AXIS_SIZE> y_rows{};
};

struct FaceSlice {
    std::array<BitRow, FACE_AXIS_SIZE> rows{};
    std::array<textureLayerType, FACE_AXIS_SIZE * FACE_AXIS_SIZE> layers{};
};

[[nodiscard]] std::size_t blockIndex(const int x, const int y, const int z) noexcept {
    return static_cast<std::size_t>((y * BLOCK_X_SIZE + x) * BLOCK_Z_SIZE + z);
}

[[nodiscard]] std::size_t rowIndex(const int a, const int b) noexcept {
    return static_cast<std::size_t>(a * FACE_AXIS_SIZE + b);
}

[[nodiscard]] std::size_t cellIndex(const int u, const int v) noexcept {
    return static_cast<std::size_t>(v * FACE_AXIS_SIZE + u);
}

[[nodiscard]] BitRow bit(const int index) noexcept {
    return static_cast<BitRow>(1U << static_cast<unsigned>(index));
}

[[nodiscard]] int firstSetBit(const BitRow row) noexcept {
    return std::countr_zero(static_cast<unsigned int>(row));
}

[[nodiscard]] BitRow clearMask(const BitRow mask) noexcept {
    return static_cast<BitRow>(CHUNK_ROW_MASK ^ mask);
}

[[nodiscard]] BitRow previousAxisVisibleBits(const BitRow solid) noexcept {
    const BitRow previous_solid =
        static_cast<BitRow>((static_cast<std::uint32_t>(solid) << 1U) &
                            CHUNK_ROW_MASK);
    return static_cast<BitRow>(solid & clearMask(previous_solid));
}

[[nodiscard]] BitRow nextAxisVisibleBits(const BitRow solid) noexcept {
    const BitRow next_solid = static_cast<BitRow>(solid >> 1U);
    return static_cast<BitRow>(solid & clearMask(next_solid));
}

[[nodiscard]] BitRow runMask(const int start, const int width) noexcept {
    const std::uint32_t mask = ((1U << static_cast<unsigned>(width)) - 1U)
        << static_cast<unsigned>(start);
    return static_cast<BitRow>(mask & CHUNK_ROW_MASK);
}

[[nodiscard]] textureLayerType textureLayerForFace(const BlockData block,
                                                   const int face) noexcept {
    if (blockTexMap == nullptr || block.id >= NUM_BLOCK_TYPES) {
        return static_cast<textureLayerType>(TexMap::side_dirt);
    }

    const TexMap tex = blockTexMap[block.id].texId[face];
    return isValidTextureId(tex)
        ? static_cast<textureLayerType>(tex)
        : static_cast<textureLayerType>(TexMap::side_dirt);
}

[[nodiscard]] AxisOccupancyMasks buildOccupancyMasks(const ChunkData& chunk) noexcept {
    AxisOccupancyMasks masks{};

    for (int y = 0; y < BLOCK_Y_SIZE; y++) {
        for (int x = 0; x < BLOCK_X_SIZE; x++) {
            for (int z = 0; z < BLOCK_Z_SIZE; z++) {
                if (chunk.blocks[blockIndex(x, y, z)].id == BlockMap::air) {
                    continue;
                }

                masks.z_rows[rowIndex(y, x)] |= bit(z);
                masks.x_rows[rowIndex(y, z)] |= bit(x);
                masks.y_rows[rowIndex(z, x)] |= bit(y);
            }
        }
    }

    return masks;
}

void setFaceCell(FaceSlice& slice,
                 const int u,
                 const int v,
                 const textureLayerType layer) noexcept {
    slice.rows[static_cast<std::size_t>(v)] |= bit(u);
    slice.layers[cellIndex(u, v)] = layer;
}

void collectZFaceCells(const ChunkData& chunk,
                       const int face,
                       const int y,
                       const int x,
                       BitRow visible,
                       std::array<FaceSlice, BLOCK_Z_SIZE>& slices) noexcept {
    while (visible != 0) {
        const int z = firstSetBit(visible);
        const BlockData block = chunk.blocks[blockIndex(x, y, z)];
        setFaceCell(slices[static_cast<std::size_t>(z)],
                    x,
                    y,
                    textureLayerForFace(block, face));
        visible = static_cast<BitRow>(visible & static_cast<BitRow>(visible - 1U));
    }
}

void collectXFaceCells(const ChunkData& chunk,
                       const int face,
                       const int y,
                       const int z,
                       BitRow visible,
                       std::array<FaceSlice, BLOCK_X_SIZE>& slices) noexcept {
    while (visible != 0) {
        const int x = firstSetBit(visible);
        const BlockData block = chunk.blocks[blockIndex(x, y, z)];
        setFaceCell(slices[static_cast<std::size_t>(x)],
                    z,
                    y,
                    textureLayerForFace(block, face));
        visible = static_cast<BitRow>(visible & static_cast<BitRow>(visible - 1U));
    }
}

void collectYFaceCells(const ChunkData& chunk,
                       const int face,
                       const int z,
                       const int x,
                       BitRow visible,
                       std::array<FaceSlice, BLOCK_Y_SIZE>& slices) noexcept {
    while (visible != 0) {
        const int y = firstSetBit(visible);
        const BlockData block = chunk.blocks[blockIndex(x, y, z)];
        setFaceCell(slices[static_cast<std::size_t>(y)],
                    x,
                    z,
                    textureLayerForFace(block, face));
        visible = static_cast<BitRow>(visible & static_cast<BitRow>(visible - 1U));
    }
}

void buildFaceSlices(const ChunkData& chunk,
                     const ChunkNeighborMasks& neighbor_masks,
                     const AxisOccupancyMasks& occupancy,
                     std::array<FaceSlice, BLOCK_Z_SIZE>& front_slices,
                     std::array<FaceSlice, BLOCK_Z_SIZE>& back_slices,
                     std::array<FaceSlice, BLOCK_X_SIZE>& left_slices,
                     std::array<FaceSlice, BLOCK_X_SIZE>& right_slices,
                     std::array<FaceSlice, BLOCK_Y_SIZE>& bottom_slices,
                     std::array<FaceSlice, BLOCK_Y_SIZE>& top_slices) noexcept {
    for (int y = 0; y < BLOCK_Y_SIZE; y++) {
        for (int x = 0; x < BLOCK_X_SIZE; x++) {
            const BitRow solid = occupancy.z_rows[rowIndex(y, x)];
            BitRow visible_front = previousAxisVisibleBits(solid);
            BitRow visible_back = nextAxisVisibleBits(solid);

            if (neighbor_masks.neg_z.present &&
                testChunkBorderMaskBit(neighbor_masks.neg_z,
                                       chunkZBorderBitIndex(y, x))) {
                visible_front = static_cast<BitRow>(visible_front & clearMask(bit(0)));
            }

            if (neighbor_masks.pos_z.present &&
                testChunkBorderMaskBit(neighbor_masks.pos_z,
                                       chunkZBorderBitIndex(y, x))) {
                visible_back = static_cast<BitRow>(
                    visible_back & clearMask(bit(BLOCK_Z_SIZE - 1))
                );
            }

            collectZFaceCells(chunk, blockDirectionIndex::front, y, x,
                              visible_front, front_slices);
            collectZFaceCells(chunk, blockDirectionIndex::back, y, x,
                              visible_back, back_slices);
        }
    }

    for (int y = 0; y < BLOCK_Y_SIZE; y++) {
        for (int z = 0; z < BLOCK_Z_SIZE; z++) {
            const BitRow solid = occupancy.x_rows[rowIndex(y, z)];
            BitRow visible_left = previousAxisVisibleBits(solid);
            BitRow visible_right = nextAxisVisibleBits(solid);

            if (neighbor_masks.neg_x.present &&
                testChunkBorderMaskBit(neighbor_masks.neg_x,
                                       chunkXBorderBitIndex(y, z))) {
                visible_left = static_cast<BitRow>(visible_left & clearMask(bit(0)));
            }

            if (neighbor_masks.pos_x.present &&
                testChunkBorderMaskBit(neighbor_masks.pos_x,
                                       chunkXBorderBitIndex(y, z))) {
                visible_right = static_cast<BitRow>(
                    visible_right & clearMask(bit(BLOCK_X_SIZE - 1))
                );
            }

            collectXFaceCells(chunk, blockDirectionIndex::left, y, z,
                              visible_left, left_slices);
            collectXFaceCells(chunk, blockDirectionIndex::right, y, z,
                              visible_right, right_slices);
        }
    }

    for (int z = 0; z < BLOCK_Z_SIZE; z++) {
        for (int x = 0; x < BLOCK_X_SIZE; x++) {
            const BitRow solid = occupancy.y_rows[rowIndex(z, x)];
            const BitRow visible_bottom = previousAxisVisibleBits(solid);
            const BitRow visible_top = nextAxisVisibleBits(solid);

            collectYFaceCells(chunk, blockDirectionIndex::bottom, z, x,
                              visible_bottom, bottom_slices);
            collectYFaceCells(chunk, blockDirectionIndex::top, z, x,
                              visible_top, top_slices);
        }
    }
}

void appendVertex(CpuChunkMesh& mesh,
                  const glm::vec3& position,
                  const glm::vec3& normal,
                  const glm::vec2& tex_coord,
                  const textureLayerType layer) {
    TextureVertex out{};
    out.pos = position;
    out.normal = normal;
    out.tex = tex_coord;
    out.texLayer = layer;
    mesh.vertices.push_back(out);
}

void appendGreedyQuad(CpuChunkMesh& mesh,
                      const int face,
                      const int slice,
                      const int u,
                      const int v,
                      const int width,
                      const int height,
                      const textureLayerType layer) {
    const float u0 = static_cast<float>(u) - 0.5f;
    const float u1 = static_cast<float>(u + width) - 0.5f;
    const float v0 = static_cast<float>(v) - 0.5f;
    const float v1 = static_cast<float>(v + height) - 0.5f;
    const float neg_plane = static_cast<float>(slice) - 0.5f;
    const float pos_plane = static_cast<float>(slice) + 0.5f;
    const float tex_w = static_cast<float>(width);
    const float tex_h = static_cast<float>(height);

    switch (face) {
    case blockDirectionIndex::front: {
        const glm::vec3 normal{0.0f, 0.0f, -1.0f};
        const glm::vec3 p0{u0, v0, neg_plane};
        const glm::vec3 p1{u0, v1, neg_plane};
        const glm::vec3 p2{u1, v1, neg_plane};
        const glm::vec3 p3{u1, v0, neg_plane};
        appendVertex(mesh, p0, normal, {0.0f, 0.0f}, layer);
        appendVertex(mesh, p1, normal, {0.0f, tex_h}, layer);
        appendVertex(mesh, p2, normal, {tex_w, tex_h}, layer);
        appendVertex(mesh, p2, normal, {tex_w, tex_h}, layer);
        appendVertex(mesh, p3, normal, {tex_w, 0.0f}, layer);
        appendVertex(mesh, p0, normal, {0.0f, 0.0f}, layer);
        break;
    }

    case blockDirectionIndex::back: {
        const glm::vec3 normal{0.0f, 0.0f, 1.0f};
        const glm::vec3 p0{u0, v0, pos_plane};
        const glm::vec3 p1{u1, v0, pos_plane};
        const glm::vec3 p2{u1, v1, pos_plane};
        const glm::vec3 p3{u0, v1, pos_plane};
        appendVertex(mesh, p0, normal, {0.0f, 0.0f}, layer);
        appendVertex(mesh, p1, normal, {tex_w, 0.0f}, layer);
        appendVertex(mesh, p2, normal, {tex_w, tex_h}, layer);
        appendVertex(mesh, p2, normal, {tex_w, tex_h}, layer);
        appendVertex(mesh, p3, normal, {0.0f, tex_h}, layer);
        appendVertex(mesh, p0, normal, {0.0f, 0.0f}, layer);
        break;
    }

    case blockDirectionIndex::left: {
        const glm::vec3 normal{-1.0f, 0.0f, 0.0f};
        const glm::vec3 p0{neg_plane, v1, u1};
        const glm::vec3 p1{neg_plane, v1, u0};
        const glm::vec3 p2{neg_plane, v0, u0};
        const glm::vec3 p3{neg_plane, v0, u1};
        appendVertex(mesh, p0, normal, {0.0f, tex_h}, layer);
        appendVertex(mesh, p1, normal, {tex_w, tex_h}, layer);
        appendVertex(mesh, p2, normal, {tex_w, 0.0f}, layer);
        appendVertex(mesh, p2, normal, {tex_w, 0.0f}, layer);
        appendVertex(mesh, p3, normal, {0.0f, 0.0f}, layer);
        appendVertex(mesh, p0, normal, {0.0f, tex_h}, layer);
        break;
    }

    case blockDirectionIndex::right: {
        const glm::vec3 normal{1.0f, 0.0f, 0.0f};
        const glm::vec3 p0{pos_plane, v1, u1};
        const glm::vec3 p1{pos_plane, v0, u0};
        const glm::vec3 p2{pos_plane, v1, u0};
        const glm::vec3 p3{pos_plane, v0, u1};
        appendVertex(mesh, p0, normal, {0.0f, tex_h}, layer);
        appendVertex(mesh, p1, normal, {tex_w, 0.0f}, layer);
        appendVertex(mesh, p2, normal, {tex_w, tex_h}, layer);
        appendVertex(mesh, p1, normal, {tex_w, 0.0f}, layer);
        appendVertex(mesh, p0, normal, {0.0f, tex_h}, layer);
        appendVertex(mesh, p3, normal, {0.0f, 0.0f}, layer);
        break;
    }

    case blockDirectionIndex::bottom: {
        const glm::vec3 normal{0.0f, -1.0f, 0.0f};
        const glm::vec3 p0{u0, neg_plane, v0};
        const glm::vec3 p1{u1, neg_plane, v0};
        const glm::vec3 p2{u1, neg_plane, v1};
        const glm::vec3 p3{u0, neg_plane, v1};
        appendVertex(mesh, p0, normal, {0.0f, tex_h}, layer);
        appendVertex(mesh, p1, normal, {tex_w, tex_h}, layer);
        appendVertex(mesh, p2, normal, {tex_w, 0.0f}, layer);
        appendVertex(mesh, p2, normal, {tex_w, 0.0f}, layer);
        appendVertex(mesh, p3, normal, {0.0f, 0.0f}, layer);
        appendVertex(mesh, p0, normal, {0.0f, tex_h}, layer);
        break;
    }

    case blockDirectionIndex::top: {
        const glm::vec3 normal{0.0f, 1.0f, 0.0f};
        const glm::vec3 p0{u0, pos_plane, v0};
        const glm::vec3 p1{u1, pos_plane, v1};
        const glm::vec3 p2{u1, pos_plane, v0};
        const glm::vec3 p3{u0, pos_plane, v1};
        appendVertex(mesh, p0, normal, {0.0f, tex_h}, layer);
        appendVertex(mesh, p1, normal, {tex_w, 0.0f}, layer);
        appendVertex(mesh, p2, normal, {tex_w, tex_h}, layer);
        appendVertex(mesh, p1, normal, {tex_w, 0.0f}, layer);
        appendVertex(mesh, p0, normal, {0.0f, tex_h}, layer);
        appendVertex(mesh, p3, normal, {0.0f, 0.0f}, layer);
        break;
    }

    default:
        break;
    }
}

[[nodiscard]] bool sameLayerRun(const FaceSlice& slice,
                                const int u,
                                const int v,
                                const int width,
                                const textureLayerType layer) noexcept {
    for (int offset = 0; offset < width; offset++) {
        if (slice.layers[cellIndex(u + offset, v)] != layer) {
            return false;
        }
    }

    return true;
}

void appendGreedySlice(CpuChunkMesh& mesh,
                       const FaceSlice& slice,
                       const int face,
                       const int slice_index) {
    std::array<BitRow, FACE_AXIS_SIZE> rows = slice.rows;

    for (int v = 0; v < FACE_AXIS_SIZE; v++) {
        while (rows[static_cast<std::size_t>(v)] != 0) {
            const int u = firstSetBit(rows[static_cast<std::size_t>(v)]);
            const textureLayerType layer = slice.layers[cellIndex(u, v)];

            int width = 1;
            while (u + width < FACE_AXIS_SIZE &&
                   (rows[static_cast<std::size_t>(v)] & bit(u + width)) != 0 &&
                   slice.layers[cellIndex(u + width, v)] == layer) {
                width++;
            }

            int height = 1;
            const BitRow quad_mask = runMask(u, width);
            while (v + height < FACE_AXIS_SIZE &&
                   (rows[static_cast<std::size_t>(v + height)] & quad_mask) ==
                       quad_mask &&
                   sameLayerRun(slice, u, v + height, width, layer)) {
                height++;
            }

            appendGreedyQuad(mesh, face, slice_index, u, v, width, height, layer);

            const BitRow inverse_quad_mask = clearMask(quad_mask);
            for (int row = 0; row < height; row++) {
                rows[static_cast<std::size_t>(v + row)] = static_cast<BitRow>(
                    rows[static_cast<std::size_t>(v + row)] & inverse_quad_mask
                );
            }
        }
    }
}

template <std::size_t N>
void appendGreedySlices(CpuChunkMesh& mesh,
                        const std::array<FaceSlice, N>& slices,
                        const int face) {
    for (std::size_t slice = 0; slice < N; slice++) {
        appendGreedySlice(mesh, slices[slice], face, static_cast<int>(slice));
    }
}

} // namespace

CpuChunkMesh ChunkMesher::build(const ChunkData& chunk,
                                const ChunkNeighborMasks& neighbor_masks) {
    CpuChunkMesh mesh{};
    mesh.vertices.reserve(10000);

    const AxisOccupancyMasks occupancy = buildOccupancyMasks(chunk);
    std::array<FaceSlice, BLOCK_Z_SIZE> front_slices{};
    std::array<FaceSlice, BLOCK_Z_SIZE> back_slices{};
    std::array<FaceSlice, BLOCK_X_SIZE> left_slices{};
    std::array<FaceSlice, BLOCK_X_SIZE> right_slices{};
    std::array<FaceSlice, BLOCK_Y_SIZE> bottom_slices{};
    std::array<FaceSlice, BLOCK_Y_SIZE> top_slices{};

    buildFaceSlices(chunk,
                    neighbor_masks,
                    occupancy,
                    front_slices,
                    back_slices,
                    left_slices,
                    right_slices,
                    bottom_slices,
                    top_slices);

    appendGreedySlices(mesh, front_slices, blockDirectionIndex::front);
    appendGreedySlices(mesh, back_slices, blockDirectionIndex::back);
    appendGreedySlices(mesh, left_slices, blockDirectionIndex::left);
    appendGreedySlices(mesh, right_slices, blockDirectionIndex::right);
    appendGreedySlices(mesh, bottom_slices, blockDirectionIndex::bottom);
    appendGreedySlices(mesh, top_slices, blockDirectionIndex::top);

    return mesh;
}

ChunkMesh::~ChunkMesh() {
    destroy();
}

ChunkMesh::ChunkMesh(ChunkMesh&& other) noexcept
    : vao{std::exchange(other.vao, 0)},
      vbo{std::exchange(other.vbo, 0)},
      vertex_count{std::exchange(other.vertex_count, 0)},
      uploaded_revision{std::exchange(other.uploaded_revision, INVALID_REVISION)} {
}

ChunkMesh& ChunkMesh::operator=(ChunkMesh&& other) noexcept {
    if (this != &other) {
        destroy();
        vao = std::exchange(other.vao, 0);
        vbo = std::exchange(other.vbo, 0);
        vertex_count = std::exchange(other.vertex_count, 0);
        uploaded_revision = std::exchange(other.uploaded_revision, INVALID_REVISION);
    }

    return *this;
}

bool ChunkMesh::upload(const std::uint64_t revision, CpuChunkMesh&& mesh) {
    const auto new_vertex_count = static_cast<std::uint32_t>(mesh.vertices.size());
    if (new_vertex_count == 0) {
        destroy();
        uploaded_revision = revision;
        return true;
    }

    GLuint new_vao = 0;
    GLuint new_vbo = 0;
    glGenVertexArrays(1, &new_vao);
    glGenBuffers(1, &new_vbo);
    if (new_vao == 0 || new_vbo == 0) {
        if (new_vao != 0) {
            glDeleteVertexArrays(1, &new_vao);
        }
        if (new_vbo != 0) {
            glDeleteBuffers(1, &new_vbo);
        }
        return false;
    }

    glBindVertexArray(new_vao);
    glBindBuffer(GL_ARRAY_BUFFER, new_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(TextureVertex)),
                 mesh.vertices.data(),
                 GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TextureVertex), nullptr);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TextureVertex),
                          reinterpret_cast<void*>(offsetof(TextureVertex, normal)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(TextureVertex),
                          reinterpret_cast<void*>(offsetof(TextureVertex, tex)));
    glEnableVertexAttribArray(2);

    glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(TextureVertex),
                           reinterpret_cast<void*>(offsetof(TextureVertex, texLayer)));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    const bool ok = glGetError() == GL_NO_ERROR;
    if (!ok) {
        glDeleteVertexArrays(1, &new_vao);
        glDeleteBuffers(1, &new_vbo);
        return false;
    }

    destroy();
    vao = new_vao;
    vbo = new_vbo;
    vertex_count = new_vertex_count;
    uploaded_revision = revision;
    return true;
}

void ChunkMesh::destroy() noexcept {
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }

    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }

    vertex_count = 0;
    uploaded_revision = INVALID_REVISION;
}

void ChunkMesh::draw(Shader* shader) const {
    (void)shader;
    if (vao == 0 || vertex_count == 0) {
        return;
    }

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertex_count));
    glBindVertexArray(0);
}

bool ChunkMesh::allocated() const noexcept {
    return uploaded_revision != INVALID_REVISION;
}

std::uint64_t ChunkMesh::revision() const noexcept {
    return uploaded_revision;
}

std::uint32_t ChunkMesh::vertexCount() const noexcept {
    return vertex_count;
}

} // namespace torq
