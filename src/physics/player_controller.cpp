#include "player_controller.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

namespace torq {
namespace {

constexpr float COLLISION_SKIN = 0.001f;

struct Aabb {
    glm::vec3 min{};
    glm::vec3 max{};
};

struct CollisionHit {
    bool hit{false};
    int min_x{std::numeric_limits<int>::max()};
    int min_y{std::numeric_limits<int>::max()};
    int min_z{std::numeric_limits<int>::max()};
    int max_x{std::numeric_limits<int>::min()};
    int max_y{std::numeric_limits<int>::min()};
    int max_z{std::numeric_limits<int>::min()};
};

[[nodiscard]] Aabb playerAabb(const glm::vec3 feet_position) noexcept {
    return {
        feet_position + glm::vec3{-PlayerController::HALF_WIDTH,
                                  0.0f,
                                  -PlayerController::HALF_WIDTH},
        feet_position + glm::vec3{PlayerController::HALF_WIDTH,
                                  PlayerController::HEIGHT,
                                  PlayerController::HALF_WIDTH}
    };
}

[[nodiscard]] Aabb blockAabb(const WorldBlockCoord block) noexcept {
    return {
        glm::vec3{
            static_cast<float>(block.x),
            static_cast<float>(block.y),
            static_cast<float>(block.z)
        },
        glm::vec3{
            static_cast<float>(block.x + 1),
            static_cast<float>(block.y + 1),
            static_cast<float>(block.z + 1)
        }
    };
}

[[nodiscard]] bool intersects(const Aabb& a, const Aabb& b) noexcept {
    return a.min.x < b.max.x && a.max.x > b.min.x &&
           a.min.y < b.max.y && a.max.y > b.min.y &&
           a.min.z < b.max.z && a.max.z > b.min.z;
}

[[nodiscard]] int minBlockCoord(const float value) noexcept {
    return static_cast<int>(std::floor(value));
}

[[nodiscard]] int maxBlockCoord(const float value) noexcept {
    return static_cast<int>(std::floor(value - COLLISION_SKIN));
}

[[nodiscard]] CollisionHit findCollision(const Aabb& aabb,
                                         const ChunkCache& chunk_cache) {
    CollisionHit hit{};
    const int min_x = minBlockCoord(aabb.min.x);
    const int min_y = minBlockCoord(aabb.min.y);
    const int min_z = minBlockCoord(aabb.min.z);
    const int max_x = maxBlockCoord(aabb.max.x);
    const int max_y = maxBlockCoord(aabb.max.y);
    const int max_z = maxBlockCoord(aabb.max.z);

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            for (int z = min_z; z <= max_z; z++) {
                if (!chunk_cache.isSolidForPhysics(WorldBlockCoord{x, y, z})) {
                    continue;
                }

                hit.hit = true;
                hit.min_x = std::min(hit.min_x, x);
                hit.min_y = std::min(hit.min_y, y);
                hit.min_z = std::min(hit.min_z, z);
                hit.max_x = std::max(hit.max_x, x);
                hit.max_y = std::max(hit.max_y, y);
                hit.max_z = std::max(hit.max_z, z);
            }
        }
    }

    return hit;
}

[[nodiscard]] glm::vec3 horizontalDirection(glm::vec3 direction) noexcept {
    direction.y = 0.0f;
    const float length = glm::length(direction);
    if (length <= 0.0001f) {
        return glm::vec3{0.0f};
    }

    return direction / length;
}

} // namespace

PlayerController::PlayerController(const glm::vec3 feet_position)
    : position_{feet_position} {
}

void PlayerController::tick(const float dt,
                            const PlayerInputIntent& input,
                            const glm::vec3 camera_front,
                            const glm::vec3 camera_right,
                            const ChunkCache& chunk_cache) {
    const glm::vec3 forward = horizontalDirection(camera_front);
    const glm::vec3 right = horizontalDirection(camera_right);

    glm::vec3 wish_dir{0.0f};
    if (input.move_forward) {
        wish_dir += forward;
    }
    if (input.move_backward) {
        wish_dir -= forward;
    }
    if (input.move_right) {
        wish_dir += right;
    }
    if (input.move_left) {
        wish_dir -= right;
    }

    if (glm::length(wish_dir) > 0.0001f) {
        wish_dir = glm::normalize(wish_dir);
    }

    velocity_.x = wish_dir.x * WALK_SPEED;
    velocity_.z = wish_dir.z * WALK_SPEED;

    if (input.jump && grounded_) {
        velocity_.y = JUMP_SPEED;
        grounded_ = false;
    }

    velocity_.y = std::max(velocity_.y - GRAVITY * dt, -MAX_FALL_SPEED);
    grounded_ = false;

    moveAxis(0, velocity_.x * dt, chunk_cache);
    moveAxis(1, velocity_.y * dt, chunk_cache);
    moveAxis(2, velocity_.z * dt, chunk_cache);
}

void PlayerController::teleportToFeetPosition(const glm::vec3 feet_position,
                                              const bool grounded) noexcept {
    position_ = feet_position;
    velocity_ = glm::vec3{0.0f};
    grounded_ = grounded;
}

void PlayerController::moveAxis(const int axis,
                                const float delta,
                                const ChunkCache& chunk_cache) {
    if (delta == 0.0f) {
        return;
    }

    glm::vec3 candidate_position = position_;
    candidate_position[axis] += delta;

    const CollisionHit hit =
        findCollision(playerAabb(candidate_position), chunk_cache);
    if (!hit.hit) {
        position_ = candidate_position;
        return;
    }

    if (axis == 0) {
        if (delta > 0.0f) {
            position_.x = static_cast<float>(hit.min_x) - HALF_WIDTH - COLLISION_SKIN;
        } else {
            position_.x = static_cast<float>(hit.max_x + 1) + HALF_WIDTH + COLLISION_SKIN;
        }
        velocity_.x = 0.0f;
        return;
    }

    if (axis == 1) {
        if (delta > 0.0f) {
            position_.y = static_cast<float>(hit.min_y) - HEIGHT - COLLISION_SKIN;
        } else {
            position_.y = static_cast<float>(hit.max_y + 1) + COLLISION_SKIN;
            grounded_ = true;
        }
        velocity_.y = 0.0f;
        return;
    }

    if (delta > 0.0f) {
        position_.z = static_cast<float>(hit.min_z) - HALF_WIDTH - COLLISION_SKIN;
    } else {
        position_.z = static_cast<float>(hit.max_z + 1) + HALF_WIDTH + COLLISION_SKIN;
    }
    velocity_.z = 0.0f;
}

glm::vec3 PlayerController::eyePosition() const noexcept {
    return position_ + glm::vec3{0.0f, EYE_HEIGHT, 0.0f};
}

glm::vec3 PlayerController::feetPosition() const noexcept {
    return position_;
}

bool PlayerController::grounded() const noexcept {
    return grounded_;
}

bool PlayerController::overlapsBlock(const WorldBlockCoord block) const noexcept {
    return intersects(playerAabb(position_), blockAabb(block));
}

bool PlayerController::canPlaceBlock(const WorldBlockCoord block) const noexcept {
    return !overlapsBlock(block);
}

bool tryFindSpawnAboveColumn(const ChunkCache& chunk_cache,
                             const int world_x,
                             const int world_z,
                             glm::vec3* out_feet_position) {
    if (out_feet_position == nullptr) {
        return false;
    }

    for (int feet_y = BLOCK_Y_SIZE; feet_y >= 1; feet_y--) {
        const bool has_ground =
            chunk_cache.isSolidForPhysics(WorldBlockCoord{world_x, feet_y - 1, world_z});
        const bool body_clear =
            !chunk_cache.isSolidForPhysics(WorldBlockCoord{world_x, feet_y, world_z}) &&
            !chunk_cache.isSolidForPhysics(WorldBlockCoord{world_x, feet_y + 1, world_z}) &&
            !chunk_cache.isSolidForPhysics(WorldBlockCoord{world_x, feet_y + 2, world_z});

        if (!has_ground || !body_clear) {
            continue;
        }

        *out_feet_position = glm::vec3{
            static_cast<float>(world_x) + 0.5f,
            static_cast<float>(feet_y) + COLLISION_SKIN,
            static_cast<float>(world_z) + 0.5f
        };
        return true;
    }

    return false;
}

BlockEditResult setBlockWithPlayerCollision(ChunkCache& chunk_cache,
                                            const PlayerController& player,
                                            const WorldBlockCoord pos,
                                            const BlockData block) {
    if (block.id != BlockMap::air && player.overlapsBlock(pos)) {
        return BlockEditResult::BlockedByPlayer;
    }

    return chunk_cache.setBlock(pos, block);
}

} // namespace torq
