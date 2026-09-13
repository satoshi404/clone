#pragma once

#include <cstdint>

// ==========================================
// TEXTURE LOADER (wrapper sobre stb_image)
// ==========================================
//
// Carrega uma imagem (PNG, JPG, etc — o que stb_image suportar) e devolve
// pixels RGBA8 tightly-packed (sem padding entre linhas), prontos pra
// passar direto pra Gpu::texture_create.

struct TextureData
{
	uint8_t *pixels = nullptr; // RGBA8, width*height*4 bytes
	uint32_t width  = 0;
	uint32_t height = 0;

	~TextureData();

	TextureData()                               = default;
	TextureData(const TextureData&)             = delete;
	TextureData& operator=(const TextureData&)  = delete;
	TextureData(TextureData&& other) noexcept;
	TextureData& operator=(TextureData&& other) noexcept;
};

bool texture_load(const char* path, TextureData& out);