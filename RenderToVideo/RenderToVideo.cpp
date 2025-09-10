#include "engine.h"
#include "rendertarget.h"

#include <gl/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
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
}

int main(void)
{
    constexpr int width = 1024;
    constexpr int height = 1024;

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

    engine engine(Projection);
    int idx = 0;

    std::vector<GLubyte> read_buffer;
    constexpr size_t read_buffer_size = width * height * 3;
    read_buffer.reserve(read_buffer_size);

    while (!glfwWindowShouldClose(window))
    {
        const int tail = (idx + 1) % no_buffers;
        engine.update(glfwGetTime());

        renderTargets[idx].Begin();
        engine.render();
        renderTargets[idx].End();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderTargets[tail].RenderTexture(width, height);

        glfwSwapBuffers(window);
        glfwPollEvents();

        void* buf = read_buffer.data();
        auto tex = renderTargets[tail].get_texture();
        glGenerateTextureMipmap(tex);
        glGetTextureImage(tex, 1, GL_RGB, GL_UNSIGNED_BYTE,
            read_buffer_size, buf);

        idx = tail;
    }

    glfwTerminate();
    return 0;
}
