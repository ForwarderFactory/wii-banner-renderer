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

#ifndef WII_BNR_FONT_H_
#define WII_BNR_FONT_H_

#include <istream>
#include <vector>

#include "Pane.h"

namespace WiiBanner
{

class Font : public Named
{
public:
	struct CharWidths
	{
		s8 left{};
		u8 glyph_width{};
		s8 char_width{};
	};

	struct Glyph
	{
		CharWidths widths{};
		u16 sheet_index{};
		u8 height{};
		float s1{};
		float t1{};
		float s2{};
		float t2{};
	};

	bool Load(std::istream& file);

	[[nodiscard]] bool IsLoaded() const { return loaded; }
	[[nodiscard]] u8 GetWidth() const { return width; }
	[[nodiscard]] u8 GetHeight() const { return height; }
	[[nodiscard]] s8 GetLineFeed() const { return line_feed; }
	[[nodiscard]] bool GetGlyph(u16 character, Glyph& glyph) const;

	bool Apply(u16 sheet_index) const;

private:
	struct CodeMap
	{
		u16 ccode_begin{};
		u16 ccode_end{};
		u16 mapping_method{};
		std::vector<u16> map_info;
	};

	struct WidthBlock
	{
		u16 index_begin{};
		u16 index_end{};
		std::vector<CharWidths> widths;
	};

	[[nodiscard]] u16 FindGlyphIndex(u16 character) const;
	[[nodiscard]] CharWidths FindWidths(u16 glyph_index) const;

	bool loaded{};
	bool archived{};
	u16 alternate_char_index{};
	CharWidths default_width{};
	s8 line_feed{};
	u8 width{};
	u8 height{};

	u8 cell_width{};
	u8 cell_height{};
	u32 sheet_size{};
	u16 sheet_count{};
	u16 sheet_format{};
	u16 sheet_row{};
	u16 sheet_line{};
	u16 sheet_width{};
	u16 sheet_height{};

	std::vector<CodeMap> code_maps;
	std::vector<WidthBlock> width_blocks;
	std::vector<std::vector<u8>> sheet_data;
	mutable std::vector<GXTexObj> texture_objects;
};

class FontList : public std::vector<Font*>
{
public:
	static const u32 BINARY_MAGIC = MAKE_FOURCC('f', 'n', 'l', '1');
};

}

#endif
