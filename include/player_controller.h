#pragma once

#include "chunk_cache.h"

#include <glm/glm.hpp>

namespace torq {

struct PlayerInputIntent {
    bool move_forward{false};
    bool move_backward{false};
    bool move_left{false};
    bool move_right{false};
    bool jump{false};
    bool descend_or_crouch{false};
};

class PlayerController {
public:
    static constexpr float HALF_WIDTH = 0.30f;
    static constexpr float HEIGHT = 1.80f;
    static constexpr float EYE_HEIGHT = 1.62f;
    static constexpr float WALK_SPEED = 20.0f;
    static constexpr float JUMP_SPEED = 10.0f;
    static constexpr float GRAVITY = 24.0f;
    static constexpr float MAX_FALL_SPEED = 50.0f;

    explicit PlayerController(glm::vec3 feet_position);

    void tick(float dt,
              const PlayerInputIntent& input,
              glm::vec3 camera_front,
              glm::vec3 camera_right,
              const ChunkCache& chunk_cache);
    void teleportToFeetPosition(glm::vec3 feet_position,
                                bool grounded = false) noexcept;

    [[nodiscard]] glm::vec3 eyePosition() const noexcept;
    [[nodiscard]] glm::vec3 feetPosition() const noexcept;
    [[nodiscard]] bool grounded() const noexcept;
    [[nodiscard]] bool overlapsBlock(WorldBlockCoord block) const noexcept;
    [[nodiscard]] bool canPlaceBlock(WorldBlockCoord block) const noexcept;

private:
    void moveAxis(int axis, float delta, const ChunkCache& chunk_cache);

    glm::vec3 position_{};
    glm::vec3 velocity_{};
    bool grounded_{false};
};

[[nodiscard]] bool tryFindSpawnAboveColumn(const ChunkCache& chunk_cache,
                                           int world_x,
                                           int world_z,
                                           glm::vec3* out_feet_position);
[[nodiscard]] BlockEditResult setBlockWithPlayerCollision(ChunkCache& chunk_cache,
                                                          const PlayerController& player,
                                                          WorldBlockCoord pos,
                                                          BlockData block);

} // namespace torq
