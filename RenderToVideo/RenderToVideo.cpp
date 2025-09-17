#include "engine.h"
#include "rendertarget.h"
#include "yuv.h"

#include <gl/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>


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

    FILE* open_video(const std::string& filename, int width, int height) {
        std::stringstream ss;
        ss << "ffmpeg.exe -loglevel error "
            << "-f rawvideo -pixel_format yuv420p -video_size "
            << width << "*" << height << " -framerate 30 -i - "
            << "-c:v h264_nvenc " << filename;

        auto cmd = ss.str();
        std::cout << "CMD: " << cmd << std::endl;
        return _popen(cmd.c_str(), "wb");
    }
}

int main(void)
{
    constexpr int width = 800;
    constexpr int height = 600;

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

#ifdef _DEBUG
    // During init, enable debug output
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(MessageCallback, 0);
#endif

    // Projection matrix: 45° Field of View, 4:3 ratio, display range: 0.1 unit <-> 100 units
    const glm::mat4 Projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);

    constexpr int no_buffers = 2;
    RenderTarget renderTargets[no_buffers];

    for (int i = 0; i < no_buffers; i++) {
        if (!renderTargets[i].init(width, height)) {
            return EXIT_FAILURE;
        }
    }

    yuv rgb_to_yuv;
    if (!rgb_to_yuv.init(width, height)) {
        return EXIT_FAILURE;
    }

    engine engine(Projection);
    int idx = 0;

    std::vector<GLubyte> Y;
    std::vector<GLubyte> U;
    std::vector<GLubyte> V;
    constexpr size_t Y_size = width * height;
    constexpr size_t U_size = width * height / 4;
    constexpr size_t V_size = width * height / 4;
    Y.reserve(Y_size);
    U.reserve(U_size);
    V.reserve(V_size);

    auto filename = "test.mkv";
    if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
    }
    auto video = open_video("test.mkv", width, height);
    int frame_no = 0;
    auto started_at = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(window))
    {
        const int tail = (idx + 1) % no_buffers;
        engine.update(glfwGetTime());

        renderTargets[idx].Begin();
        engine.render();
        renderTargets[idx].End();

        // TODO: Gamma-Correction : Already in linear RGB, should not be needed!?
        // https://nicolbolas.github.io/oldtut/Texturing/Tutorial%2016.html
        // https://learnopengl.com/Advanced-Lighting/Gamma-Correction

        // TODO: Convert RGB to YUV 4:2:0 in a fragment shader
        // https://stackoverflow.com/questions/7901519/how-to-use-opengl-fragment-shader-to-convert-rgb-to-yuv420
        rgb_to_yuv.Begin();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        rgb_to_yuv.ConvertToYUV(renderTargets[tail].get_texture());
        rgb_to_yuv.End();

        // Render result to screen
        //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        //renderTargets[0].RenderTexture(width, height, renderTargets[tail].get_texture());
        //glfwSwapBuffers(window);

        glfwPollEvents();

        auto tex = rgb_to_yuv.get_texture(0);
        glGetTextureImage(tex, 0, GL_RED, GL_UNSIGNED_BYTE, Y_size, Y.data());
        tex = rgb_to_yuv.get_texture(1);
        glGenerateTextureMipmap(tex);
        glGetTextureImage(tex, 1, GL_RED, GL_UNSIGNED_BYTE, U_size, U.data());
        tex = rgb_to_yuv.get_texture(2);
        glGenerateTextureMipmap(tex);
        glGetTextureImage(tex, 1, GL_RED, GL_UNSIGNED_BYTE, V_size, V.data());

        _fwrite_nolock(Y.data(), 1, Y_size, video);
        _fwrite_nolock(U.data(), 1, U_size, video);
        _fwrite_nolock(V.data(), 1, V_size, video);

        idx = tail;
        frame_no++;
    }

    std::chrono::duration<double> elapsed_seconds = std::chrono::high_resolution_clock::now() - started_at;
    std::cout << "FPS: " << frame_no / elapsed_seconds.count() << std::endl;

    _pclose(video);
    glfwTerminate();
    return 0;
}
