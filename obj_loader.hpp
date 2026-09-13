#pragma once

#include <cstdint>

// ==========================================
// OBJ LOADER (wrapper sobre tinyobjloader)
// ==========================================
//
// Carrega um arquivo .obj (+ .mtl associado) e produz um buffer de vértices
// já intercalado no layout: position (float3) + color (float3) -> stride = 6.
//
// A cor vem do material (Kd / diffuse) do .mtl, resolvida por FACE (um
// material cobre um grupo de faces, não vértices individuais — cada face
// usa a cor do seu material, replicada nos vértices que a compõem). Faces
// sem material válido, ou arquivos sem .mtl, caem no cinza (0.8, 0.8, 0.8).
//
// Não faz merge de faces com atributos diferentes de forma otimizada
// (cada combinação (pos, normal, uv) vira um vértice novo, sem indexação),
// ou seja, o resultado já vem pronto pra desenhar como lista de triângulos
// (glDrawArrays-style), sem precisar de index buffer.
//
// Se seu Backend exigir index buffer, dá pra adaptar depois; comecei sem
// pra bater com o desenho não-indexado que já existe no main.cpp (mesh de
// 6 vértices sem índice).

struct ObjMeshData
{
	float*   vertices    = nullptr; // interleaved: px,py,pz, nx,ny,nz, ...
	uint32_t vertexCount = 0;       // número de vértices (não de floats)
	uint32_t stride      = 6;       // floats por vértice (3 pos + 3 normal)

	// Bounding box em espaço de objeto (antes de qualquer transform). Útil pra
	// diagnosticar clipping e pra auto-enquadrar a câmera em torno do modelo,
	// já que escala/origem de um .obj arbitrário são desconhecidas de antemão.
	float minBounds[3] = { 0.0f, 0.0f, 0.0f };
	float maxBounds[3] = { 0.0f, 0.0f, 0.0f };

	~ObjMeshData();

	// Impede cópia acidental (o buffer é heap-alocado e liberado no destrutor)
	ObjMeshData()                               = default;
	ObjMeshData(const ObjMeshData&)             = delete;
	ObjMeshData& operator=(const ObjMeshData&)  = delete;
	ObjMeshData(ObjMeshData&& other) noexcept;
	ObjMeshData& operator=(ObjMeshData&& other) noexcept;
};

// Retorna true em sucesso e preenche 'out'. Em falha, loga o erro via
// TerminalDebug e retorna false (out fica em estado vazio/zero).
bool obj_load(const char* path, ObjMeshData& out);