#include "texture.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include <stb_image.h>

namespace fs = std::filesystem;

namespace {
constexpr GLint MINECRAFT_STYLE_MIPMAP_LEVELS = 4;

bool getTextureFormats(int nrChannels, GLint& internalFormat, GLenum& dataFormat) {
	switch (nrChannels) {
	case 1:
		internalFormat = GL_R8;
		dataFormat = GL_RED;
		return true;
	case 3:
		internalFormat = GL_RGB8;
		dataFormat = GL_RGB;
		return true;
	case 4:
		internalFormat = GL_RGBA8;
		dataFormat = GL_RGBA;
		return true;
	default:
		return false;
	}
}

GLint getMinecraftStyleMaxMipmapLevel(int size) {
	GLint level = 0;
	while (size > 1 && level < MINECRAFT_STYLE_MIPMAP_LEVELS) {
		size /= 2;
		level++;
	}

	return level;
}

}

Texture::Texture(const std::string& texturePath) {
	setup(texturePath);
}

Texture::~Texture() {
	cleanup();
	freeLoadedTextures();
}

Texture::Texture(Texture&& other) noexcept
	: tileSize{std::exchange(other.tileSize, 0)},
	  width{std::exchange(other.width, 0)},
	  height{std::exchange(other.height, 0)},
	  nrChannels{std::exchange(other.nrChannels, 0)},
	  textures{std::move(other.textures)},
	  id{std::exchange(other.id, 0)},
	  texture_unit{std::exchange(other.texture_unit, 0)},
	  texture_unit_set{std::exchange(other.texture_unit_set, false)} {
}

Texture& Texture::operator=(Texture&& other) noexcept {
	if (this != &other) {
		cleanup();
		freeLoadedTextures();

		tileSize = std::exchange(other.tileSize, 0);
		width = std::exchange(other.width, 0);
		height = std::exchange(other.height, 0);
		nrChannels = std::exchange(other.nrChannels, 0);
		textures = std::move(other.textures);
		id = std::exchange(other.id, 0);
		texture_unit = std::exchange(other.texture_unit, 0);
		texture_unit_set = std::exchange(other.texture_unit_set, false);
	}

	return *this;
}

bool Texture::setup(const std::string& texturePath) {
	cleanup();
	freeLoadedTextures();

	if (!loadTextures(texturePath)) {
		freeLoadedTextures();
		return false;
	}

	GLint internalFormat = GL_RGBA8;
	GLenum dataFormat = GL_RGBA;
	if (!getTextureFormats(nrChannels, internalFormat, dataFormat)) {
		std::cerr << "Unsupported number of texture channels: " << nrChannels << std::endl;
		freeLoadedTextures();
		return false;
	}

	GLint maxLayers = 0;
	glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
	if (static_cast<GLint>(textures.size()) > maxLayers) {
		std::cerr << "Too many texture array layers: " << textures.size()
				  << ", max supported is " << maxLayers << std::endl;
		freeLoadedTextures();
		return false;
	}

	tileSize = width;
	glGenTextures(1, &id);
	if (!id) {
		std::cerr << "Failed to allocate OpenGL texture array" << std::endl;
		freeLoadedTextures();
		return false;
	}

	glBindTexture(GL_TEXTURE_2D_ARRAY, id);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL,
		getMinecraftStyleMaxMipmapLevel(tileSize));

	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, internalFormat, tileSize, tileSize,
		static_cast<GLsizei>(textures.size()), 0, dataFormat, GL_UNSIGNED_BYTE, nullptr);

	for (const Tex& texture : textures) {
		const GLint layer = static_cast<GLint>(texture.id);
		glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, tileSize, tileSize, 1,
			dataFormat, GL_UNSIGNED_BYTE, texture.data);
	}

	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
	glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

	freeLoadedTextures();
	return true;
}

bool Texture::loadTextures(const std::string& texturePath) {
	const fs::path textureDir(texturePath);
	if (!fs::is_directory(textureDir)) {
		std::cerr << "Texture directory does not exist: " << texturePath << std::endl;
		return false;
	}

	stbi_set_flip_vertically_on_load(true);

	for (std::size_t layer = 0; layer < TEXTURE_COUNT; layer++) {
		const TexMap texId = static_cast<TexMap>(layer);
		const fs::path path = textureDir / textureFileName(texId);

		Tex texture;
		texture.id = texId;
		texture.data = stbi_load(path.string().c_str(),
			&texture.width, &texture.height, &texture.nrChannels, 0);
		if (!texture.data) {
			std::cerr << "Could not load texture image for layer " << layer
					  << ": " << path << std::endl;
			return false;
		}

		if (texture.width != texture.height) {
			std::cerr << "Texture is not square: " << path.filename().string()
					  << " (" << texture.width << "x" << texture.height << ")" << std::endl;
			stbi_image_free(texture.data);
			return false;
		}

		if (textures.empty()) {
			width = texture.width;
			height = texture.height;
			nrChannels = texture.nrChannels;
		} else if (texture.width != width || texture.height != height ||
			texture.nrChannels != nrChannels) {
			std::cerr << "Texture dimensions/channels do not match the first texture: "
					  << path.filename().string() << std::endl;
			stbi_image_free(texture.data);
			return false;
		}

		textures.push_back(texture);
	}

	return !textures.empty();
}

void Texture::freeLoadedTextures() {
	for (Tex& texture : textures) {
		if (texture.data) {
			stbi_image_free(texture.data);
			texture.data = nullptr;
		}
	}
	textures.clear();
}

bool Texture::activateAt(unsigned int unitIndex) {
	if (!id) {
		std::cerr << "Cannot activate texture, texture array is not loaded" << std::endl;
		return false;
	}

	GLint maxUnits = 0;
	glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxUnits);
	if (unitIndex >= static_cast<unsigned int>(maxUnits)) {
		std::cerr << "Texture unit " << unitIndex
				  << " is out of range, max units: " << maxUnits << std::endl;
		return false;
	}

	glActiveTexture(GL_TEXTURE0 + unitIndex);
	glBindTexture(GL_TEXTURE_2D_ARRAY, id);
	texture_unit = unitIndex;
	texture_unit_set = true;
	return true;
}

unsigned int Texture::getId() const {
	return id;
}

unsigned int Texture::getUnit() const {
	if (!texture_unit_set) {
		std::cerr << "Texture unit requested before texture activation" << std::endl;
	}
	return texture_unit;
}

bool Texture::isLoaded() const {
	return id != 0;
}

void Texture::cleanup() {
	if (id) {
		glDeleteTextures(1, &id);
		id = 0;
	}
	texture_unit = 0;
	texture_unit_set = false;
}

void Texture::set_wrap_s(GLint param) {
	if (!id) {
		std::cerr << "Cannot set texture wrap S, texture array is not loaded" << std::endl;
		return;
	}
	glBindTexture(GL_TEXTURE_2D_ARRAY, id);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, param);
}

void Texture::set_wrap_t(GLint param) {
	if (!id) {
		std::cerr << "Cannot set texture wrap T, texture array is not loaded" << std::endl;
		return;
	}
	glBindTexture(GL_TEXTURE_2D_ARRAY, id);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, param);
}

void Texture::set_mag_filter(GLint param) {
	if (!id) {
		std::cerr << "Cannot set texture mag filter, texture array is not loaded" << std::endl;
		return;
	}
	glBindTexture(GL_TEXTURE_2D_ARRAY, id);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, param);
}

void Texture::set_min_filter(GLint param) {
	if (!id) {
		std::cerr << "Cannot set texture min filter, texture array is not loaded" << std::endl;
		return;
	}
	glBindTexture(GL_TEXTURE_2D_ARRAY, id);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, param);
}
