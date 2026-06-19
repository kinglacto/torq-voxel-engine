#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <thread>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "chunk_utility.h"
#include "src/IO/camera.h"
#include "src/IO/keyboard.h"
#include "src/IO/mouse.h"
#include "src/IO/screen.h"

// #include "src/graphics/models/sphere.h"
#include "src/graphics/models/cube.h"

#include <chunk_cache.h>
#include <chunk_renderer.h>
#include <chunk_worker.h>
#include <region_store.h>
#include <resource.h>

#include <assets.h>
#include <stb_image_write.h>
#include <worldgen.hpp>

#include <shader.h>
#include <texture.h>

#include <vector>

void processInput();
void init();
bool saveFramebufferScreenshot(GLFWwindow* window, const std::string& path);

float x, y, z;

float deltaTime = 0.0f;	
float lastFrame = 0.0f;
bool printFPS = true;
double fpsPrintStart = 0.0;
int fpsPrintFrames = 0;

Camera camera(glm::vec3(0.0f, 36.0f, 200.0f));
Screen screen(800, 600);
double mouse_dx;
double mouse_dy;
double mouse_scroll;

torq::ChunkCoord cameraChunkCoord() {
	const auto world_x = static_cast<int>(std::floor(camera.cameraPos.x));
	const auto world_y = static_cast<int>(std::floor(camera.cameraPos.y));
	const auto world_z = static_cast<int>(std::floor(camera.cameraPos.z));
	return torq::chunkCoordFromWorldBlock(torq::WorldBlockCoord{
		world_x,
		world_y,
		world_z
	});
}

int main(){
	init();

	if (!screen.init()) {
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}

	if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
		return -1;
	}

	glfwSwapInterval(0);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

	glEnable(GL_MULTISAMPLE);

	screen.setParameters();

	generate_data();

	ResourceManager::LoadShader(BASIC_TEXTURE_VERTEX_SHADER, BASIC_TEXTURE_FRAG_SHADER, 0);
	ResourceManager::LoadTexture(TEXTURE_DIR, 0);

	Shader* shader = ResourceManager::GetShader(0);
	shader->activate();

	Texture* texture = ResourceManager::GetTexture(0);
	if (!texture->activateAt(0)) {
		std::cerr << "Texture activation failed: " << std::endl;
	}
	shader->setInt("texture1", 0);

	auto view = glm::mat4(1.0f);
	auto projection = glm::mat4(1.0f);

	WorldGen::setMasterSeed(123456);
	camera.pitch = -25.0f;
	camera.updateCameraVectors();

	const std::filesystem::path regionDir =
		std::filesystem::path(CACHE_DIR) / "chunks";
	torq::RegionStore regionStore{regionDir};
	torq::ChunkStreamingJobExecutor chunkJobExecutor{regionStore};
	const unsigned int hardwareWorkers = std::thread::hardware_concurrency();
	const std::size_t workerCount =
		std::max(1U, std::min(hardwareWorkers == 0 ? 2U : hardwareWorkers, 4U));
	torq::ChunkWorkerPool chunkWorkers{workerCount, chunkJobExecutor};
	torq::ChunkRenderer chunkRenderer{};
	const torq::ChunkCacheConfig chunkCacheConfig{
		.active_radius = 3,
		.render_distance = 10,
		.keep_margin = 1
	};
	torq::ChunkCache chunkCache{chunkCacheConfig, chunkWorkers};
	torq::ChunkStreamingPipeline streamingPipeline{
		chunkCache,
		chunkRenderer,
		chunkWorkers
	};
	const torq::StreamBudget streamBudget{
		.max_results = 128,
		.max_load_jobs = 16,
		.max_persist_jobs = 4,
		.max_mesh_jobs = 16,
		.max_clean_evictions = 16
	};
	const glm::vec3 fogColor{0.529f, 0.808f, 0.922f};
	const float renderDistanceBlocks =
		static_cast<float>(chunkCacheConfig.render_distance * BLOCK_X_SIZE);
	const float fogStartDistance = renderDistanceBlocks * 0.75f;
	const float fogEndDistance = renderDistanceBlocks * 1.35f;
	shader->set3Float("fogColor", fogColor);
	shader->setFloat("fogStart", fogStartDistance);
	shader->setFloat("fogEnd", fogEndDistance);

	int captureFrame = -1;
	if (const char* captureFrameEnv = std::getenv("TORQ_CAPTURE_FRAME")) {
		captureFrame = std::atoi(captureFrameEnv);
	}
	std::string capturePath = "torq_capture.png";
	if (const char* capturePathEnv = std::getenv("TORQ_CAPTURE_PATH")) {
		capturePath = capturePathEnv;
	}
	int captureMinMeshes = 0;
	if (const char* captureMinMeshesEnv = std::getenv("TORQ_CAPTURE_MIN_MESHES")) {
		captureMinMeshes = std::atoi(captureMinMeshesEnv);
	}
	const bool exitAfterCapture = std::getenv("TORQ_EXIT_AFTER_CAPTURE") != nullptr;
	bool capturedFrame = false;
	int frameIndex = 0;
	double fpsTestSeconds = 0.0;
	if (const char* fpsTestSecondsEnv = std::getenv("TORQ_FPS_TEST_SECONDS")) {
		fpsTestSeconds = std::atof(fpsTestSecondsEnv);
	}
	double fpsTestStart = -1.0;
	int fpsTestFrames = 0;
	double fpsTestMin = std::numeric_limits<double>::max();
	double fpsTestMax = 0.0;

	while (!screen.shouldClose()) {
		auto currentFrame = static_cast<float>(glfwGetTime());
		const float frameDelta = currentFrame - lastFrame;
		deltaTime = std::min(frameDelta, 0.05f);
		lastFrame = currentFrame;
		camera.deltaTime = deltaTime;
		processInput();

		chunkCache.setCenterChunk(cameraChunkCoord());
		streamingPipeline.applyWorkerResults(streamBudget);
		chunkCache.tick(streamBudget, chunkRenderer);

		view = camera.getViewMatrix();
		projection = glm::perspective(glm::radians(camera.getZoom()),
			screen.getAspectRatio(), camera.znear, camera.zfar);

		glClearColor(fogColor.r, fogColor.g, fogColor.b, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		shader->activate();

		shader->setMat4("view", view);
		shader->setMat4("projection", projection);
		shader->set3Float("cameraPos", camera.cameraPos);
		const torq::ChunkFrustum frustum =
			torq::makeChunkFrustum(projection * view);
		chunkRenderer.draw(shader, frustum);

		const int renderedMeshCount =
			static_cast<int>(chunkRenderer.storage().pool.live_count);
		if (!capturedFrame && captureFrame >= 0 && frameIndex >= captureFrame &&
			renderedMeshCount >= captureMinMeshes) {
			if (!saveFramebufferScreenshot(screen.window, capturePath)) {
				std::cerr << "Failed to save framebuffer screenshot: " << capturePath << std::endl;
			}
			capturedFrame = true;
			if (exitAfterCapture) {
				screen.setShouldClose(true);
			}
		}
		screen.newFrame();
		if (printFPS) {
			if (fpsPrintStart == 0.0) {
				fpsPrintStart = glfwGetTime();
			}

			fpsPrintFrames++;
			const double now = glfwGetTime();
			const double elapsed = now - fpsPrintStart;
			if (elapsed >= 1.0) {
				std::cout << "FPS: "
						  << static_cast<double>(fpsPrintFrames) / elapsed
						  << '\n';
				fpsPrintStart = now;
				fpsPrintFrames = 0;
			}
		}
		if (fpsTestSeconds > 0.0) {
			const double now = glfwGetTime();
			if (fpsTestStart < 0.0) {
				fpsTestStart = now;
			}

			fpsTestFrames++;
			if (frameDelta > 0.0f) {
				const double instantFps = 1.0 / static_cast<double>(frameDelta);
				fpsTestMin = std::min(fpsTestMin, instantFps);
				fpsTestMax = std::max(fpsTestMax, instantFps);
			}

			const double elapsed = now - fpsTestStart;
			if (elapsed >= fpsTestSeconds) {
				const double averageFps = static_cast<double>(fpsTestFrames) / elapsed;
				std::cout << "FPS_TEST seconds=" << elapsed
						  << " frames=" << fpsTestFrames
						  << " avg_fps=" << averageFps
						  << " min_instant_fps=" << fpsTestMin
						  << " max_instant_fps=" << fpsTestMax
						  << std::endl;
				screen.setShouldClose(true);
			}
		}
		frameIndex++;
	}

	chunkWorkers.shutdown();
	chunkRenderer.shutdown();
	ResourceManager::deleteAll();
	glfwTerminate();
	delete[] blockTexMap;
	return 0;
}

void processInput() {
	if (keyboard::key(GLFW_KEY_ESCAPE)) {
		screen.setShouldClose(true);
	}

	if (keyboard::key(GLFW_KEY_W))
		camera.updateCameraPos(cameraDirection::FORWARD, deltaTime);
	if (keyboard::key(GLFW_KEY_S))
		camera.updateCameraPos(cameraDirection::BACKWARD, deltaTime);
	if (keyboard::key(GLFW_KEY_D))
		camera.updateCameraPos(cameraDirection::RIGHT, deltaTime);
	if (keyboard::key(GLFW_KEY_A))
		camera.updateCameraPos(cameraDirection::LEFT, deltaTime);
	if (keyboard::key(GLFW_KEY_SPACE))
		camera.updateCameraPos(cameraDirection::UP, deltaTime);
	if (keyboard::key(GLFW_KEY_LEFT_SHIFT))
		camera.updateCameraPos(cameraDirection::DOWN, deltaTime);

	mouse_dx = mouse::getDX();
	mouse_dy = mouse::getDY();
	mouse_scroll = mouse::getScrollDY();

	if ((mouse_dx != 0 || mouse_dy != 0) && mouse::button(GLFW_MOUSE_BUTTON_LEFT)) {
		camera.updateCameraDirection(mouse_dx, mouse_dy);
	}

	if (mouse_scroll != 0) {
		camera.updateCameraZoom(mouse_scroll);
	}
}

void init() {
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_SAMPLES, 4);
}

bool saveFramebufferScreenshot(GLFWwindow* window, const std::string& path) {
	int width = 0;
	int height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	if (width <= 0 || height <= 0) {
		return false;
	}

	std::vector<unsigned char> pixels(width * height * 3);
	std::vector<unsigned char> flipped(width * height * 3);

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadBuffer(GL_BACK);
	glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

	for (int y = 0; y < height; y++) {
		const int srcY = height - 1 - y;
		std::copy_n(
			pixels.data() + srcY * width * 3,
			width * 3,
			flipped.data() + y * width * 3
		);
	}

	return stbi_write_png(path.c_str(), width, height, 3, flipped.data(), width * 3) != 0;
}
