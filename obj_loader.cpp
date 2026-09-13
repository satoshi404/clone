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
	if ( vertices )
	{
		free( vertices );
		vertices = nullptr;
	}
}

ObjMeshData::ObjMeshData( ObjMeshData&& other ) noexcept
{
	vertices     = other.vertices;
	vertexCount  = other.vertexCount;
	stride       = other.stride;

	other.vertices    = nullptr;
	other.vertexCount = 0;
}

ObjMeshData& ObjMeshData::operator=( ObjMeshData&& other ) noexcept
{
	if ( this != &other )
	{
		if ( vertices ) free( vertices );

		vertices     = other.vertices;
		vertexCount  = other.vertexCount;
		stride       = other.stride;

		other.vertices    = nullptr;
		other.vertexCount = 0;
	}
	return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool obj_load( const char* path, ObjMeshData& out )
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	// tinyobjloader NÃO deriva o diretório do .mtl a partir do .obj sozinho —
	// se mtl_basedir não for passado, ele procura relativo ao cwd do processo.
	// Derivamos manualmente pra funcionar independente de onde o .obj estiver.
	std::string pathStr( path );
	size_t lastSlash = pathStr.find_last_of( "/\\" );
	std::string baseDir = ( lastSlash != std::string::npos ) ? pathStr.substr( 0, lastSlash + 1 ) : "";

	bool ok = tinyobj::LoadObj( &attrib, &shapes, &materials, &warn, &err, path, baseDir.c_str() );

	if ( !warn.empty() )
	{
		TerminalDebug::println( PrintColorType_Yellow, "ObjLoader::Warning: %s", warn.c_str() );
	}

	if ( !ok )
	{
		TerminalDebug::println( PrintColorType_Red, "ObjLoader::Error: %s (%s)", err.c_str(), path );
		return false;
	}

	if ( shapes.empty() )
	{
		TerminalDebug::println( PrintColorType_Red, "ObjLoader::Error: no shapes found in %s", path );
		return false;
	}

	// Conta o total de vértices (soma de todos os shapes/faces) pra alocar de uma vez
	size_t totalVertices = 0;
	for ( const tinyobj::shape_t& shape : shapes )
	{
		totalVertices += shape.mesh.indices.size();
	}

	if ( totalVertices == 0 )
	{
		TerminalDebug::println( PrintColorType_Red, "ObjLoader::Error: no vertex data in %s", path );
		return false;
	}

	const uint32_t stride = 6; // pos(3) + color(3) — cor vem do material (Kd), por face
	float* vertices = (float*)malloc( totalVertices * stride * sizeof( float ) );
	if ( !vertices )
	{
		TerminalDebug::println( PrintColorType_Red, "ObjLoader::Error: out of memory (%s)", path );
		return false;
	}

	float minB[3] = {  FLT_MAX,  FLT_MAX,  FLT_MAX };
	float maxB[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

	const float fallbackColor[3] = { 0.8f, 0.8f, 0.8f }; // usado quando a face não tem material válido
	bool loggedMissingMaterial = false;

	size_t writeIndex = 0;
	for ( const tinyobj::shape_t& shape : shapes )
	{
		size_t indexOffset = 0;
		for ( size_t face = 0; face < shape.mesh.num_face_vertices.size(); ++face )
		{
			const int faceVertexCount = shape.mesh.num_face_vertices[ face ];

			// Cor é por-face (bate com o conceito de material do OBJ: um material cobre
			// um grupo de faces, não vértices individuais). Resolve uma vez por face.
			const float* faceColor = fallbackColor;
			if ( face < shape.mesh.material_ids.size() )
			{
				const int matId = shape.mesh.material_ids[ face ];
				if ( matId >= 0 && (size_t)matId < materials.size() )
				{
					faceColor = materials[ matId ].diffuse; // float[3], já vem assim do tinyobjloader
				}
				else if ( !loggedMissingMaterial )
				{
					TerminalDebug::println( PrintColorType_Yellow, "ObjLoader::Warning: '%s' has faces without a valid material, using fallback gray", path );
					loggedMissingMaterial = true;
				}
			}

			for ( int v = 0; v < faceVertexCount; ++v )
			{
				const tinyobj::index_t& index = shape.mesh.indices[ indexOffset + v ];
				const size_t posBase = 3 * (size_t)index.vertex_index;

				float px = attrib.vertices[ posBase + 0 ];
				float py = attrib.vertices[ posBase + 1 ];
				float pz = attrib.vertices[ posBase + 2 ];

				minB[0] = px < minB[0] ? px : minB[0];
				minB[1] = py < minB[1] ? py : minB[1];
				minB[2] = pz < minB[2] ? pz : minB[2];
				maxB[0] = px > maxB[0] ? px : maxB[0];
				maxB[1] = py > maxB[1] ? py : maxB[1];
				maxB[2] = pz > maxB[2] ? pz : maxB[2];

				float* dst = vertices + writeIndex * stride;
				dst[0] = px; dst[1] = py; dst[2] = pz;
				dst[3] = faceColor[0]; dst[4] = faceColor[1]; dst[5] = faceColor[2];

				writeIndex++;
			}

			indexOffset += faceVertexCount;
		}
	}

	if ( materials.empty() )
	{
		TerminalDebug::println( PrintColorType_Yellow, "ObjLoader::Warning: '%s' has no materials (.mtl not found or empty), using fallback gray for all faces", path );
	}

	out.vertices    = vertices;
	out.vertexCount = (uint32_t)writeIndex;
	out.stride      = stride;
	memcpy( out.minBounds, minB, sizeof(minB) );
	memcpy( out.maxBounds, maxB, sizeof(maxB) );

	TerminalDebug::println( PrintColorType_Green, "ObjLoader: loaded '%s' (%u vertices)", path, out.vertexCount );
	TerminalDebug::println( PrintColorType_Cyan, "ObjLoader: bounds min(%.3f, %.3f, %.3f) max(%.3f, %.3f, %.3f)",
		minB[0], minB[1], minB[2], maxB[0], maxB[1], maxB[2] );
	return true;
}