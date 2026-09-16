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

#include <GL/glew.h>

#include <algorithm>
#include <limits>
#include <vector>

#include "../include/libwb/Textbox.h"
#include "../include/libwb/Layout.h"
#include "../include/libwb/Endian.h"
#include "../include/libwb/Material.h"

namespace WiiBanner
{

void Textbox::Load(std::istream& file)
{
	const std::streamoff section_start = file.tellg() - std::streamoff(8);

	Pane::Load(file);

	uint16_t text_buf_bytes, text_str_bytes;
	file >> BE >> text_buf_bytes >> text_str_bytes
		>> material_index >> font_index >> text_position >> text_alignment;

	file.seekg(2, std::ios::cur);

	uint32_t text_str_offset;
	file >> BE >> text_str_offset;

	ReadBEArray(file, &colors->r, sizeof(colors));

	file >> BE >> font_width >> font_height >> space_char >> space_line;

	text.clear();
	file.seekg(section_start + static_cast<std::streamoff>(text_str_offset),
		std::ios::beg);
	for (uint16_t i = 0; i < text_str_bytes / sizeof(uint16_t); ++i)
	{
		uint16_t character;
		file >> BE >> character;
		if (!file || !character)
			break;
		text.push_back(static_cast<char16_t>(character));
	}
}

namespace
{

	void SetupTextTevStages()
	{
		GX_SetTevOrder(0, GX_TEXCOORD0, 0, 4);
		GX_SetTevColorIn(0, 0xf, 0xf, 0xf, 8);
		GX_SetTevAlphaIn(0, 0x7, 0x7, 0x7, 4);
		GX_SetTevColorOp(0, 0, 0, 0, 1, 0);
		GX_SetTevAlphaOp(0, 0, 0, 0, 1, 0);
		GX_SetTevSwapMode(0, 0, 0);

		GX_SetTevOrder(1, 0xff, 0xff, 4);
		GX_SetTevColorIn(1, 0xf, 0, 0xa, 0xf);
		GX_SetTevAlphaIn(1, 0x7, 0, 5, 0x7);
		GX_SetTevColorOp(1, 0, 0, 0, 1, 0);
		GX_SetTevAlphaOp(1, 0, 0, 0, 1, 0);
		GX_SetTevSwapMode(1, 0, 0);

		// we must call this last cuz this generates and compiles the shader
		GX_SetNumTevStages(2);
	}

}

void Textbox::Draw(const Resources& resources, uint8_t render_alpha) const
{
	if (text.empty() || font_index >= resources.fonts.size())
		return;

	Font* const font = resources.fonts[font_index];
	if (!font || !font->IsLoaded() || !font->GetWidth() || !font->GetHeight())
		return;

	const Material* material = nullptr;
	if (material_index < resources.materials.size())
	{
		material = resources.materials[material_index];
		material->Apply(resources);
	}

	SetupTextTevStages();

	const float scale_x = font_width / font->GetWidth();
	const float scale_y = font_height / font->GetHeight();
	const float font_line_feed = font->GetLineFeed() > 0
		? font->GetLineFeed() * scale_y
		: font_height;
	const float line_advance = font_line_feed + space_line;

	std::vector<float> line_widths(1, 0.f);
	bool first_character = true;
	for (char16_t character : text)
	{
		if (character == u'\n')
		{
			line_widths.push_back(0.f);
			first_character = true;
			continue;
		}

		Font::Glyph glyph;
		if (!font->GetGlyph(static_cast<uint16_t>(character), glyph))
			continue;

		if (!first_character)
			line_widths.back() += space_char;
		line_widths.back() += glyph.widths.char_width * scale_x;
		first_character = false;
	}

	const float frame_width = *std::max_element(
		line_widths.begin(), line_widths.end());
	const float frame_height = font_height +
		(line_widths.size() - 1) * line_advance;
	const uint8_t align_horizontal = text_position % 3;
	const uint8_t align_vertical = text_position / 3;

	auto line_start = [&](size_t line_number)
	{
		const float text_width = align_horizontal == 1
			? line_widths[line_number]
			: frame_width;
		return -0.5f * (GetOriginX() * GetWidth() +
			align_horizontal * (-GetWidth() + text_width));
	};

	float x_position = line_start(0);
	float y_position = -0.5f * (
		align_vertical * -frame_height +
		GetHeight() * (align_vertical - (2 - GetOriginY()))) - font_height;
	size_t line_number = 0;
	uint16_t last_sheet = std::numeric_limits<uint16_t>::max();
	first_character = true;

	glPushMatrix();

	for (char16_t character : text)
	{
		if (character == u'\n')
		{
			++line_number;
			if (line_number >= line_widths.size())
				break;
			x_position = line_start(line_number);
			y_position -= line_advance;
			first_character = true;
			continue;
		}

		Font::Glyph glyph;
		if (!font->GetGlyph(static_cast<uint16_t>(character), glyph))
			continue;

		if (!first_character)
			x_position += space_char;

		if (glyph.sheet_index != last_sheet)
		{
			if (!font->Apply(glyph.sheet_index))
				continue;
			last_sheet = glyph.sheet_index;
		}

		const float glyph_x = x_position + glyph.widths.left * scale_x;
		const float glyph_width = glyph.widths.glyph_width * scale_x;

		const GXColor bottom_color = material
			? material->GetChannelColor(colors[1], render_alpha)
			: GXColor{ colors[1].r, colors[1].g, colors[1].b, MultiplyColors(colors[1].a, render_alpha) };

		const GXColor top_color = material
			? material->GetChannelColor(colors[0], render_alpha)
			: GXColor{ colors[0].r, colors[0].g, colors[0].b, MultiplyColors(colors[0].a, render_alpha) };

		glBegin(GL_QUADS);

		glColor4ub(bottom_color.r, bottom_color.g, bottom_color.b, bottom_color.a);
		glTexCoord2f(glyph.s1, glyph.t2);
		glVertex2f(glyph_x, y_position);

		glTexCoord2f(glyph.s2, glyph.t2);
		glVertex2f(glyph_x + glyph_width, y_position);

		glColor4ub(top_color.r, top_color.g, top_color.b, top_color.a);
		glTexCoord2f(glyph.s2, glyph.t1);
		glVertex2f(glyph_x + glyph_width, y_position + font_height);

		glTexCoord2f(glyph.s1, glyph.t1);
		glVertex2f(glyph_x, y_position + font_height);

		glEnd();

		x_position += glyph.widths.char_width * scale_x;
		first_character = false;
	}

	glPopMatrix();
}

void Textbox::ProcessHermiteKey(const KeyType& type, float value)
{
	if (type.type == ANIMATION_TYPE_VERTEX_COLOR)
	{
		if (type.target < 4)
		{
			(&colors[0].r)[type.target] = static_cast<uint8_t>(value);
			return;
		}
		if (type.target >= 8 && type.target < 12)
		{
			(&colors[1].r)[type.target - 8] = static_cast<uint8_t>(value);
			return;
		}
	}

	Base::ProcessHermiteKey(type, value);
}

}