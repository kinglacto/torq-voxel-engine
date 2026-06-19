#pragma once

#include <vector>
#include "shader.h"
#include "texture.h"
#include "vertex.h"

class PrimitiveMesh {
private:
	bool elementDraw{false};
	std::vector<PrimitiveVertex> vertices;
	std::vector<unsigned int> indices;
	unsigned int VAO{0}, VBO{0}, EBO{0};
	int vertexCount{0};
	int indexCount{0};

	void setup();

public:
	PrimitiveMesh() = default;
	explicit PrimitiveMesh(std::vector<PrimitiveVertex>&& vertices);
	PrimitiveMesh(std::vector<PrimitiveVertex>&& vertices, std::vector<unsigned int>&& indices);
	~PrimitiveMesh();

	PrimitiveMesh(const PrimitiveMesh&) = delete;
	PrimitiveMesh& operator=(const PrimitiveMesh&) = delete;
	PrimitiveMesh(PrimitiveMesh&& other) noexcept;
	PrimitiveMesh& operator=(PrimitiveMesh&& other) noexcept;

	void render() const;
	void cleanup();
};

class TextureMesh {
private:
	bool elementDraw{false};
	std::vector<TextureVertex> vertices;
	std::vector<unsigned int> indices;
	unsigned int VAO{0}, VBO{0}, EBO{0};
	int vertexCount{0};
	int indexCount{0};

	void setup();

public:
	TextureMesh() = default;
	explicit TextureMesh(std::vector<TextureVertex>&& vertices);
	TextureMesh(std::vector<TextureVertex>&& vertices, std::vector<unsigned int>&& indices);
	~TextureMesh();

	TextureMesh(const TextureMesh&) = delete;
	TextureMesh& operator=(const TextureMesh&) = delete;
	TextureMesh(TextureMesh&& other) noexcept;
	TextureMesh& operator=(TextureMesh&& other) noexcept;

	void render() const;
	void cleanup();
};
