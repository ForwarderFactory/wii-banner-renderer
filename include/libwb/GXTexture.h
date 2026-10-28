/*
Copyright (c) 2026 - Jacob Nilsson

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
*/

#ifndef WB_GX_TEXTURE_H_
#define WB_GX_TEXTURE_H_

#include <cstdint>

namespace wb
{

enum GxTextureFormat : uint32_t
{
	GX_TEXFMT_I4 = 0,
	GX_TEXFMT_I8 = 1,
	GX_TEXFMT_IA4 = 2,
	GX_TEXFMT_IA8 = 3,
	GX_TEXFMT_RGB565 = 4,
	GX_TEXFMT_RGB5A3 = 5,
	GX_TEXFMT_RGBA8 = 6,
	GX_TEXFMT_C4 = 8,
	GX_TEXFMT_C8 = 9,
	GX_TEXFMT_C14X2 = 10,
	GX_TEXFMT_CMPR = 14,
};

enum GxPaletteFormat : uint32_t
{
	GX_PALFMT_IA8 = 0,
	GX_PALFMT_RGB565 = 1,
	GX_PALFMT_RGB5A3 = 2,
};

enum class DecodedFormat
{
	Invalid,
	Luminance8,
	LuminanceAlpha8,
	Rgba8,
};

uint32_t BytesPerPixel(DecodedFormat format);

struct TexturePalette
{
	const uint8_t* data = nullptr;
	uint32_t entries = 0;
	uint32_t format = GX_PALFMT_IA8;
};

uint32_t GxBlockWidth(uint32_t gx_format);
uint32_t GxBlockHeight(uint32_t gx_format);
uint32_t GxTextureSize(uint32_t width, uint32_t height, uint32_t gx_format);
DecodedFormat GxDecodedFormat(uint32_t gx_format, const TexturePalette* palette);
DecodedFormat DecodeGxTexture(uint8_t* dst, const uint8_t* src, uint32_t width, uint32_t height,
	uint32_t gx_format, const TexturePalette* palette);

}

#endif