#pragma once

#include <glad/glad.h>

#include <string>
#include <vector>

#include "texture_utility.h"

struct Tex {
	TexMap id{};
	int width{0};
	int height{0};
	int nrChannels{0};
	unsigned char* data{nullptr};
};

class Texture {
private:
	int tileSize{0};
	int width{0};
	int height{0};
	int nrChannels{0};

	std::vector<Tex> textures;
	GLuint id{0};
	unsigned int texture_unit{0};
	bool texture_unit_set{false};

	bool setup(const std::string& texturePath);
	bool loadTextures(const std::string& texturePath);
	void freeLoadedTextures();

public:
	Texture() = default;
	explicit Texture(const std::string& texturePath);
	~Texture();

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;
	Texture(Texture&& other) noexcept;
	Texture& operator=(Texture&& other) noexcept;

	bool activateAt(unsigned int unit);
	[[nodiscard]] unsigned int getId() const;
	[[nodiscard]] unsigned int getUnit() const;
	[[nodiscard]] bool isLoaded() const;
	void cleanup();

	void set_wrap_s(GLint param);
	void set_wrap_t(GLint param);
	void set_mag_filter(GLint param);
	void set_min_filter(GLint param);
};
