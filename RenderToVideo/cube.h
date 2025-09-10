#pragma once

#include <GL/glew.h>

class cube {
public:
	cube();
	void draw();
private:
	GLuint vao = 0;
	GLuint vert_buffer = 0;
	GLuint color_buffer = 0;
};