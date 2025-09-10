#pragma once
#include <GL/glew.h>

// https://www.opengl-tutorial.org/intermediate-tutorials/tutorial-14-render-to-texture/#using-the-rendered-texture

class RenderTarget
{
public:
	virtual ~RenderTarget();

	[[nodiscard]] bool init(GLsizei width, GLsizei height);
	void Begin();
	void End();

	void RenderTexture(int width, int height);
	[[nodiscard]] GLuint get_texture() const { return tex; }

private:
	bool InitTextureToScreen();
	void Free();

private:
	GLsizei width = 0;
	GLsizei height = 0;
	GLuint fbo = 0;
	GLuint tex = 0;
	GLuint depth = 0;
	GLuint quad_vert_arr_id = 0;
	GLuint quad_vert_buffer_id = 0;
	GLuint program_id = 0;
};
