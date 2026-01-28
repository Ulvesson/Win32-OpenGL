#include "engine.h"
#include "encoder.h"
#include "rendertarget.h"

#include <gl/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cuda.h>

#include <chrono>
#include <filesystem>
#include <iostream>

#pragma comment(lib, "cudart.lib")
#pragma comment(lib, "cuda.lib")

namespace {
#ifdef _DEBUG
	void GLAPIENTRY
		MessageCallback(GLenum source,
			GLenum type,
			GLuint id,
			GLenum severity,
			GLsizei length,
			const GLchar* message,
			const void* userParam)
	{
		fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
			(type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
			type, severity, message);
	}
#endif

	CUcontext createCudaContext() {
		CUdevice cuDevice;
		CUcontext cuContext;

		// Initialize CUDA
		if (cuInit(0) != CUDA_SUCCESS) {
			throw std::runtime_error("Failed to initialize CUDA");
		}

		// Get the first CUDA device
		if (cuDeviceGet(&cuDevice, 0) != CUDA_SUCCESS) {
			throw std::runtime_error("Failed to get CUDA device");
		}

		// Create a CUDA context
		if (cuCtxCreate(&cuContext, nullptr, CU_CTX_SCHED_AUTO, cuDevice) != CUDA_SUCCESS) {
			throw std::runtime_error("Failed to create CUDA context");
		}

		std::cout << "CUDA context created successfully!" << std::endl;
		return cuContext;
	}

    FILE* open_video(const std::string& filename, int width, int height) {
        std::stringstream ss;
        ss << "C:/Users/tommy/source/repos/3pp/ffmpeg-8.0-essentials_build/bin/ffmpeg.exe -loglevel error "
            << " -framerate 30 -i - "
            << " " << filename;

        auto cmd = ss.str();
        std::cout << "CMD: " << cmd << std::endl;
        return _popen(cmd.c_str(), "wb");
    }
}

int main(void)
{
    constexpr int width = 800;
    constexpr int height = 800;

    GLFWwindow* window;

    /* Initialize the library */
    if (!glfwInit())
        return -1;

    /* Create a windowed mode window and its OpenGL context */
    window = glfwCreateWindow(width, height, "Hello World", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        return -1;
    }

    /* Make the window's context current */
    glfwMakeContextCurrent(window);

    if (int err = glewInit() != GLEW_OK) {
        std::cout << "GLEW init failed: " << glewGetErrorString(err) << std::endl;
        return EXIT_FAILURE;
    }

    auto cudaCtx = createCudaContext();

#ifdef _DEBUG
    // During init, enable debug output
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(MessageCallback, 0);
#endif

    // Projection matrix: 45 deg Field of View, 4:3 ratio, display range: 0.1 unit <-> 100 units
    const glm::mat4 Projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);

    constexpr int no_buffers = 2;
    RenderTarget renderTargets[no_buffers];
	GLsync fences[no_buffers] = { nullptr, nullptr };

    for (int i = 0; i < no_buffers; i++) {
        if (!renderTargets[i].init(width, height)) {
            return EXIT_FAILURE;
        }
    }

    auto filename = "output.mkv";
    if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
    }

    constexpr int fps = 30;
	constexpr int max_frames = 60 * 1 * 30;
	auto stream = open_video(filename, width, height);
	Encoder encoder(stream);
    encoder.initializeEncoder();
	encoder.createSession(cudaCtx);
	encoder.createEncoder(width, height, 4000000, fps);
	for (int i = 0; i < no_buffers; i++) {
        encoder.registerCudaResource(renderTargets[i].get_texture(), width, height);
    }
	
    engine engine(Projection);
    int idx = 0;
	int tail = 1 - no_buffers;

    int frame_no = 0;
    auto started_at = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(window))
    {
        engine.update(glfwGetTime());
        renderTargets[idx].Begin();
        engine.render();
        renderTargets[idx].End();

		fences[idx] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		
        if (tail >= 0 && tail < no_buffers && fences[tail]) {
            // Wait for the fence of the buffer to be encoded next
            while (true) {
                GLenum waitReturn = glClientWaitSync(fences[tail], GL_SYNC_FLUSH_COMMANDS_BIT, 6000);
                if (waitReturn == GL_ALREADY_SIGNALED || waitReturn == GL_CONDITION_SATISFIED) {
                    break;
                }
            }
            glDeleteSync(fences[tail]);
            fences[tail] = nullptr;

            if (encoder.mapInput(tail, width, height)) {
                if (!encoder.processTextureWithNvenc()) {
                    std::cerr << "Failed to encode frame " << frame_no << std::endl;
                }
                encoder.unmapInput(tail);
            }
            else {
                std::cerr << "Failed to map input texture for encoding" << std::endl;
            }
		}

		idx = (idx + 1) % no_buffers;
		tail = tail < 0 ? tail + 1 : (tail + 1) % no_buffers;
        frame_no++;
        glfwPollEvents();

        if (frame_no >= max_frames) {
            break;
		}
    }

	// Flush remaining frames
    while (tail != idx) {
        if (tail >= 0 && tail < no_buffers && fences[tail]) {
            // Wait for the fence of the buffer to be encoded next
            while (true) {
                GLenum waitReturn = glClientWaitSync(fences[tail], GL_SYNC_FLUSH_COMMANDS_BIT, 6000);
                if (waitReturn == GL_ALREADY_SIGNALED || waitReturn == GL_CONDITION_SATISFIED) {
                    break;
                }
            }
            glDeleteSync(fences[tail]);
            fences[tail] = nullptr;
            if (encoder.mapInput(tail, width, height)) {
                if (!encoder.processTextureWithNvenc()) {
                    std::cerr << "Failed to encode frame " << frame_no << std::endl;
                }
                encoder.unmapInput(tail);
            }
            else {
                std::cerr << "Failed to map input texture for encoding" << std::endl;
            }
        }
        tail = (tail + 1) % no_buffers;
	}

    std::chrono::duration<double> elapsed_seconds = std::chrono::high_resolution_clock::now() - started_at;
    std::cout << "FPS: " << frame_no / elapsed_seconds.count() << std::endl;
	std::cout << "Total frames: " << frame_no << std::endl;
	std::cout << "Duration seconds: " << frame_no / fps << std::endl;

    if (cuCtxDestroy(cudaCtx) != CUDA_SUCCESS) {
        std::cerr << "Failed to destroy CUDA context" << std::endl;
        return EXIT_FAILURE;
    }

    _pclose(stream);
    glfwTerminate();
      return 0;
}
