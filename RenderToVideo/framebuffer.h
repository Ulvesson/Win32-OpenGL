#pragma once

#include <GL/glew.h>

class FrameBuffer {
public:
	FrameBuffer() = default;
	~FrameBuffer();
	bool Init(int width, int height, bool depth_buffer = false);
	void Bind();
	void Unbind();
	GLuint GetTexture() const { return tex; }
	GLenum checkNamedFramebufferStatus();
	bool IsValid() const { return fbo != 0; }
private:
	GLuint fbo = 0;
	GLuint tex = 0;
	GLuint depth = 0;
	int width = 0;
	int height = 0;
};

#ifdef FRAMEBUFFER_IMPLEMENTATION
#include <iostream>

FrameBuffer::~FrameBuffer()
{
	if (fbo != 0) {
		glDeleteFramebuffers(1, &fbo);
	}
	if (tex != 0) {
		glDeleteTextures(1, &tex);
	}
	if (depth != 0) {
		glDeleteRenderbuffers(1, &depth);
	}
}
bool FrameBuffer::Init(int width, int height, bool depth_buffer)
{
	this->width = width;
	this->height = height;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0);

	if (depth_buffer) {
		glGenRenderbuffers(1, &depth);
		glBindRenderbuffer(GL_RENDERBUFFER, depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
			width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			GL_RENDERBUFFER, depth);
	}

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return true;
}

void FrameBuffer::Bind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, width, height);
}

void FrameBuffer::Unbind()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

inline GLenum FrameBuffer::checkNamedFramebufferStatus()
{
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

	return status;
}

#endif