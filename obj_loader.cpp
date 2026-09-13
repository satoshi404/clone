#include "obj_loader.hpp"

#include <core/debug.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <cstdlib>
#include <cstring>
#include <cfloat>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ObjMeshData::~ObjMeshData()
{
    if (vertices)
    {
        free(vertices);
        vertices = nullptr;
    }

    if (materials)
    {
        free(materials);
        materials = nullptr;
    }
}

ObjMeshData::ObjMeshData(
    ObjMeshData&& other
) noexcept
{
    vertices = other.vertices;
    vertexCount = other.vertexCount;
    stride = other.stride;

    materials = other.materials;
    materialCount = other.materialCount;

    memcpy(minBounds, other.minBounds, sizeof(minBounds));
    memcpy(maxBounds, other.maxBounds, sizeof(maxBounds));

    other.vertices = nullptr;
    other.vertexCount = 0;

    other.materials = nullptr;
    other.materialCount = 0;
}

ObjMeshData& ObjMeshData::operator=(
    ObjMeshData&& other
) noexcept
{
    if (this != &other)
    {
        free(vertices);
        free(materials);

        vertices = other.vertices;
        vertexCount = other.vertexCount;
        stride = other.stride;

        materials = other.materials;
        materialCount = other.materialCount;

        memcpy(minBounds, other.minBounds, sizeof(minBounds));
        memcpy(maxBounds, other.maxBounds, sizeof(maxBounds));

        other.vertices = nullptr;
        other.vertexCount = 0;

        other.materials = nullptr;
        other.materialCount = 0;
    }

    return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool obj_load(const char *path, ObjMeshData &out)
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	// tinyobjloader NÃO deriva o diretório do .mtl a partir do .obj sozinho —
	// se mtl_basedir não for passado, ele procura relativo ao cwd do processo.
	// Derivamos manualmente pra funcionar independente de onde o .obj estiver.
	std::string pathStr(path);
	size_t lastSlash = pathStr.find_last_of("/\\");
	std::string baseDir = (lastSlash != std::string::npos) ? pathStr.substr(0, lastSlash + 1) : "";

	bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path, baseDir.c_str());

	if (!warn.empty())
	{
		TerminalDebug::println(PrintColorType_Yellow, "ObjLoader::Warning: %s", warn.c_str());
	}

	if (!ok)
	{
		TerminalDebug::println(PrintColorType_Red, "ObjLoader::Error: %s (%s)", err.c_str(), path);
		return false;
	}

	if (shapes.empty())
	{
		TerminalDebug::println(PrintColorType_Red, "ObjLoader::Error: no shapes found in %s", path);
		return false;
	}

	// Conta o total de vértices (soma de todos os shapes/faces) pra alocar de uma vez
	size_t totalVertices = 0;
	for (const tinyobj::shape_t &shape : shapes)
	{
		totalVertices += shape.mesh.indices.size();
	}

	if (totalVertices == 0)
	{
		TerminalDebug::println(PrintColorType_Red, "ObjLoader::Error: no vertex data in %s", path);
		return false;
	}

	const uint32_t stride = 8; // pos(3) + color(3) — cor vem do material (Kd), por face
	float *vertices = (float *)malloc(totalVertices * stride * sizeof(float));
	if (!vertices)
	{
		TerminalDebug::println(PrintColorType_Red, "ObjLoader::Error: out of memory (%s)", path);
		return false;
	}

	float minB[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
	float maxB[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

	const float fallbackColor[3] = {0.8f, 0.8f, 0.8f}; // usado quando a face não tem material válido
	bool loggedMissingMaterial = false;

	size_t writeIndex = 0;
	for (const tinyobj::shape_t &shape : shapes)
	{
		size_t indexOffset = 0;
		for (size_t face = 0; face < shape.mesh.num_face_vertices.size(); ++face)
		{
			const int faceVertexCount = shape.mesh.num_face_vertices[face];

			// Cor é por-face (bate com o conceito de material do OBJ: um material cobre
			// um grupo de faces, não vértices individuais). Resolve uma vez por face.
			const float *faceColor = fallbackColor;
			if (face < shape.mesh.material_ids.size())
			{
				const int matId = shape.mesh.material_ids[face];
				if (matId >= 0 && (size_t)matId < materials.size())
				{
					faceColor = materials[matId].diffuse; // float[3], já vem assim do tinyobjloader
				}
				else if (!loggedMissingMaterial)
				{
					TerminalDebug::println(PrintColorType_Yellow, "ObjLoader::Warning: '%s' has faces without a valid material, using fallback gray", path);
					loggedMissingMaterial = true;
				}
			}

			for (int v = 0; v < faceVertexCount; ++v)
			{
				const tinyobj::index_t &index = shape.mesh.indices[indexOffset + v];
				const size_t posBase = 3 * (size_t)index.vertex_index;

				float px = attrib.vertices[posBase + 0];
				float py = attrib.vertices[posBase + 1];
				float pz = attrib.vertices[posBase + 2];

				minB[0] = px < minB[0] ? px : minB[0];
				minB[1] = py < minB[1] ? py : minB[1];
				minB[2] = pz < minB[2] ? pz : minB[2];
				maxB[0] = px > maxB[0] ? px : maxB[0];
				maxB[1] = py > maxB[1] ? py : maxB[1];
				maxB[2] = pz > maxB[2] ? pz : maxB[2];

				float nx = 0.0f;
				float ny = 0.0f;
				float nz = 0.0f;

				if (index.normal_index >= 0)
				{
					const size_t normalBase = 3 * (size_t)index.normal_index;

					nx = attrib.normals[normalBase + 0];
					ny = attrib.normals[normalBase + 1];
					nz = attrib.normals[normalBase + 2];
				}

				float u = 0.0f;
				float vcoord = 0.0f;

				if (index.texcoord_index >= 0)
				{
					const size_t uvBase =
						2 * (size_t)index.texcoord_index;

					u = attrib.texcoords[uvBase + 0];
					vcoord = attrib.texcoords[uvBase + 1];
				}

				float *dst = vertices + writeIndex * stride;

				dst[0] = px;
				dst[1] = py;
				dst[2] = pz;

				dst[3] = nx;
				dst[4] = ny;
				dst[5] = nz;

				dst[6] = u;
				dst[7] = vcoord;

				writeIndex++;
			}

			indexOffset += faceVertexCount;
		}
	}

	if (!materials.empty())
	{
		out.materialCount = materials.size();

		out.materials = (ObjMaterial *)calloc(
			out.materialCount,
			sizeof(ObjMaterial));

		if (!out.materials)
		{
			free(vertices);
			return false;
		}

		for (usize i = 0; i < out.materialCount; ++i)
		{
			const tinyobj::material_t &src = materials[i];
			ObjMaterial &dst = out.materials[i];

			strncpy(
				dst.name,
				src.name.c_str(),
				sizeof(dst.name) - 1);

			dst.diffuse[0] = src.diffuse[0];
			dst.diffuse[1] = src.diffuse[1];
			dst.diffuse[2] = src.diffuse[2];

			dst.specular[0] = src.specular[0];
			dst.specular[1] = src.specular[1];
			dst.specular[2] = src.specular[2];

			dst.ambient[0] = src.ambient[0];
			dst.ambient[1] = src.ambient[1];
			dst.ambient[2] = src.ambient[2];

			dst.shininess = src.shininess;
			dst.opacity = src.dissolve;

			strncpy(
				dst.diffuseTexture,
				src.diffuse_texname.c_str(),
				sizeof(dst.diffuseTexture) - 1);
		}
	}

	out.vertices = vertices;
	out.vertexCount = (uint32_t)writeIndex;
	out.stride = stride;
	memcpy(out.minBounds, minB, sizeof(minB));
	memcpy(out.maxBounds, maxB, sizeof(maxB));

	TerminalDebug::println(PrintColorType_Green, "ObjLoader: loaded '%s' (%u vertices)", path, out.vertexCount);
	TerminalDebug::println(PrintColorType_Cyan, "ObjLoader: bounds min(%.3f, %.3f, %.3f) max(%.3f, %.3f, %.3f)",
						   minB[0], minB[1], minB[2], maxB[0], maxB[1], maxB[2]);

	// Log de cada material: se 'diffuseTexture' vier preenchido, o Kd sólido abaixo é só
	// um placeholder — a cor real do material está numa textura que ainda não é
	// sampleada por este backend (ver TODO de textura em command_list_execute).
	for (usize i = 0; i < out.materialCount; ++i)
	{
		const ObjMaterial &mat = out.materials[i];
		TerminalDebug::println(PrintColorType_Cyan,
			"ObjLoader: material[%zu] '%s' Kd(%.3f, %.3f, %.3f) texture='%s'",
			i, mat.name, mat.diffuse[0], mat.diffuse[1], mat.diffuse[2],
			mat.diffuseTexture[0] ? mat.diffuseTexture : "(none)");
	}

	return true;
}