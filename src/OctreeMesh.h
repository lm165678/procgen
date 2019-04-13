#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Octree.h"
#include "Mesh.h"
#include "utils.h"
#include "OctreeVisualization.h"

enum Direction {
	xplus, xminus,
	yplus, yminus,
	zplus, zminus
};

class OctreeMesh : public Mesh
{
public:
	//OctreeMesh();
	OctreeMesh(GLuint program, const int size, const glm::vec3 position);
	~OctreeMesh();
	void Load();

	void Draw(const float time) override;

	void Enlarge(Direction);
	void EnlargeAsync(Direction dir);

	Octree* GetOctree();

private:
	std::tuple<int, int, glm::vec3> EnlargeCorners(Direction dir);

	Octree* m_tree;
	glm::vec3 m_position;
	int m_size;
	OctreeVisualization m_visualization;
	std::vector<OctreeMesh*> m_childMeshes;
};
