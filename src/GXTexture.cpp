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

#include "../include/libwb/GXTexture.h"

#include <cstddef>
#include <cstdint>

namespace wb
{
namespace
{

struct Rgba
{
	uint8_t r, g, b, a;
};

uint16_t ReadBe16(const uint8_t* p)
{
	return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint8_t Widen3(unsigned v) { return static_cast<uint8_t>((v << 5) | (v << 2) | (v >> 1)); }
uint8_t Widen4(unsigned v) { return static_cast<uint8_t>(v * 17); }
uint8_t Widen5(unsigned v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); }
uint8_t Widen6(unsigned v) { return static_cast<uint8_t>((v << 2) | (v >> 4)); }

Rgba FromRgb565(uint16_t v)
{
	return { Widen5((v >> 11) & 0x1F), Widen6((v >> 5) & 0x3F), Widen5(v & 0x1F), 255 };
}

Rgba FromRgb5A3(uint16_t v)
{
	if (v & 0x8000)
		return { Widen5((v >> 10) & 0x1F), Widen5((v >> 5) & 0x1F), Widen5(v & 0x1F), 255 };

	return { Widen4((v >> 8) & 0xF), Widen4((v >> 4) & 0xF), Widen4(v & 0xF), Widen3((v >> 12) & 0x7) };
}

struct Surface
{
	uint8_t* data;
	uint32_t width;

	void PutL(uint32_t x, uint32_t y, uint8_t l) const
	{
		data[static_cast<std::size_t>(y) * width + x] = l;
	}

	void PutLA(uint32_t x, uint32_t y, uint8_t l, uint8_t a) const
	{
		uint8_t* p = data + (static_cast<std::size_t>(y) * width + x) * 2;
		p[0] = l;
		p[1] = a;
	}

	void PutRgba(uint32_t x, uint32_t y, const Rgba& c) const
	{
		uint8_t* p = data + (static_cast<std::size_t>(y) * width + x) * 4;
		p[0] = c.r;
		p[1] = c.g;
		p[2] = c.b;
		p[3] = c.a;
	}
};

template <uint32_t BlockWidth, uint32_t BlockHeight, uint32_t BlockBytes, class Fn>
void ForEachBlock(uint32_t width, uint32_t height, const uint8_t* src, Fn&& fn)
{
	for (uint32_t y = 0; y + BlockHeight <= height; y += BlockHeight)
	{
		for (uint32_t x = 0; x + BlockWidth <= width; x += BlockWidth)
		{
			fn(x, y, src);
			src += BlockBytes;
		}
	}
}

DecodedFormat PaletteOutput(uint32_t palette_format)
{
	return palette_format == GX_PALFMT_IA8 ? DecodedFormat::LuminanceAlpha8 : DecodedFormat::Rgba8;
}

void PutPaletteColor(const Surface& surface, uint32_t x, uint32_t y, const TexturePalette& palette,
	uint32_t index)
{
	const bool valid = index < palette.entries;
	const uint16_t entry = valid ? ReadBe16(palette.data + static_cast<std::size_t>(index) * 2) : 0;

	switch (palette.format)
	{
	case GX_PALFMT_IA8:
		surface.PutLA(x, y, valid ? static_cast<uint8_t>(entry & 0xFF) : 0,
			valid ? static_cast<uint8_t>(entry >> 8) : 0);
		break;

	case GX_PALFMT_RGB565:
		surface.PutRgba(x, y, valid ? FromRgb565(entry) : Rgba{ 0, 0, 0, 0 });
		break;

	default:
		surface.PutRgba(x, y, valid ? FromRgb5A3(entry) : Rgba{ 0, 0, 0, 0 });
		break;
	}
}

void DecodeCmprSubBlock(const Surface& surface, uint32_t x0, uint32_t y0, const uint8_t* block)
{
	const uint16_t raw0 = ReadBe16(block);
	const uint16_t raw1 = ReadBe16(block + 2);

	const Rgba c0 = FromRgb565(raw0);
	const Rgba c1 = FromRgb565(raw1);

	Rgba colors[4];
	colors[0] = c0;
	colors[1] = c1;

	if (raw0 > raw1)
	{
		auto third_eighths = [](int from, int to) {
			const int delta = to - from;
			return (delta >> 1) - (delta >> 3);
		};

		const int dr = third_eighths(c0.r, c1.r);
		const int dg = third_eighths(c0.g, c1.g);
		const int db = third_eighths(c0.b, c1.b);

		colors[2] = { static_cast<uint8_t>(c0.r + dr), static_cast<uint8_t>(c0.g + dg),
			static_cast<uint8_t>(c0.b + db), 255 };
		colors[3] = { static_cast<uint8_t>(c1.r - dr), static_cast<uint8_t>(c1.g - dg),
			static_cast<uint8_t>(c1.b - db), 255 };
	}
	else
	{
		colors[2] = { static_cast<uint8_t>((c0.r + c1.r + 1) / 2), static_cast<uint8_t>((c0.g + c1.g + 1) / 2),
			static_cast<uint8_t>((c0.b + c1.b + 1) / 2), 255 };
		colors[3] = { c1.r, c1.g, c1.b, 0 };
	}

	for (uint32_t row = 0; row < 4; ++row)
	{
		const uint8_t indices = block[4 + row];

		for (uint32_t column = 0; column < 4; ++column)
		{
			const unsigned shift = static_cast<unsigned>(6 - 2 * column);
			surface.PutRgba(x0 + column, y0 + row, colors[(indices >> shift) & 0x3]);
		}
	}
}

}

uint32_t BytesPerPixel(DecodedFormat format)
{
	switch (format)
	{
	case DecodedFormat::Luminance8:
		return 1;
	case DecodedFormat::LuminanceAlpha8:
		return 2;
	case DecodedFormat::Rgba8:
		return 4;
	default:
		return 0;
	}
}

uint32_t GxBlockWidth(uint32_t gx_format)
{
	switch (gx_format)
	{
	case GX_TEXFMT_I4:
	case GX_TEXFMT_C4:
	case GX_TEXFMT_I8:
	case GX_TEXFMT_IA4:
	case GX_TEXFMT_C8:
	case GX_TEXFMT_CMPR:
		return 8;

	case GX_TEXFMT_IA8:
	case GX_TEXFMT_RGB565:
	case GX_TEXFMT_RGB5A3:
	case GX_TEXFMT_RGBA8:
	case GX_TEXFMT_C14X2:
		return 4;

	default:
		return 0;
	}
}

uint32_t GxBlockHeight(uint32_t gx_format)
{
	switch (gx_format)
	{
	case GX_TEXFMT_I4:
	case GX_TEXFMT_C4:
	case GX_TEXFMT_CMPR:
		return 8;

	case GX_TEXFMT_I8:
	case GX_TEXFMT_IA4:
	case GX_TEXFMT_C8:
	case GX_TEXFMT_IA8:
	case GX_TEXFMT_RGB565:
	case GX_TEXFMT_RGB5A3:
	case GX_TEXFMT_RGBA8:
	case GX_TEXFMT_C14X2:
		return 4;

	default:
		return 0;
	}
}

uint32_t GxTextureSize(uint32_t width, uint32_t height, uint32_t gx_format)
{
	const uint32_t block_width = GxBlockWidth(gx_format);
	const uint32_t block_height = GxBlockHeight(gx_format);

	if (!block_width || !block_height)
		return 0;

	const uint32_t block_bytes = gx_format == GX_TEXFMT_RGBA8 ? 64 : 32;

	return (width / block_width) * (height / block_height) * block_bytes;
}

DecodedFormat GxDecodedFormat(uint32_t gx_format, const TexturePalette* palette)
{
	switch (gx_format)
	{
	case GX_TEXFMT_I4:
	case GX_TEXFMT_I8:
		return DecodedFormat::Luminance8;

	case GX_TEXFMT_IA4:
	case GX_TEXFMT_IA8:
		return DecodedFormat::LuminanceAlpha8;

	case GX_TEXFMT_RGB565:
	case GX_TEXFMT_RGB5A3:
	case GX_TEXFMT_RGBA8:
	case GX_TEXFMT_CMPR:
		return DecodedFormat::Rgba8;

	case GX_TEXFMT_C4:
	case GX_TEXFMT_C8:
	case GX_TEXFMT_C14X2:
		return palette && palette->data ? PaletteOutput(palette->format) : DecodedFormat::Invalid;

	default:
		return DecodedFormat::Invalid;
	}
}

DecodedFormat DecodeGxTexture(uint8_t* dst, const uint8_t* src, uint32_t width, uint32_t height,
	uint32_t gx_format, const TexturePalette* palette)
{
	const DecodedFormat result = GxDecodedFormat(gx_format, palette);
	if (result == DecodedFormat::Invalid)
		return result;

	const Surface surface{ dst, width };

	switch (gx_format)
	{
	case GX_TEXFMT_I4:
		ForEachBlock<8, 8, 32>(width, height, src, [&](uint32_t x, uint32_t y, const uint8_t* block) {
			for (uint32_t row = 0; row < 8; ++row)
			{
				for (uint32_t column = 0; column < 8; ++column)
				{
					const uint8_t byte = block[row * 4 + column / 2];
					const unsigned value = (column & 1) ? (byte & 0xF) : (byte >> 4);
					surface.PutL(x + column, y + row, Widen4(value));
				}
			}
		});
		break;

	case GX_TEXFMT_I8:
		ForEachBlock<8, 4, 32>(width, height, src, [&](uint32_t x, uint32_t y, const uint8_t* block) {
			for (uint32_t row = 0; row < 4; ++row)
				for (uint32_t column = 0; column < 8; ++column)
					surface.PutL(x + column, y + row, block[row * 8 + column]);
		});
		break;

	case GX_TEXFMT_IA4:
		ForEachBlock<8, 4, 32>(width, height, src, [&](uint32_t x, uint32_t y, const uint8_t* block) {
			for (uint32_t row = 0; row < 4; ++row)
			{
				for (uint32_t column = 0; column < 8; ++column)
				{
					// alpha in the high half of the byte, intensity in the low half
					const uint8_t byte = block[row * 8 + column];
					surface.PutLA(x + column, y + row, Widen4(byte & 0xF), Widen4(byte >> 4));
				}
			}
		});
		break;

	case GX_TEXFMT_IA8:
		ForEachBlock<4, 4, 32>(width, height, src, [&](uint32_t x, uint32_t y, const uint8_t* block) {
			for (uint32_t row = 0; row < 4; ++row)
			{
				for (uint32_t column = 0; column < 4; ++column)
				{
					// alpha first, then intensity
					const uint8_t* pixel = block + (row * 4 + column) * 2;
					surface.PutLA(x + column, y + row, pixel[1], pixel[0]);
				}
			}
		});
		break;

	case GX_TEXFMT_RGB565:
		ForEachBlock<4, 4, 32>(width, height, src, [&](uint32_t x, uint32_t y, const uint8_t* block) {
			for (uint32_t row = 0; row < 4; ++row)
				for (uint32_t column = 0; column < 4; ++column)
					surface.PutRgba(x + column, y + row, FromRgb565(ReadBe16(block + (row * 4 + column) * 2)));
		});
		break;

	case GX_TEXFMT_RGB5A3:
		ForEachBlock<4, 4, 32>(width, height, src, [&](uint32_t x, uint32_t y, const uint8_t* block) {
			for (uint32_t row = 0; row < 4; ++row)
				for (uint32_t column = 0; column < 4; ++column)
					surface.PutRgba(x + column, y + row, FromRgb5A3(ReadBe16(block + (row * 4 + column) * 2)));
		});
		break;

	case GX_TEXFMT_RGBA8:
		ForEachBlock<4, 4, 64>(width, height, src, [&](uint32_t x, uint32_t y, const uint8_t* block) {
			for (uint32_t row = 0; row < 4; ++row)
			{
				for (uint32_t column = 0; column < 4; ++column)
				{
					const uint32_t index = row * 4 + column;
					const uint8_t* alpha_red = block + index * 2;
					const uint8_t* green_blue = block + 32 + index * 2;
					surface.PutRgba(x + column, y + row, { alpha_red[1], green_blue[0], green_blue[1], alpha_red[0] });
				}
			}
		});
		break;

	case GX_TEXFMT_C4:
		ForEachBlock<8, 8, 32>(width, height, src, [&](uint32_t x, uint32_t y, const uint8_t* block) {
			for (uint32_t row = 0; row < 8; ++row)
			{
				for (uint32_t column = 0; column < 8; ++column)
				{
					const uint8_t byte = block[row * 4 + column / 2];
					const uint32_t index = (column & 1) ? (byte & 0xF) : (byte >> 4);
					PutPaletteColor(surface, x + column, y + row, *palette, index);
				}
			}
		});
		break;

	case GX_TEXFMT_C8:
		ForEachBlock<8, 4, 32>(width, height, src, [&](uint32_t x, uint32_t y, const uint8_t* block) {
			for (uint32_t row = 0; row < 4; ++row)
				for (uint32_t column = 0; column < 8; ++column)
					PutPaletteColor(surface, x + column, y + row, *palette, block[row * 8 + column]);
		});
		break;

	case GX_TEXFMT_C14X2:
		ForEachBlock<4, 4, 32>(width, height, src, [&](uint32_t x, uint32_t y, const uint8_t* block) {
			for (uint32_t row = 0; row < 4; ++row)
				for (uint32_t column = 0; column < 4; ++column)
					PutPaletteColor(surface, x + column, y + row, *palette,
						ReadBe16(block + (row * 4 + column) * 2) & 0x3FFF);
		});
		break;

	case GX_TEXFMT_CMPR:
		ForEachBlock<8, 8, 32>(width, height, src, [&](uint32_t x, uint32_t y, const uint8_t* block) {
			DecodeCmprSubBlock(surface, x, y, block);
			DecodeCmprSubBlock(surface, x + 4, y, block + 8);
			DecodeCmprSubBlock(surface, x, y + 4, block + 16);
			DecodeCmprSubBlock(surface, x + 4, y + 4, block + 24);
		});
		break;

	default:
		return DecodedFormat::Invalid;
	}

	return result;
}

}
