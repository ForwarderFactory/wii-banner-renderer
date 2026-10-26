/*
Copyright (c) 2010 - Wii Banner Player Project
Copyright (c) 2012 - giantpune
Copyright (c) 2012 - Dimok

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

#include "../include/libwb/Font.h"

#include <algorithm>
#include <array>
#include <limits>

namespace wb
{
namespace
{

enum BinaryMagic : uint32_t
{
	BINARY_MAGIC_FONT = MAKE_FOURCC('R', 'F', 'N', 'T'),
	BINARY_MAGIC_FONT_ARCHIVE = MAKE_FOURCC('R', 'F', 'N', 'A'),
	BINARY_MAGIC_GLYPH_GROUP = MAKE_FOURCC('G', 'L', 'G', 'R'),
	BINARY_MAGIC_FONT_INFORMATION = MAKE_FOURCC('F', 'I', 'N', 'F'),
	BINARY_MAGIC_TEXTURE_GLYPH = MAKE_FOURCC('T', 'G', 'L', 'P'),
	BINARY_MAGIC_CHARACTER_CODE_MAP = MAKE_FOURCC('C', 'M', 'A', 'P'),
	BINARY_MAGIC_CHARACTER_WIDTH = MAKE_FOURCC('C', 'W', 'D', 'H')
};

bool CanRead(const std::vector<uint8_t>& data, size_t offset, size_t length)
{
	return offset <= data.size() && length <= data.size() - offset;
}

uint16_t ReadBE16(const std::vector<uint8_t>& data, size_t offset)
{
	return static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8) |
		data[offset + 1]);
}

uint32_t ReadBE32(const std::vector<uint8_t>& data, size_t offset)
{
	return (static_cast<uint32_t>(data[offset]) << 24) |
		(static_cast<uint32_t>(data[offset + 1]) << 16) |
		(static_cast<uint32_t>(data[offset + 2]) << 8) |
		data[offset + 3];
}

bool DecompressHuffman8(
	const uint8_t* input,
	size_t input_size,
	size_t output_size,
	std::vector<uint8_t>& output)
{
	if (input_size < 8 || input[0] != 0x28)
		return false;

	const size_t encoded_size = static_cast<size_t>(input[1]) |
		(static_cast<size_t>(input[2]) << 8) |
		(static_cast<size_t>(input[3]) << 16);
	if (encoded_size && encoded_size != output_size)
		return false;

	const size_t bitstream_offset = 6 + static_cast<size_t>(input[4]) * 2;
	if (bitstream_offset >= input_size)
		return false;

	output.clear();
	output.reserve(output_size);

	size_t node_offset = 5;
	for (size_t word_offset = bitstream_offset;
		word_offset <= input_size && 4 <= input_size - word_offset &&
		output.size() < output_size;
		word_offset += 4)
	{
		for (int byte_in_word = 3;
			byte_in_word >= 0 && output.size() < output_size;
			--byte_in_word)
		{
			const uint8_t byte = input[word_offset + byte_in_word];
			for (uint8_t bit_mask = 0x80;
				bit_mask && output.size() < output_size;
				bit_mask >>= 1)
			{
				if (node_offset >= bitstream_offset)
					return false;

				const uint8_t node = input[node_offset];
				const uint8_t direction = (byte & bit_mask) ? 1 : 0;
				const size_t child_offset = node_offset + direction +
					((static_cast<size_t>(node) << 1) & 0x7e) +
					(2 - (node_offset & 1));

				if (child_offset >= bitstream_offset)
					return false;

				if ((static_cast<uint16_t>(node) << direction) & 0x80)
				{
					output.push_back(input[child_offset]);
					node_offset = 5;
				}
				else
				{
					node_offset = child_offset;
				}
			}
		}
	}

	return output.size() == output_size;
}

}

bool Font::Load(std::istream& file)
{
	loaded = false;
	code_maps.clear();
	width_blocks.clear();
	sheet_data.clear();
	texture_objects.clear();

	std::array<uint8_t, 16> header{};
	file.read(reinterpret_cast<char*>(header.data()), header.size());
	if (file.gcount() != static_cast<std::streamsize>(header.size()))
		return false;

	const uint32_t magic = (static_cast<uint32_t>(header[0]) << 24) |
		(static_cast<uint32_t>(header[1]) << 16) |
		(static_cast<uint32_t>(header[2]) << 8) |
		header[3];
	const uint16_t endian = static_cast<uint16_t>((header[4] << 8) | header[5]);
	const uint16_t version = static_cast<uint16_t>((header[6] << 8) | header[7]);
	const uint32_t file_size = (static_cast<uint32_t>(header[8]) << 24) |
		(static_cast<uint32_t>(header[9]) << 16) |
		(static_cast<uint32_t>(header[10]) << 8) |
		header[11];
	const uint16_t first_section = static_cast<uint16_t>((header[12] << 8) | header[13]);
	const uint16_t section_count = static_cast<uint16_t>((header[14] << 8) | header[15]);

	if ((magic != BINARY_MAGIC_FONT && magic != BINARY_MAGIC_FONT_ARCHIVE) ||
		endian != 0xfeff || version != 0x0104 ||
		file_size < header.size() || first_section < header.size() ||
		first_section >= file_size)
	{
		return false;
	}

	std::vector<uint8_t> data(file_size);
	std::copy(header.begin(), header.end(), data.begin());
	file.read(
		reinterpret_cast<char*>(data.data() + header.size()),
		static_cast<std::streamsize>(data.size() - header.size()));
	if (file.gcount() != static_cast<std::streamsize>(data.size() - header.size()))
		return false;

	archived = magic == BINARY_MAGIC_FONT_ARCHIVE;
	uint32_t sheet_image_offset = 0;
	uint32_t glyph_group_sheet_size = 0;
	bool found_font_information = false;
	bool found_texture_glyph = false;

	size_t section_offset = first_section;
	for (uint16_t section_index = 0; section_index < section_count; ++section_index)
	{
		if (!CanRead(data, section_offset, 8))
			return false;

		const uint32_t section_magic = ReadBE32(data, section_offset);
		const uint32_t section_size = ReadBE32(data, section_offset + 4);
		if (section_size < 8 || !CanRead(data, section_offset, section_size))
			return false;

		const size_t payload = section_offset + 8;
		const size_t payload_size = section_size - 8;

		switch (section_magic)
		{
		case BINARY_MAGIC_GLYPH_GROUP:
			if (payload_size >= 4)
				glyph_group_sheet_size = ReadBE32(data, payload);
			break;

		case BINARY_MAGIC_FONT_INFORMATION:
			if (payload_size < 24)
				return false;
			line_feed = static_cast<int8_t>(data[payload + 1]);
			alternate_char_index = ReadBE16(data, payload + 2);
			default_width.left = static_cast<int8_t>(data[payload + 4]);
			default_width.glyph_width = data[payload + 5];
			default_width.char_width = static_cast<int8_t>(data[payload + 6]);
			height = data[payload + 20];
			width = data[payload + 21];
			found_font_information = true;
			break;

		case BINARY_MAGIC_TEXTURE_GLYPH:
			if (payload_size < 24)
				return false;
			cell_width = data[payload];
			cell_height = data[payload + 1];
			sheet_size = ReadBE32(data, payload + 4);
			sheet_count = ReadBE16(data, payload + 8);
			sheet_format = ReadBE16(data, payload + 10) & 0x7fff;
			sheet_row = ReadBE16(data, payload + 12);
			sheet_line = ReadBE16(data, payload + 14);
			sheet_width = ReadBE16(data, payload + 16);
			sheet_height = ReadBE16(data, payload + 18);
			sheet_image_offset = ReadBE32(data, payload + 20);
			found_texture_glyph = true;
			break;

		case BINARY_MAGIC_CHARACTER_CODE_MAP:
		{
			if (payload_size < 12)
				return false;

			CodeMap code_map;
			code_map.ccode_begin = ReadBE16(data, payload);
			code_map.ccode_end = ReadBE16(data, payload + 2);
			code_map.mapping_method = ReadBE16(data, payload + 4);

			const size_t map_offset = payload + 12;
			if (code_map.ccode_end < code_map.ccode_begin)
				return false;

			size_t value_count = 0;
			switch (code_map.mapping_method)
			{
			case 0:
				value_count = 1;
				break;
			case 1:
				value_count = static_cast<size_t>(code_map.ccode_end) -
					code_map.ccode_begin + 1;
				break;
			case 2:
				if (!CanRead(data, map_offset, 2))
					return false;
				value_count = 1 + static_cast<size_t>(ReadBE16(data, map_offset)) * 2;
				break;
			default:
				return false;
			}

			if (!CanRead(data, map_offset, value_count * sizeof(uint16_t)) ||
				map_offset + value_count * sizeof(uint16_t) > section_offset + section_size)
			{
				return false;
			}

			code_map.map_info.reserve(value_count);
			for (size_t i = 0; i < value_count; ++i)
				code_map.map_info.push_back(ReadBE16(data, map_offset + i * 2));

			code_maps.push_back(std::move(code_map));
			break;
		}

		case BINARY_MAGIC_CHARACTER_WIDTH:
		{
			if (payload_size < 8)
				return false;

			WidthBlock width_block;
			width_block.index_begin = ReadBE16(data, payload);
			width_block.index_end = ReadBE16(data, payload + 2);
			if (width_block.index_end < width_block.index_begin)
				return false;

			const size_t width_count = static_cast<size_t>(width_block.index_end) -
				width_block.index_begin + 1;
			const size_t widths_offset = payload + 8;
			if (!CanRead(data, widths_offset, width_count * 3) ||
				widths_offset + width_count * 3 > section_offset + section_size)
			{
				return false;
			}

			width_block.widths.reserve(width_count);
			for (size_t i = 0; i < width_count; ++i)
			{
				const size_t offset = widths_offset + i * 3;
				width_block.widths.push_back({
					static_cast<int8_t>(data[offset]),
					data[offset + 1],
					static_cast<int8_t>(data[offset + 2])
				});
			}

			width_blocks.push_back(std::move(width_block));
			break;
		}

		default:
			break;
		}

		section_offset += section_size;
	}

	if (!found_font_information || !found_texture_glyph || !width || !height ||
		!sheet_size || !sheet_count || !sheet_row || !sheet_line ||
		!sheet_width || !sheet_height || sheet_image_offset >= data.size())
	{
		return false;
	}

	if (archived && glyph_group_sheet_size && glyph_group_sheet_size != sheet_size)
		return false;

	sheet_data.reserve(sheet_count);
	size_t sheet_offset = sheet_image_offset;
	for (uint16_t sheet_index = 0; sheet_index < sheet_count; ++sheet_index)
	{
		std::vector<uint8_t> sheet;
		if (archived)
		{
			if (!CanRead(data, sheet_offset, 4))
				return false;

			const uint32_t compressed_size = ReadBE32(data, sheet_offset);
			sheet_offset += 4;
			if (!compressed_size || !CanRead(data, sheet_offset, compressed_size) ||
				!DecompressHuffman8(
					data.data() + sheet_offset,
					compressed_size,
					sheet_size,
					sheet))
			{
				return false;
			}
			sheet_offset += compressed_size;
		}
		else
		{
			if (!CanRead(data, sheet_offset, sheet_size))
				return false;
			sheet.assign(
				data.begin() + static_cast<std::ptrdiff_t>(sheet_offset),
				data.begin() + static_cast<std::ptrdiff_t>(sheet_offset + sheet_size));
			sheet_offset += sheet_size;
		}

		sheet_data.push_back(std::move(sheet));
	}

	texture_objects.resize(sheet_data.size());
	for (size_t i = 0; i < sheet_data.size(); ++i)
	{
		GX_InitTexObj(
			&texture_objects[i],
			sheet_data[i].data(),
			sheet_width,
			sheet_height,
			static_cast<uint8_t>(sheet_format),
			0,
			0,
			0);
		GX_InitTexObjFilterMode(&texture_objects[i], 1, 1);
	}

	loaded = true;
	return true;
}

uint16_t Font::FindGlyphIndex(uint16_t character) const
{
	for (const CodeMap& code_map : code_maps)
	{
		if (character < code_map.ccode_begin || character > code_map.ccode_end)
			continue;

		switch (code_map.mapping_method)
		{
		case 0:
		{
			const uint16_t index = static_cast<uint16_t>(
				code_map.map_info[0] + character - code_map.ccode_begin);
			if (index != std::numeric_limits<uint16_t>::max())
				return index;
			break;
		}

		case 1:
		{
			const uint16_t index = code_map.map_info[character - code_map.ccode_begin];
			if (index != std::numeric_limits<uint16_t>::max())
				return index;
			break;
		}

		case 2:
		{
			const uint16_t count = code_map.map_info[0];
			for (uint16_t i = 0; i < count; ++i)
			{
				const size_t offset = 1 + static_cast<size_t>(i) * 2;
				if (code_map.map_info[offset] == character)
					return code_map.map_info[offset + 1];
			}
			break;
		}

		default:
			break;
		}
	}

	return alternate_char_index;
}

Font::CharWidths Font::FindWidths(uint16_t glyph_index) const
{
	for (const WidthBlock& width_block : width_blocks)
	{
		if (glyph_index >= width_block.index_begin &&
			glyph_index <= width_block.index_end)
		{
			return width_block.widths[glyph_index - width_block.index_begin];
		}
	}

	return default_width;
}

bool Font::GetGlyph(uint16_t character, Glyph& glyph) const
{
	if (!loaded)
		return false;

	const uint16_t glyph_index = FindGlyphIndex(character);
	const uint32_t cells_per_sheet = static_cast<uint32_t>(sheet_row) * sheet_line;
	if (!cells_per_sheet)
		return false;

	const uint16_t glyph_sheet = static_cast<uint16_t>(glyph_index / cells_per_sheet);
	if (glyph_sheet >= sheet_count)
		return false;

	const uint32_t glyph_cell = glyph_index % cells_per_sheet;
	const uint32_t unit_x = glyph_cell % sheet_row;
	const uint32_t unit_y = glyph_cell / sheet_row;
	const uint32_t pixel_x = unit_x * (cell_width + 1);
	const uint32_t pixel_y = unit_y * (cell_height + 1);

	glyph.widths = FindWidths(glyph_index);
	glyph.sheet_index = glyph_sheet;
	glyph.height = cell_height;
	glyph.s1 = static_cast<float>(pixel_x + 1) / sheet_width;
	glyph.t1 = static_cast<float>(pixel_y + 1) / sheet_height;
	glyph.s2 = static_cast<float>(pixel_x + 1 + glyph.widths.glyph_width) /
		sheet_width;
	glyph.t2 = static_cast<float>(pixel_y + 1 + cell_height) / sheet_height;

	return true;
}

bool Font::Apply(uint16_t sheet_index) const
{
	if (!loaded || sheet_index >= texture_objects.size())
		return false;

	GX_LoadTexObj(&texture_objects[sheet_index], 0);
	return true;
}

}
