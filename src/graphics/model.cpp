#include "model.h"

PrimitiveModel::PrimitiveModel() = default;

PrimitiveModel::~PrimitiveModel() = default;

void PrimitiveModel::renderAll() const {
	for (const PrimitiveMesh& mesh: meshes) {
		mesh.render();
	}
}

void PrimitiveModel::cleanup() {
	for (auto & mesh : meshes) {
		mesh.cleanup();
	}
	meshes.clear();
}



TextureModel::TextureModel() = default;

TextureModel::~TextureModel() = default;

void TextureModel::renderAll() const {
	for (const TextureMesh& mesh: meshes) {
		mesh.render();
	}
}

void TextureModel::cleanup() {
	for (auto & mesh : meshes) {
		mesh.cleanup();
	}
	meshes.clear();
}
