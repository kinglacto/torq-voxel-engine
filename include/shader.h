#pragma once

#include <glad/glad.h>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

class Shader {
private:
	GLuint id{0};
	mutable std::unordered_map<std::string, GLint> uniformLocationCache;

	void generate(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	[[nodiscard]] GLint getUniformLocation(const char* name) const;
	static std::string loadShaderSrc(const std::string& filepath);
	static GLuint compileShader(const std::string& filepath, GLenum type);
public:
	Shader();
	Shader(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	~Shader();

	Shader(const Shader&) = delete;
	Shader& operator=(const Shader&) = delete;
	Shader(Shader&& other) noexcept;
	Shader& operator=(Shader&& other) noexcept;

	void activate() const;

	void cleanup();

	void setInt(const char* name, int value) const;
	void setFloat(const char* name, float value) const;
	void setMat4(const char* name, const glm::mat4& val) const;
	void set3Float(const char* name, const glm::vec3& v) const;
	void set3Float(const char* name, float v1, float v2, float v3) const;

	[[nodiscard]] unsigned int getId() const;
};
