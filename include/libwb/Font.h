/*
Copyright (c) 2010 - Wii Banner Player Project
Copyright (c) 2012 - giantpune
Copyright (c) 2012 - Dimok
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

#ifndef WII_BNR_FONT_H_
#define WII_BNR_FONT_H_

#include <istream>
#include <vector>

#include <libwb/Pane.h>

namespace wb
{

class Font : public Named
{
public:
	struct CharWidths
	{
		int8_t left{};
		uint8_t glyph_width{};
		int8_t char_width{};
	};

	struct Glyph
	{
		CharWidths widths{};
		uint16_t sheet_index{};
		uint8_t height{};
		float s1{};
		float t1{};
		float s2{};
		float t2{};
	};

	bool Load(std::istream& file);

	[[nodiscard]] bool IsLoaded() const { return loaded; }
	[[nodiscard]] uint8_t GetWidth() const { return width; }
	[[nodiscard]] uint8_t GetHeight() const { return height; }
	[[nodiscard]] int8_t GetLineFeed() const { return line_feed; }
	[[nodiscard]] bool GetGlyph(uint16_t character, Glyph& glyph) const;

	bool Apply(uint16_t sheet_index) const;

private:
	struct CodeMap
	{
		uint16_t ccode_begin{};
		uint16_t ccode_end{};
		uint16_t mapping_method{};
		std::vector<uint16_t> map_info;
	};

	struct WidthBlock
	{
		uint16_t index_begin{};
		uint16_t index_end{};
		std::vector<CharWidths> widths;
	};

	[[nodiscard]] uint16_t FindGlyphIndex(uint16_t character) const;
	[[nodiscard]] CharWidths FindWidths(uint16_t glyph_index) const;

	bool loaded{};
	bool archived{};
	uint16_t alternate_char_index{};
	CharWidths default_width{};
	int8_t line_feed{};
	uint8_t width{};
	uint8_t height{};

	uint8_t cell_width{};
	uint8_t cell_height{};
	uint32_t sheet_size{};
	uint16_t sheet_count{};
	uint16_t sheet_format{};
	uint16_t sheet_row{};
	uint16_t sheet_line{};
	uint16_t sheet_width{};
	uint16_t sheet_height{};

	std::vector<CodeMap> code_maps;
	std::vector<WidthBlock> width_blocks;
	std::vector<std::vector<uint8_t>> sheet_data;
	mutable std::vector<GXTexObj> texture_objects;
};

class FontList : public std::vector<Font*>
{
public:
	static const uint32_t BINARY_MAGIC = MAKE_FOURCC('f', 'n', 'l', '1');
};

}

#endif
