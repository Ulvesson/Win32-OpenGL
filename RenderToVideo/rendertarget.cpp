#include "rendertarget.h"
#define FRAMEBUFFER_IMPLEMENTATION
#include "framebuffer.h"
#include <iostream>

namespace {
	const GLfloat quad_vertex_buffer_data[] = {
		-1.0f, -1.0f, 0.0f,
		1.0f, -1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f,
		1.0f, -1.0f, 0.0f,
		1.0f,  1.0f, 0.0f,
	};

	void check_compile(GLuint shader) {
		GLint status;

		glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

		if (status == GL_FALSE) {
			constexpr int LOG_SIZE = 256;
			GLchar log[LOG_SIZE];
			GLsizei length = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
			glGetShaderInfoLog(shader, LOG_SIZE - 1, &length, log);
			if (length > 0) {
				std::cout << log << std::endl;
			}

			exit(1);
		}
	}

	GLuint compile_shaders() {
		const char* vert_src = R"(
			#version 330 core
			layout(location = 0) in vec3 pos;

			out vec2 uv;

			void main() {
				uv = (vec2(pos.xy) + 1) / 2;
				gl_Position = vec4(pos, 1);
			}
		)";

		// 			#layout(location = 0) out vec4 frag_rgba;
		//			#layout(location = 1) out vec3 frag_norm;
		const char* frag_src = R"(
			#version 330 core
			uniform sampler2D tex0;
			in vec2 uv;
			layout(location = 0) out vec3 color;

			void main() {
				color = texture(tex0, uv).xyz;
			}
		)";
		
		GLuint vert = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vert, 1, &vert_src, nullptr);
		glCompileShader(vert);
		check_compile(vert);

		GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(frag, 1, &frag_src, nullptr);
		glCompileShader(frag);
		check_compile(frag);

		GLuint prog = glCreateProgram();
		glAttachShader(prog, vert);
		glAttachShader(prog, frag);
		glLinkProgram(prog);

		GLint status = 0;
		glGetProgramiv(prog, GL_LINK_STATUS, &status);

		if (status == GL_FALSE) {
			GLint length = 0;
			glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &length);

			std::string errorLog(length, ' ');  // Resize and fill with space character
			glGetProgramInfoLog(prog, length, &length, &errorLog[0]);
			std::cerr << "SHADER COMPILE TEST ERROR " << std::endl;
			exit(1);
		}

		return prog;
	}

	void checkError() {
		auto err = glGetError();
		if (err != 0) {
			std::cerr << "GL error: " << err << std::endl;
		}
	}
}

RenderTarget::~RenderTarget()
{
	Free();
	delete framebuffer;
}

bool RenderTarget::init(GLsizei width, GLsizei height)
{
	if (framebuffer != nullptr) {
		return false;
	}

	framebuffer = new FrameBuffer();
	framebuffer->Init(width, height, true);

	this->width = width;
	this->height = height;

	auto status = framebuffer->checkNamedFramebufferStatus();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return status == GL_FRAMEBUFFER_COMPLETE && InitTextureToScreen();
}

void RenderTarget::Begin()
{
	if (!framebuffer->IsValid()) {
		return;
	}

	framebuffer->Bind();
	glViewport(0, 0, width, height);
}

void RenderTarget::End()
{
	if (!framebuffer->IsValid()) {
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::RenderTexture(int width, int height, GLuint texture)
{
	glViewport(0, 0, width, height);
	
	checkError();
	glUseProgram(program_id);
	checkError();
	GLuint texLoc = glGetUniformLocation(program_id, "tex0");
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture == 0 ? framebuffer->GetTexture() : texture);
	glUniform1i(texLoc, 0);

	glBindVertexArray(quad_vert_arr_id);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
}

GLuint RenderTarget::get_texture() const
{
	if (framebuffer != nullptr && framebuffer->IsValid()) {
		return framebuffer->GetTexture();
	}
	return 0;
}

bool RenderTarget::InitTextureToScreen()
{
	// The fullscreen quad's FBO
	glGenVertexArrays(1, &quad_vert_arr_id);
	glBindVertexArray(quad_vert_arr_id);
	checkError();

	glGenBuffers(1, &quad_vert_buffer_id);
	glBindBuffer(GL_ARRAY_BUFFER, quad_vert_buffer_id);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertex_buffer_data), quad_vertex_buffer_data, GL_STATIC_DRAW);
	checkError();

	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, quad_vert_buffer_id);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
	checkError();

	glBindVertexArray(0);

	program_id = compile_shaders();
	return true;
}

void RenderTarget::Free()
{
	End();
	glDeleteBuffers(1, &quad_vert_buffer_id);
	glDeleteVertexArrays(1, &quad_vert_arr_id);
}
