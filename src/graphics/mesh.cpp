#include <cstddef>
#include <mesh.h>
#include <utility>

PrimitiveMesh::PrimitiveMesh(std::vector<PrimitiveVertex>&& vertices)
	: vertices{std::move(vertices)} {
	setup();
}

PrimitiveMesh::PrimitiveMesh(std::vector<PrimitiveVertex>&& vertices,
	std::vector<unsigned int>&& indices)
	: elementDraw{true}, vertices{std::move(vertices)}, indices{std::move(indices)} {
	setup();
}

PrimitiveMesh::~PrimitiveMesh() {
	cleanup();
}

PrimitiveMesh::PrimitiveMesh(PrimitiveMesh&& other) noexcept
	: elementDraw{std::exchange(other.elementDraw, false)},
	  vertices{std::move(other.vertices)},
	  indices{std::move(other.indices)},
	  VAO{std::exchange(other.VAO, 0)},
	  VBO{std::exchange(other.VBO, 0)},
	  EBO{std::exchange(other.EBO, 0)},
	  vertexCount{std::exchange(other.vertexCount, 0)},
	  indexCount{std::exchange(other.indexCount, 0)} {
}

PrimitiveMesh& PrimitiveMesh::operator=(PrimitiveMesh&& other) noexcept {
	if (this != &other) {
		cleanup();
		elementDraw = std::exchange(other.elementDraw, false);
		vertices = std::move(other.vertices);
		indices = std::move(other.indices);
		VAO = std::exchange(other.VAO, 0);
		VBO = std::exchange(other.VBO, 0);
		EBO = std::exchange(other.EBO, 0);
		vertexCount = std::exchange(other.vertexCount, 0);
		indexCount = std::exchange(other.indexCount, 0);
	}

	return *this;
}

void PrimitiveMesh::render() const {
	if (!VAO || vertexCount <= 0) {
		return;
	}

	glBindVertexArray(VAO);
	if (elementDraw) {
		if (indexCount > 0) {
			glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
		}
	} else {
		glDrawArrays(GL_TRIANGLES, 0, vertexCount);
	}
	glBindVertexArray(0);
}

void PrimitiveMesh::cleanup() {
	if (VAO) {
		glDeleteVertexArrays(1, &VAO);
		VAO = 0;
	}

	if (VBO) {
		glDeleteBuffers(1, &VBO);
		VBO = 0;
	}

	if (EBO) {
		glDeleteBuffers(1, &EBO);
		EBO = 0;
	}

	vertexCount = 0;
	indexCount = 0;
	elementDraw = false;
}

void PrimitiveMesh::setup() {
	vertexCount = static_cast<int>(vertices.size());
	indexCount = static_cast<int>(indices.size());
	if (vertexCount <= 0) {
		return;
	}

	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);

	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER,
		static_cast<GLsizeiptr>(vertices.size() * sizeof(PrimitiveVertex)),
		vertices.data(), GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex), nullptr);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex),
		reinterpret_cast<void*>(offsetof(PrimitiveVertex, color)));
	glEnableVertexAttribArray(1);

	if (elementDraw && indexCount > 0) {
		glGenBuffers(1, &EBO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
			indices.data(), GL_STATIC_DRAW);
	}

	glBindVertexArray(0);
	std::vector<PrimitiveVertex>().swap(vertices);
	std::vector<unsigned int>().swap(indices);
}

TextureMesh::TextureMesh(std::vector<TextureVertex>&& vertices)
	: vertices{std::move(vertices)} {
	setup();
}

TextureMesh::TextureMesh(std::vector<TextureVertex>&& vertices,
	std::vector<unsigned int>&& indices)
	: elementDraw{true}, vertices{std::move(vertices)}, indices{std::move(indices)} {
	setup();
}

TextureMesh::~TextureMesh() {
	cleanup();
}

TextureMesh::TextureMesh(TextureMesh&& other) noexcept
	: elementDraw{std::exchange(other.elementDraw, false)},
	  vertices{std::move(other.vertices)},
	  indices{std::move(other.indices)},
	  VAO{std::exchange(other.VAO, 0)},
	  VBO{std::exchange(other.VBO, 0)},
	  EBO{std::exchange(other.EBO, 0)},
	  vertexCount{std::exchange(other.vertexCount, 0)},
	  indexCount{std::exchange(other.indexCount, 0)} {
}

TextureMesh& TextureMesh::operator=(TextureMesh&& other) noexcept {
	if (this != &other) {
		cleanup();
		elementDraw = std::exchange(other.elementDraw, false);
		vertices = std::move(other.vertices);
		indices = std::move(other.indices);
		VAO = std::exchange(other.VAO, 0);
		VBO = std::exchange(other.VBO, 0);
		EBO = std::exchange(other.EBO, 0);
		vertexCount = std::exchange(other.vertexCount, 0);
		indexCount = std::exchange(other.indexCount, 0);
	}

	return *this;
}

void TextureMesh::render() const {
	if (!VAO || vertexCount <= 0) {
		return;
	}

	glBindVertexArray(VAO);
	if (elementDraw) {
		if (indexCount > 0) {
			glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
		}
	} else {
		glDrawArrays(GL_TRIANGLES, 0, vertexCount);
	}
	glBindVertexArray(0);
}

void TextureMesh::cleanup() {
	if (VAO) {
		glDeleteVertexArrays(1, &VAO);
		VAO = 0;
	}

	if (VBO) {
		glDeleteBuffers(1, &VBO);
		VBO = 0;
	}

	if (EBO) {
		glDeleteBuffers(1, &EBO);
		EBO = 0;
	}

	vertexCount = 0;
	indexCount = 0;
	elementDraw = false;
}

void TextureMesh::setup() {
	vertexCount = static_cast<int>(vertices.size());
	indexCount = static_cast<int>(indices.size());
	if (vertexCount <= 0) {
		return;
	}

	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);

	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER,
		static_cast<GLsizeiptr>(vertices.size() * sizeof(TextureVertex)),
		vertices.data(), GL_STATIC_DRAW);

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

	if (elementDraw && indexCount > 0) {
		glGenBuffers(1, &EBO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
			indices.data(), GL_STATIC_DRAW);
	}

	glBindVertexArray(0);
	std::vector<TextureVertex>().swap(vertices);
	std::vector<unsigned int>().swap(indices);
}
