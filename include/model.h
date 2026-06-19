#pragma once

#include <vector>
#include "mesh.h"

class PrimitiveModel {
protected:
	std::vector<PrimitiveMesh> meshes;
	PrimitiveModel();
	PrimitiveModel(PrimitiveModel&&) noexcept = default;
	PrimitiveModel& operator=(PrimitiveModel&&) noexcept = default;
	void renderAll() const;
public:
	virtual ~PrimitiveModel();
	PrimitiveModel(const PrimitiveModel&) = delete;
	PrimitiveModel& operator=(const PrimitiveModel&) = delete;
	void cleanup();
};

class TextureModel {
protected:
	std::vector<TextureMesh> meshes;
	TextureModel();
	TextureModel(TextureModel&&) noexcept = default;
	TextureModel& operator=(TextureModel&&) noexcept = default;
	void renderAll() const;
public:
	virtual ~TextureModel();
	TextureModel(const TextureModel&) = delete;
	TextureModel& operator=(const TextureModel&) = delete;
	void cleanup();
};
