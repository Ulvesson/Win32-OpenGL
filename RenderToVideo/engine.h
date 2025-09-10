#pragma once

#include "cube.h"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

class engine
{
public:
	engine(const glm::mat4x4 &proj);
	void update(double time);
	void render();

private:
	cube cube_mesh;
	glm::mat4x4 proj;
	glm::mat4x4 model;
	double last_time = 0;
	GLuint prog_id;
};

