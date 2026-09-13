#pragma once

#include <core/types.hpp>

#include <cstdint>

struct ObjMaterial
{
    char name[64];

    float diffuse[3];
    float specular[3];
    float ambient[3];

    float shininess;
    float opacity;

    char diffuseTexture[256];
};

struct ObjMeshData
{
    float* vertices = nullptr;

    usize vertexCount = 0;
    usize stride = 0;

    float minBounds[3] = {};
    float maxBounds[3] = {};

    ObjMaterial* materials = nullptr;
    usize materialCount = 0;


	ObjMeshData(){};
	~ObjMeshData();
	ObjMeshData(
    	ObjMeshData&& other
	) noexcept;


ObjMeshData& operator=(
    ObjMeshData&& other
) noexcept;
};

bool obj_load(const char* path, ObjMeshData& out);