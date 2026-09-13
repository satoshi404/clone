#pragma once

#include <cmath>

// ==========================================
// MAT4 — implementação mínima, convenção vetor-linha (row-vector)
// ==========================================
//
// Convenção adotada: v' = v * M  (vetor como linha, multiplicado pela direita).
// Ao compor transformações, a ordem de aplicação é da ESQUERDA pra DIREITA:
//   mvp = model * view * proj;   // primeiro model, depois view, depois proj
//   clipPos = worldPos * mvp;
//
// No lado do HLSL isso exige declarar a matriz como `row_major` no cbuffer
// (ver shader), pra bater com o layout de memória que gravamos aqui.
//
// Se seu projeto já tem uma lib de math própria, ignore este arquivo e
// adapte a chamada de mat4_perspective/mat4_look_at pros equivalentes dela
// — só preste atenção na convenção row-vector vs column-vector, que é a
// causa mais comum de "a câmera renderiza tudo invertido/errado".

struct Mat4
{
	float m[4][4]; // m[row][col]
};

inline Mat4 mat4_identity()
{
	Mat4 r = {};
	r.m[0][0] = 1.0f; r.m[1][1] = 1.0f; r.m[2][2] = 1.0f; r.m[3][3] = 1.0f;
	return r;
}

inline Mat4 mat4_multiply( const Mat4& a, const Mat4& b )
{
	Mat4 r = {};
	for ( int row = 0; row < 4; ++row )
	{
		for ( int col = 0; col < 4; ++col )
		{
			r.m[row][col] = a.m[row][0] * b.m[0][col]
			              + a.m[row][1] * b.m[1][col]
			              + a.m[row][2] * b.m[2][col]
			              + a.m[row][3] * b.m[3][col];
		}
	}
	return r;
}

// Câmera olhando de 'eye' para 'target', com 'up' como referência de cima.
// Sistema left-handed (padrão D3D): forward = target - eye, mas orientado
// pra +Z apontar pra frente da câmera.
inline Mat4 mat4_look_at( float eyeX, float eyeY, float eyeZ,
                           float targetX, float targetY, float targetZ,
                           float upX, float upY, float upZ )
{
	float fx = targetX - eyeX, fy = targetY - eyeY, fz = targetZ - eyeZ;
	float flen = std::sqrt( fx*fx + fy*fy + fz*fz );
	fx /= flen; fy /= flen; fz /= flen;

	// right = up x forward (left-handed)
	float rx = upY*fz - upZ*fy;
	float ry = upZ*fx - upX*fz;
	float rz = upX*fy - upY*fx;
	float rlen = std::sqrt( rx*rx + ry*ry + rz*rz );
	rx /= rlen; ry /= rlen; rz /= rlen;

	// up real (ortogonal) = forward x right
	float ux = fy*rz - fz*ry;
	float uy = fz*rx - fx*rz;
	float uz = fx*ry - fy*rx;

	Mat4 r = mat4_identity();
	r.m[0][0] = rx; r.m[1][0] = ry; r.m[2][0] = rz;
	r.m[0][1] = ux; r.m[1][1] = uy; r.m[2][1] = uz;
	r.m[0][2] = fx; r.m[1][2] = fy; r.m[2][2] = fz;

	r.m[3][0] = -(rx*eyeX + ry*eyeY + rz*eyeZ);
	r.m[3][1] = -(ux*eyeX + uy*eyeY + uz*eyeZ);
	r.m[3][2] = -(fx*eyeX + fy*eyeY + fz*eyeZ);
	return r;
}

// Projeção perspectiva left-handed, profundidade D3D-style (NDC z em [0,1]).
inline Mat4 mat4_perspective_fov( float fovYRadians, float aspect, float nearZ, float farZ )
{
	float yScale = 1.0f / std::tan( fovYRadians * 0.5f );
	float xScale = yScale / aspect;

	Mat4 r = {};
	r.m[0][0] = xScale;
	r.m[1][1] = yScale;
	r.m[2][2] = farZ / (farZ - nearZ);
	r.m[2][3] = 1.0f;
	r.m[3][2] = -nearZ * farZ / (farZ - nearZ);
	return r;
}