#pragma once

#include <cstdint>

#include <glm/glm.hpp>

using textureLayerType = uint32_t;

struct PrimitiveVertex {
	glm::vec3 pos;
	glm::vec3 color;
};

struct TextureVertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 tex;
	textureLayerType texLayer;
};
