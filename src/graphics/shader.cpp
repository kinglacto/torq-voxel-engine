#include "shader.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

#include <glm/gtc/type_ptr.hpp>

namespace {
const char* shaderTypeName(GLenum type) {
	switch (type) {
	case GL_VERTEX_SHADER:
		return "vertex";
	case GL_FRAGMENT_SHADER:
		return "fragment";
	default:
		return "unknown";
	}
}
}

Shader::Shader() = default;

Shader::Shader(const std::string& vertexShaderPath, const std::string& fragmentShaderPath) {
	generate(vertexShaderPath, fragmentShaderPath);
}

Shader::~Shader() {
	cleanup();
}

Shader::Shader(Shader&& other) noexcept
	: id{std::exchange(other.id, 0)},
	  uniformLocationCache{std::move(other.uniformLocationCache)} {
}

Shader& Shader::operator=(Shader&& other) noexcept {
	if (this != &other) {
		cleanup();
		id = std::exchange(other.id, 0);
		uniformLocationCache = std::move(other.uniformLocationCache);
	}

	return *this;
}

void Shader::generate(const std::string& vertexShaderPath, const std::string& fragmentShaderPath) {
	cleanup();

	const GLuint vertexShader = compileShader(vertexShaderPath, GL_VERTEX_SHADER);
	const GLuint fragmentShader = compileShader(fragmentShaderPath, GL_FRAGMENT_SHADER);
	if (!vertexShader || !fragmentShader) {
		if (vertexShader) {
			glDeleteShader(vertexShader);
		}
		if (fragmentShader) {
			glDeleteShader(fragmentShader);
		}
		return;
	}

	id = glCreateProgram();
	if (!id) {
		std::cerr << "Failed to create shader program" << std::endl;
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);
		return;
	}

	glAttachShader(id, vertexShader);
	glAttachShader(id, fragmentShader);
	glLinkProgram(id);

	int success = 0;
	glGetProgramiv(id, GL_LINK_STATUS, &success);
	if (!success) {
		char logInfo[512];
		glGetProgramInfoLog(id, 512, nullptr, logInfo);
		std::cerr << "Linking error with program:" << std::endl << logInfo << std::endl;
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);
		cleanup();
		return;
	}

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
}

GLuint Shader::compileShader(const std::string& filepath, GLenum type) {
	const std::string shaderSrc = loadShaderSrc(filepath);
	if (shaderSrc.empty()) {
		std::cerr << "Failed to load " << shaderTypeName(type)
				  << " shader source: " << filepath << std::endl;
		return 0;
	}

	const GLuint shaderId = glCreateShader(type);
	if (!shaderId) {
		std::cerr << "Failed to create " << shaderTypeName(type) << " shader" << std::endl;
		return 0;
	}

	const GLchar* shaderSrcPtr = shaderSrc.c_str();
	glShaderSource(shaderId, 1, &shaderSrcPtr, nullptr);
	glCompileShader(shaderId);

	int success = 0;
	glGetShaderiv(shaderId, GL_COMPILE_STATUS, &success);
	if (!success) {
		char logInfo[512];
		glGetShaderInfoLog(shaderId, 512, nullptr, logInfo);
		std::cerr << "Error compiling " << shaderTypeName(type) << " shader "
				  << filepath << ":" << std::endl << logInfo << std::endl;
		glDeleteShader(shaderId);
		return 0;
	}

	return shaderId;
}

std::string Shader::loadShaderSrc(const std::string& filepath) {
	std::ifstream file(filepath, std::ios::binary);
	if (!file.is_open()) {
		std::cerr << "Failed to open file " << filepath << std::endl;
		return {};
	}

	std::ostringstream buffer;
	buffer << file.rdbuf();
	if (file.bad()) {
		std::cerr << "Failed to read file " << filepath << std::endl;
		return {};
	}

	return buffer.str();
}

GLint Shader::getUniformLocation(const char* name) const {
	if (!name || name[0] == '\0') {
		std::cerr << "Uniform name is empty or null" << std::endl;
		return -1;
	}
	if (!id) {
		std::cerr << "Cannot set uniform variable " << name
				  << ", shader program is not initialized" << std::endl;
		return -1;
	}

	const auto cached = uniformLocationCache.find(name);
	if (cached != uniformLocationCache.end()) {
		return cached->second;
	}

	const GLint loc = glGetUniformLocation(id, name);
	const auto [it, inserted] = uniformLocationCache.emplace(name, loc);
	if (inserted && loc == -1) {
		std::cerr << "Error setting uniform variable " << name
				  << ", could not locate it" << std::endl;
	}

	return it->second;
}

void Shader::activate() const {
	glUseProgram(id);
}

void Shader::setInt(const char* name, int value) const {
	const GLint loc = getUniformLocation(name);
	if (loc != -1) {
		glUniform1i(loc, value);
	}
}

void Shader::setFloat(const char* name, float value) const {
	const GLint loc = getUniformLocation(name);
	if (loc != -1) {
		glUniform1f(loc, value);
	}
}

void Shader::setMat4(const char* name, const glm::mat4& val) const {
	const GLint loc = getUniformLocation(name);
	if (loc != -1) {
		glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(val));
	}
}

void Shader::set3Float(const char* name, const glm::vec3& v) const {
	const GLint loc = getUniformLocation(name);
	if (loc != -1) {
		glUniform3fv(loc, 1, glm::value_ptr(v));
	}
}

void Shader::set3Float(const char* name, float v1, float v2, float v3) const {
	const GLint loc = getUniformLocation(name);
	if (loc != -1) {
		glUniform3f(loc, v1, v2, v3);
	}
}

unsigned int Shader::getId() const {
	return id;
}

void Shader::cleanup() {
	if (id) {
		glDeleteProgram(id);
		id = 0;
	}
	uniformLocationCache.clear();
}
