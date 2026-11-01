/*
Copyright (c) 2010 - Wii Banner Player Project

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

#include <fstream>
#include <set>

#include "../include/libwb/Texture.h"
#include "../include/libwb/Endian.h"
#include "../include/libwb/GXDebug.h"

namespace wb
{

enum BinaryMagic : uint32_t
{
	BINARY_MAGIC_TEXTURE = MAKE_FOURCC(0x00, ' ', 0xAF, 0x30)
};

static std::set<uint32_t> g_occupied_tlut_names;

uint32_t GetFreeTlutName()
{
	uint32_t ret = 1;
	while (g_occupied_tlut_names.contains(ret))
		++ret;

	return ret;
}

Texture::~Texture()
{
	delete[] img_ptr;
	delete[] tlut_ptr;

	g_occupied_tlut_names.erase(tlut_name);
}

void Texture::Load(std::istream& file)
{
	const std::streamoff file_start = file.tellg();

	FourCC magic;
	uint32_t texture_count;
	uint32_t header_size;

	file >> magic >> BE >> texture_count >> header_size;

	if (magic != BINARY_MAGIC_TEXTURE)
		return;

	file.seekg(header_size - 0xC, std::ios::cur);

	std::streamoff next_offset = file.tellg();

	while (texture_count--)
	{
		file.seekg(next_offset, std::ios::beg);

		uint32_t texture_offset;
		uint32_t palette_offset;

		file >> BE >> texture_offset >> palette_offset;

		next_offset = file.tellg();

		// palette
		if (palette_offset)
		{
			file.seekg(file_start + palette_offset, std::ios::beg);

			uint16_t palette_unused;
			uint32_t palette_data_offset;

			file >> BE >> tlut_count
				>> palette_unused
				>> tlut_format
				>> palette_data_offset;

			file.seekg(file_start + palette_data_offset, std::ios::beg);

			tlut_ptr = new char[tlut_count * 2];

			file.read(tlut_ptr, tlut_count * 2);
		}

		// texture header
		file.seekg(file_start + texture_offset, std::ios::beg);

		uint32_t format;
		uint32_t texture_data_offset;

		uint16_t height;
		uint16_t width;

		uint32_t wrap_s;
		uint32_t wrap_t;

		uint32_t min_filter;
		uint32_t mag_filter;

		float lod_bias;
		uint8_t edge_lod;
		uint8_t min_lod;
		uint8_t max_lod;
		uint8_t unpacked;

		file >> BE
			>> height
			>> width
			>> format
			>> texture_data_offset
			>> wrap_s
			>> wrap_t
			>> min_filter
			>> mag_filter
			>> lod_bias
			>> edge_lod
			>> min_lod
			>> max_lod
			>> unpacked;

		file.seekg(file_start + texture_data_offset, std::ios::beg);

		uint8_t mipmap = (max_lod > 0) ? 1 : 0;
		uint8_t bias_clamp = (lod_bias > 0.0f) ? 1 : 0;

		const uint32_t tex_size =
			GX_GetTexBufferSize(width, height, format, mipmap, max_lod);

		img_ptr = new char[tex_size];

		file.read(img_ptr, tex_size);

		info.loaded = file.gcount() == static_cast<std::streamsize>(tex_size);
		info.width = width;
		info.height = height;
		info.format = format;
		info.wrap_s = wrap_s;
		info.wrap_t = wrap_t;
		info.min_filter = min_filter;
		info.mag_filter = mag_filter;
		info.lod_bias = lod_bias;
		info.edge_lod = edge_lod;
		info.min_lod = min_lod;
		info.max_lod = max_lod;
		info.data_bytes = tex_size;
		info.has_palette = palette_offset != 0;

		if (palette_offset)
		{
			file.seekg(file_start + palette_offset, std::ios::beg);

			uint16_t palette_unused;
			uint32_t palette_data_offset;

			file >> BE >> tlut_count
				 >> palette_unused
				 >> tlut_format
				 >> palette_data_offset;

			file.seekg(file_start + palette_data_offset, std::ios::beg);

			tlut_ptr = new char[tlut_count * 2];
			file.read(tlut_ptr, tlut_count * 2);

			GX_InitTexObj(
				&texobj,
				img_ptr,
				width,
				height,
				format,
				wrap_s,
				wrap_t,
				mipmap
			);


			GX_InitTexObjTlut(&texobj, 0);
		}
		else
		{
			GX_InitTexObj(
				&texobj,
				img_ptr,
				width,
				height,
				format,
				wrap_s,
				wrap_t,
				mipmap
			);
		}

		if (mipmap)
		{
			GX_InitTexObjLOD(
				&texobj,
				min_filter,
				mag_filter,
				min_lod,
				max_lod,
				lod_bias,
				bias_clamp,
				bias_clamp,
				edge_lod
			);
		}
		else
		{
			GX_InitTexObjFilterMode(
				&texobj,
				min_filter,
				mag_filter
			);
		}
	}
}

void Texture::DebugDescribe(std::ostream& os, int indent) const
{
	using gxdebug::Print;
	namespace gdn = gxdebug::names;

	if (!info.width && !info.height)
	{
		Print(os, "%*s'%s': not loaded (missing from the archive, or not a valid TPL)\n", indent, "", GetName().c_str());
		return;
	}

	Print(os, "%*s'%s': %ux%u %s%s, %u bytes, wrap=%s/%s filter=%s/%s", indent, "", GetName().c_str(),
		static_cast<unsigned>(info.width), static_cast<unsigned>(info.height), gdn::TexFormat(info.format).c_str(),
		info.loaded ? "" : " (truncated)", static_cast<unsigned>(info.data_bytes),
		gdn::Wrap(info.wrap_s).c_str(), gdn::Wrap(info.wrap_t).c_str(),
		gdn::TexFilter(info.min_filter).c_str(), gdn::TexFilter(info.mag_filter).c_str());

	if (info.max_lod)
		Print(os, " mipmaps (lod %u..%u, bias %g, edge_lod %u)", static_cast<unsigned>(info.min_lod),
			static_cast<unsigned>(info.max_lod), static_cast<double>(info.lod_bias), static_cast<unsigned>(info.edge_lod));

	if (info.has_palette)
		Print(os, " palette: %u entries, %s", static_cast<unsigned>(tlut_count), gdn::TlutFormat(tlut_format).c_str());

	Print(os, "\n");
}

void Texture::Apply(uint8_t& tlutName, uint8_t map_id, uint8_t wrap_s, uint8_t wrap_t) const {
	if (map_id >= 8 || tlutName >= 20)
		return;

	GXTexObj tmpTexObj = texobj;

	if (tlut_ptr)
	{
		GXTlutObj tlutobj;

		GX_InitTlutObj(
			&tlutobj,
			tlut_ptr,
			tlut_format,
			tlut_count
		);

		GX_LoadTlut(
			&tlutobj,
			tlutName
		);

		GX_InitTexObjTlut(
			&tmpTexObj,
			tlutName
		);

		tlutName++;
	}

	GX_InitTexObjWrapMode(
		&tmpTexObj,
		wrap_s,
		wrap_t
	);

	GX_LoadTexObj(
		&tmpTexObj,
		map_id
	);
}

}
