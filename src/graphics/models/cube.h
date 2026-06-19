#pragma once

#include <model.h>
#include <vertex.h>
#include <texture.h>
#include <shader.h>
#include <glm/glm.hpp>

class Cube: public TextureModel {
protected:
	glm::vec3 size;

	Shader* shader{nullptr};
	Texture* texture{nullptr};

public:
	glm::vec3 position;
	glm::mat4 model{1.0f};
	Cube(const glm::vec3& pos, const glm::vec3& size, Shader* shader, Texture* texture);
	Cube(const glm::vec3& pos, const glm::vec3& size);

	Cube(const glm::vec3& pos, float size, Shader* shader, Texture* texture);
	Cube(const glm::vec3& pos, float size);

	~Cube() override;
		
	void init();
	void render();
		
	bool setSize(float size);
	bool setSize(const glm::vec3& size);
	void setPosition(const glm::vec3& pos);
	void setShader(Shader* shader);
	void setTexture(Texture* texture);
};
