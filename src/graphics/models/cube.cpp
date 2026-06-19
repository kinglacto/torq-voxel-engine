#include "cube.h"
#include <iostream>
#include <utility>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

Cube::Cube(const glm::vec3& pos, const glm::vec3& size, Shader* shader, Texture* texture):
	size(size), position(pos), shader(shader), texture(texture) {
}

Cube::Cube(const glm::vec3& pos, const glm::vec3& size) :
	size(size), position(pos) {
}

Cube::Cube(const glm::vec3& pos, const float size, Shader* shader, Texture* texture):
	size(glm::vec3(size)), position(pos), shader(shader), texture(texture) {
}

Cube::Cube(const glm::vec3& pos, const float size) :
	size(glm::vec3(size)), position(pos) {
}

Cube::~Cube() = default;

void Cube::init() {
	model = glm::mat4(1.0f);
	if (!shader) {
		std::cerr << "Failed to initialize the cube, shader is NULL" << std::endl;
		return;
	}

	if (!texture) {
		std::cerr << "Failed to initialize the cube, texture is NULL" << std::endl;
		return;
	}

	if (!isValidTextureId(TexMap::side_dirt)) {
		std::cerr << "Failed to initialize the cube, side_dirt texture id is invalid" << std::endl;
		return;
	}

	cleanup();

	constexpr int noVertices = 36;

    float v[] = {
			// as seen down onto the x-z plane

			// up
        	-0.5f, -0.5f, -0.5f,     0.0f,  0.0f, -1.0f,    0.0f, 0.0f,
             0.5f, -0.5f, -0.5f,     0.0f,  0.0f, -1.0f,    1.0f, 0.0f,
             0.5f,  0.5f, -0.5f,     0.0f,  0.0f, -1.0f,    1.0f, 1.0f,
             0.5f,  0.5f, -0.5f,     0.0f,  0.0f, -1.0f,    1.0f, 1.0f,
            -0.5f,  0.5f, -0.5f,     0.0f,  0.0f, -1.0f,    0.0f, 1.0f,
            -0.5f, -0.5f, -0.5f,     0.0f,  0.0f, -1.0f,    0.0f, 0.0f,

			// down
            -0.5f, -0.5f,  0.5f,     0.0f,  0.0f,  1.0f,    0.0f, 0.0f,
             0.5f, -0.5f,  0.5f,     0.0f,  0.0f,  1.0f,    1.0f, 0.0f,
             0.5f,  0.5f,  0.5f,     0.0f,  0.0f,  1.0f,    1.0f, 1.0f,
             0.5f,  0.5f,  0.5f,     0.0f,  0.0f,  1.0f,    1.0f, 1.0f,
            -0.5f,  0.5f,  0.5f,     0.0f,  0.0f,  1.0f,    0.0f, 1.0f,
            -0.5f, -0.5f,  0.5f,     0.0f,  0.0f,  1.0f,    0.0f, 0.0f,

			// left
            -0.5f,  0.5f,  0.5f,    -1.0f,  0.0f,  0.0f,    0.0f, 1.0f,
            -0.5f,  0.5f, -0.5f,    -1.0f,  0.0f,  0.0f,    1.0f, 1.0f,
            -0.5f, -0.5f, -0.5f,    -1.0f,  0.0f,  0.0f,    1.0f, 0.0f,
            -0.5f, -0.5f, -0.5f,    -1.0f,  0.0f,  0.0f,    1.0f, 0.0f,
            -0.5f, -0.5f,  0.5f,    -1.0f,  0.0f,  0.0f,    0.0f, 0.0f,
            -0.5f,  0.5f,  0.5f,    -1.0f,  0.0f,  0.0f,    0.0f, 1.0f,

			// right
             0.5f,  0.5f,  0.5f,     1.0f,  0.0f,  0.0f,    0.0f, 1.0f,
             0.5f,  0.5f, -0.5f,     1.0f,  0.0f,  0.0f,    1.0f, 1.0f,
             0.5f, -0.5f, -0.5f,     1.0f,  0.0f,  0.0f,    1.0f, 0.0f,
             0.5f, -0.5f, -0.5f,     1.0f,  0.0f,  0.0f,    1.0f, 0.0f,
             0.5f, -0.5f,  0.5f,     1.0f,  0.0f,  0.0f,    0.0f, 0.0f,
             0.5f,  0.5f,  0.5f,     1.0f,  0.0f,  0.0f,    0.0f, 1.0f,

			 // bottom
            -0.5f, -0.5f, -0.5f,     0.0f, -1.0f,  0.0f,    0.0f, 1.0f,
             0.5f, -0.5f, -0.5f,     0.0f, -1.0f,  0.0f,    1.0f, 1.0f,
             0.5f, -0.5f,  0.5f,     0.0f, -1.0f,  0.0f,    1.0f, 0.0f,
             0.5f, -0.5f,  0.5f,     0.0f, -1.0f,  0.0f,    1.0f, 0.0f,
            -0.5f, -0.5f,  0.5f,     0.0f, -1.0f,  0.0f,    0.0f, 0.0f,
            -0.5f, -0.5f, -0.5f,     0.0f, -1.0f,  0.0f,    0.0f, 1.0f,

			// top
            -0.5f,  0.5f, -0.5f,     0.0f,  1.0f,  0.0f,    0.0f, 1.0f,
             0.5f,  0.5f, -0.5f,     0.0f,  1.0f,  0.0f,    1.0f, 1.0f,
             0.5f,  0.5f,  0.5f,     0.0f,  1.0f,  0.0f,    1.0f, 0.0f,
             0.5f,  0.5f,  0.5f,     0.0f,  1.0f,  0.0f,    1.0f, 0.0f,
            -0.5f,  0.5f,  0.5f,     0.0f,  1.0f,  0.0f,    0.0f, 0.0f,
            -0.5f,  0.5f, -0.5f,     0.0f,  1.0f,  0.0f,    0.0f, 1.0f
    };

	std::vector<TextureVertex> vertices(noVertices);
	if (!texture->activateAt(0)) {
		std::cerr << "Failed to initialize the cube, texture activation failed" << std::endl;
		return;
	}
	shader->activate();
    shader->setInt("texture1", texture->getUnit());
	for (int i = 0; i < noVertices; i++) {
		int base = i * 8;
		vertices[i].pos   = glm::vec3(v[base], v[base+1], v[base+2]);
		vertices[i].normal = glm::vec3(v[base+3], v[base+4], v[base+5]);
		vertices[i].tex = glm::vec2(v[base+6], v[base + 7]);
		vertices[i].texLayer = static_cast<textureLayerType>(TexMap::side_dirt);
	}
	meshes.emplace_back(std::move(vertices));
}


void Cube::render() {
	if (!shader) {
		std::cerr << "Failed to render the cube, shader is NULL" << std::endl;
		return;
	}

	if (!texture) {
		std::cerr << "Failed to render the cube, texture is NULL" << std::endl;
		return;
	}

	shader->activate();
	model = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), size);
	shader->setMat4("model", model);

	renderAll();
}

void Cube::setShader(Shader *shader){
	this->shader = shader;
}

void Cube::setTexture(Texture *texture){
	this->texture = texture;
}

bool Cube::setSize(const float size) {
	if (size > 0.0f) {
		Cube::size = glm::vec3(size);
		return true;
	}

	return false;
}

bool Cube::setSize(const glm::vec3& size) {
	if (size.x > 0.0f && size.y > 0.0f && size.z > 0.0f) {
		Cube::size = size;
		return true;
	}

	return false;
}

void Cube::setPosition(const glm::vec3& pos) {
	position = pos;
}
