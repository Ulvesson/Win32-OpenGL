#pragma once
#include <GL/glew.h>

// https://www.opengl-tutorial.org/intermediate-tutorials/tutorial-14-render-to-texture/#using-the-rendered-texture

class yuv
{
public:
	virtual ~yuv();

	[[nodiscard]] bool init(GLsizei width, GLsizei height);
	void Begin();
	void End();

	void RenderTexture(int width, int height, int channel);
	void ConvertToYUV(GLuint sourceTexture) const;
	[[nodiscard]] GLuint get_texture(int channel) const { return tex[channel]; }

private:
	bool InitTextureToScreen();
	void Free();

	enum constants {
		channels = 3
	};

private:
	GLsizei width = 0;
	GLsizei height = 0;
	GLuint fbo = 0;
	GLuint tex[channels];
	GLuint depth = 0;
	GLuint quad_vert_arr_id = 0;
	GLuint quad_vert_buffer_id = 0;
	GLuint program_id = 0;
};