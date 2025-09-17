#include "yuv.h"

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

		const char* frag_src = R"(
			#version 330 core
			uniform sampler2D tex0;
			in vec2 uv;
			layout(location = 0) out vec3 color[3];

            mat4 toYUV = mat4( 0.299, -0.14713,  0.615,   0,
                               0.587, -0.28886, -0.51499, 0,
                               0.144,  0.436,   -0.10001, 0,
                               0.0625, 0.5,      0.5,     1 );

			void main() {
				vec4 yuv = toYUV * vec4(texture(tex0, uv).xyz, 1);
				color[0] = vec3(yuv.x);
				color[1] = vec3(yuv.y);
				color[2] = vec3(yuv.z);
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

yuv::~yuv()
{
	Free();
}

bool yuv::init(GLsizei width, GLsizei height)
{
	if (fbo > 0) {
		return false;
	}

	this->width = width;
	this->height = height;

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	checkError();

	glGenTextures(channels, &tex[0]);

	for (int i = 0; i < channels; i++) {
		glBindTexture(GL_TEXTURE_2D, tex[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height,
			0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

		checkError();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		checkError();
	}

	glBindTexture(GL_TEXTURE_2D, 0);

	glGenRenderbuffers(1, &depth);
	glBindRenderbuffer(GL_RENDERBUFFER, depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, 
		width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 
		GL_RENDERBUFFER, depth);
	checkError();

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[0], 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, tex[1], 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, tex[2], 0);
	checkError();

	GLenum DrawBuffers[]{ GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
	glDrawBuffers(channels, DrawBuffers);
	checkError();

	auto status = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);

	switch (status) {
	case GL_FRAMEBUFFER_COMPLETE:
		std::cout << "Framebuffer status: " << "GL_FRAMEBUFFER_COMPLETE" << std::endl;
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
		std::cerr << "Framebuffer status: " << "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT" << std::endl;
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
		std::cerr << "Framebuffer status: " << "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER" << std::endl;
		break;
	default:
		std::cerr << "Framebuffer status: " << status << std::endl;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return status == GL_FRAMEBUFFER_COMPLETE && InitTextureToScreen();
}

void yuv::Begin()
{
	if (fbo == 0) {
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, width, height);
}

void yuv::End()
{
	if (fbo == 0) {
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void yuv::RenderTexture(int width, int height, int channel)
{
	glViewport(0, 0, width, height);
	
	checkError();
	glUseProgram(program_id);
	checkError();
	GLuint texLoc = glGetUniformLocation(program_id, "tex0");
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, channel);
	glUniform1i(texLoc, 0);

	glBindVertexArray(quad_vert_arr_id);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
}

void yuv::ConvertToYUV(GLuint sourceTexture) const
{
	glViewport(0, 0, width, height);
	glUseProgram(program_id);
	GLuint texLoc = glGetUniformLocation(program_id, "tex0");
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, sourceTexture);
	glUniform1i(texLoc, 0);
	glBindVertexArray(quad_vert_arr_id);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
}

bool yuv::InitTextureToScreen()
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

void yuv::Free()
{
	End();
	glDeleteFramebuffers(1, &fbo);
	for (int i = 0; i < channels; i++) {
		glDeleteTextures(1, &tex[i]);
	}	
	glDeleteRenderbuffers(1, &depth);
	glDeleteBuffers(1, &quad_vert_buffer_id);
	glDeleteVertexArrays(1, &quad_vert_arr_id);
}
