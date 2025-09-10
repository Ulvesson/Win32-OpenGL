#include "engine.h"
#include "cube.h"

#include <gl/glew.h>

#include <iostream>

namespace {
	// https://learnopengl.com/Advanced-OpenGL/Framebuffers
	// https://www.opengl-tutorial.org/intermediate-tutorials/tutorial-14-render-to-texture/

	GLuint program() {
		const char* vert_shader_source = R"(
		#version 330 core
		layout(location = 0) in vec3 pos;
		layout(location = 1) in vec3 vertexColor;

		// Values that stay constant for the whole mesh.
		uniform mat4 MVP;

		out vec3 fragmentColor;

		void main()
		{
			// Output position of the vertex, in clip space: MVP * position
			gl_Position =  MVP * vec4(pos, 1);

			// The color of each vertex will be interpolated
			// to produce the color of each fragment
			fragmentColor = vertexColor;
		}
	)";

		const char* frag_shader_source = R"(
		#version 330 core
		in vec3 fragmentColor;
		out vec3 color;
		void main()
		{
			color = fragmentColor;
		}
		)";

		GLuint vert = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vert, 1, &vert_shader_source, nullptr);

		GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(frag, 1, &frag_shader_source, nullptr);

		GLuint prog = glCreateProgram();
		glAttachShader(prog, vert);
		glAttachShader(prog, frag);
		glLinkProgram(prog);

		GLint status = 0;
		glGetProgramiv(prog, GL_LINK_STATUS, &status);

		if (status == 0) {
			std::cerr << "Link error: " << std::endl;
		}

		return prog;
	}

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

	constexpr float PI = glm::pi<float>();
}

engine::engine(const glm::mat4x4& proj)
	: proj(proj)
	, model(glm::mat4(1.0f))
	, prog_id(program())
{
	glClearColor(0, 0, 0, 0);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
}

void engine::update(double time)
{
	float dt = static_cast<float>(time) - static_cast<float>(last_time);
	last_time = time;
	model = glm::rotate(model, PI * dt / 2.0f, glm::vec3(0.5f, 0.75, 0));
}

void engine::render()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glUseProgram(prog_id);

	// Camera matrix
	const glm::mat4 View = glm::lookAt(
		glm::vec3(0, 0, 3.5f), // Camera is at (4,3,3), in World Space
		glm::vec3(0, 0, 0), // and looks at the origin
		glm::vec3(0, 1, 0)  // Head is up (set to 0,-1,0 to look upside-down)
	);	
	glm::mat4 mvp = proj * View * model; // Remember, matrix multiplication is the other way around
	GLuint MatrixID = glGetUniformLocation(prog_id, "MVP");
	glUniformMatrix4fv(MatrixID, 1, GL_FALSE, &mvp[0][0]);

	cube_mesh.draw();
	glUseProgram(0);
}
