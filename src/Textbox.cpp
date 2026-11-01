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


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#include "../include/libwb/Textbox.h"

#include "logger_holder.hpp"
#include "../include/libwb/Layout.h"
#include "../include/libwb/Endian.h"
#include "../include/libwb/GxDebug.h"
#include "../include/libwb/Font.h"
#include "../include/libwb/Material.h"

namespace wb
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

	if (std::getenv("WB_TEXT_DEBUG"))
	{
		bool has_control = false;
		for (char16_t c : text)
			has_control = has_control || (c < 0x20 && c != u'\n');

		if (has_control)
		{
			logger.printf("[text] %zu units:", text.size());
			for (char16_t c : text)
				logger.printf(" %04X", static_cast<unsigned>(c));
			logger.printf("\n[text] ascii:   ");
			for (char16_t c : text)
				logger.printf("%c", (c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '.');
			logger.printf("\n");
		}
	}
}

namespace
{
	float NextTabStop(float position, float tab_stop)
	{
		if (!(tab_stop > 0.f))
			return position;
		return (std::floor(position / tab_stop + 0.0001f) + 1.f) * tab_stop;
	}

	std::u16string LayoutText(const std::u16string& source)
	{
		std::u16string clean;
		clean.reserve(source.size());
		for (char16_t character : source)
		{
			if (character == u'\n' || character == u'\t' || character >= 0x20)
				clean.push_back(character);
		}

		return clean;
	}

	void SetupTextTevStages()
	{
		GX_SetTevOrder(0, GX_TEXCOORD0, 0, 4);
		GX_SetTevColorIn(0, 2 /*C0*/, 4 /*C1*/, 8 /*TEXC*/, 0xf /*ZERO*/);
		GX_SetTevAlphaIn(0, 1 /*A0*/, 2 /*A1*/, 4 /*TEXA*/, 0x7 /*ZERO*/);
		GX_SetTevColorOp(0, 0, 0, 0, 1, 0);
		GX_SetTevAlphaOp(0, 0, 0, 0, 1, 0);
		GX_SetTevSwapMode(0, 0, 0);

		GX_SetTevOrder(1, 0xff, 0xff, 4);
		GX_SetTevColorIn(1, 0xf, 0, 0xa, 0xf);
		GX_SetTevAlphaIn(1, 0x7, 0, 5, 0x7);
		GX_SetTevColorOp(1, 0, 0, 0, 1, 0);
		GX_SetTevAlphaOp(1, 0, 0, 0, 1, 0);
		GX_SetTevSwapMode(1, 0, 0);

		GX_SetNumIndStages(0);
		GX_SetTevIndirect(0, 0, GX_ITF_8, GX_ITB_NONE, GX_ITM_OFF, 0, 0, 0, 0, 0);
		GX_SetTevIndirect(1, 0, GX_ITF_8, GX_ITB_NONE, GX_ITM_OFF, 0, 0, 0, 0, 0);

		// we must call this last cuz this generates and compiles the shader
		GX_SetNumTevStages(2);
	}

}

void Textbox::Draw(const Resources& resources, uint8_t render_alpha, Mtx mtx) const
{
	const bool trace = gxdebug::Tracing(gxdebug::TRACE_DRAW);

	if (text.empty() || font_index >= resources.fonts.size())
	{
		if (trace)
			gxdebug::Log("[draw]   text '%s' skipped: %s", GetName().c_str(),
				text.empty() ? "empty text" : "font index out of range");
		return;
	}

	Font* const font = resources.fonts[font_index];
	if (!font || !font->IsLoaded() || !font->GetWidth() || !font->GetHeight())
	{
		if (trace)
			gxdebug::Log("[draw]   text '%s' skipped: font #%u '%s' is not loaded (nothing will be drawn)", GetName().c_str(),
				static_cast<unsigned>(font_index), font ? font->GetName().c_str() : "?");
		return;
	}

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

	const float tab_stop = 4.f * font_width;

	const std::u16string laid_out = LayoutText(text);
	if (laid_out.empty())
	{
		if (trace)
			gxdebug::Log("[draw]   text '%s' skipped", GetName().c_str());
		return;
	}

	std::vector<float> line_widths(1, 0.f);
	bool first_character = true;
	for (char16_t character : laid_out)
	{
		if (character == u'\n')
		{
			line_widths.push_back(0.f);
			first_character = true;
			continue;
		}

		if (character == u'\t')
		{
			line_widths.back() = NextTabStop(line_widths.back(), tab_stop);
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
	float line_origin = x_position;
	float y_position = -0.5f * (
		align_vertical * -frame_height +
		GetHeight() * (align_vertical - (2 - GetOriginY()))) - font_height;
	size_t line_number = 0;
	uint16_t last_sheet = std::numeric_limits<uint16_t>::max();
	first_character = true;

	if (trace)
	{
		std::string widths;
		for (float w : line_widths)
			widths += gxdebug::Format(" %g", static_cast<double>(w));

		gxdebug::Log("[draw]   text '%s' \"%s\" font #%u '%s' glyph size=%gx%g (font %ux%u, scale %g,%g) space=%g,%g position=%u alignment=%u\n"
			"[draw]     frame %gx%g, %zu line(s), line widths:%s, start=(%g,%g)",
			GetName().c_str(), gxdebug::ToUtf8(laid_out).c_str(), static_cast<unsigned>(font_index), font->GetName().c_str(),
			static_cast<double>(font_width), static_cast<double>(font_height), static_cast<unsigned>(font->GetWidth()),
			static_cast<unsigned>(font->GetHeight()), static_cast<double>(scale_x), static_cast<double>(scale_y),
			static_cast<double>(space_char), static_cast<double>(space_line), static_cast<unsigned>(text_position),
			static_cast<unsigned>(text_alignment), static_cast<double>(frame_width), static_cast<double>(frame_height),
			line_widths.size(), widths.c_str(), static_cast<double>(x_position), static_cast<double>(y_position));
	}

	// glyphs are positioned in the pane's own space
	GX_LoadPosMtxImm(mtx, GX_PNMTX0);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	for (char16_t character : laid_out)
	{
		if (character == u'\n')
		{
			++line_number;
			if (line_number >= line_widths.size())
				break;
			x_position = line_start(line_number);
			line_origin = x_position;
			y_position -= line_advance;
			first_character = true;
			continue;
		}

		if (character == u'\t')
		{
			x_position = line_origin +
				NextTabStop(x_position - line_origin, tab_stop);
			first_character = true;
			continue;
		}

		Font::Glyph glyph;
		if (!font->GetGlyph(static_cast<uint16_t>(character), glyph))
		{
			++gxdebug::GetStats().missing_glyphs;
			if (trace)
				gxdebug::Log("[draw]     !! no glyph for U+%04X in font '%s', skipped", static_cast<unsigned>(character),
					font->GetName().c_str());
			continue;
		}

		if (!first_character)
			x_position += space_char;

		if (glyph.sheet_index != last_sheet)
		{
			if (!font->Apply(glyph.sheet_index))
			{
				if (trace)
					gxdebug::Log("[draw]     !! font '%s' has no sheet %u for U+%04X, skipped", font->GetName().c_str(),
						static_cast<unsigned>(glyph.sheet_index), static_cast<unsigned>(character));
				continue;
			}
			last_sheet = glyph.sheet_index;
		}

		const float glyph_x = x_position + glyph.widths.left * scale_x;
		const float glyph_width = glyph.widths.glyph_width * scale_x;

		const float glyph_height = glyph.height * scale_y;
		const float glyph_top = y_position + font_height;
		const float glyph_bottom = glyph_top - glyph_height;

		const GXColor bottom_color = material
			? material->GetChannelColor(colors[1], render_alpha)
			: GXColor{ colors[1].r, colors[1].g, colors[1].b, MultiplyColors(colors[1].a, render_alpha) };

		const GXColor top_color = material
			? material->GetChannelColor(colors[0], render_alpha)
			: GXColor{ colors[0].r, colors[0].g, colors[0].b, MultiplyColors(colors[0].a, render_alpha) };

		++gxdebug::GetStats().glyphs;

		if (trace)
			gxdebug::Log("[draw]     U+%04X '%s' sheet=%u x=%g..%g y=%g..%g uv=(%g,%g)-(%g,%g) top=%s bottom=%s advance=%g",
				static_cast<unsigned>(character), gxdebug::ToUtf8(std::u16string(1, character)).c_str(),
				static_cast<unsigned>(glyph.sheet_index), static_cast<double>(glyph_x),
				static_cast<double>(glyph_x + glyph_width), static_cast<double>(glyph_bottom), static_cast<double>(glyph_top),
				static_cast<double>(glyph.s1), static_cast<double>(glyph.t1), static_cast<double>(glyph.s2),
				static_cast<double>(glyph.t2), gxdebug::names::Color(top_color.r, top_color.g, top_color.b, top_color.a).c_str(),
				gxdebug::names::Color(bottom_color.r, bottom_color.g, bottom_color.b, bottom_color.a).c_str(),
				static_cast<double>(glyph.widths.char_width * scale_x));

		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);

		GX_Position2f32(glyph_x, glyph_bottom);
		GX_Color4u8(bottom_color.r, bottom_color.g, bottom_color.b, bottom_color.a);
		GX_TexCoord2f32(glyph.s1, glyph.t2);

		GX_Position2f32(glyph_x + glyph_width, glyph_bottom);
		GX_Color4u8(bottom_color.r, bottom_color.g, bottom_color.b, bottom_color.a);
		GX_TexCoord2f32(glyph.s2, glyph.t2);

		GX_Position2f32(glyph_x + glyph_width, glyph_top);
		GX_Color4u8(top_color.r, top_color.g, top_color.b, top_color.a);
		GX_TexCoord2f32(glyph.s2, glyph.t1);

		GX_Position2f32(glyph_x, glyph_top);
		GX_Color4u8(top_color.r, top_color.g, top_color.b, top_color.a);
		GX_TexCoord2f32(glyph.s1, glyph.t1);

		GX_End();

		x_position += glyph.widths.char_width * scale_x;
		first_character = false;
	}

}

void Textbox::DebugDescribe(std::ostream& os, const Resources& resources, int indent) const
{
	using gxdebug::Print;
	namespace gdn = gxdebug::names;

	Pane::DebugDescribe(os, resources, indent);

	Print(os, "%*smaterial #%u", indent + 2, "", static_cast<unsigned>(material_index));
	if (material_index < resources.materials.size())
		Print(os, " '%s'\n", resources.materials[material_index]->GetName().c_str());
	else
		Print(os, " !! OUT OF RANGE (%zu materials): drawn without a material\n", resources.materials.size());

	Print(os, "%*sfont #%u", indent + 2, "", static_cast<unsigned>(font_index));
	if (font_index < resources.fonts.size())
		Print(os, " '%s'%s\n", resources.fonts[font_index]->GetName().c_str(),
			resources.fonts[font_index]->IsLoaded() ? "" : " !! NOT LOADED, nothing will be drawn");
	else
		Print(os, " !! OUT OF RANGE (%zu fonts)\n", resources.fonts.size());

	Print(os, "%*sglyph size=%gx%g char spacing=%g line spacing=%g text_position=%u (h=%u,v=%u) text_alignment=%u\n", indent + 2, "",
		static_cast<double>(font_width), static_cast<double>(font_height), static_cast<double>(space_char),
		static_cast<double>(space_line), static_cast<unsigned>(text_position), static_cast<unsigned>(text_position % 3),
		static_cast<unsigned>(text_position / 3), static_cast<unsigned>(text_alignment));
	Print(os, "%*scolours: top=%s bottom=%s\n", indent + 2, "", gdn::Color(colors[0].r, colors[0].g, colors[0].b, colors[0].a).c_str(),
		gdn::Color(colors[1].r, colors[1].g, colors[1].b, colors[1].a).c_str());

	Print(os, "%*stext (%zu UTF-16 units): \"", indent + 2, "", text.size());
	for (char16_t c : text)
	{
		if (c == u'\n')
			os << "\\n";
		else if (c == u'\t')
			os << "\\t";
		else if (c < 0x20)
			Print(os, "\\u%04X", static_cast<unsigned>(c));
		else
			os << gxdebug::ToUtf8(std::u16string(1, c));
	}
	Print(os, "\"\n");

	bool has_control = false;
	for (char16_t c : text)
		has_control = has_control || (c < 0x20 && c != u'\n' && c != u'\t');

	if (has_control)
	{
		Print(os, "%*s!! contains control characters (these are tags, not supported yet), units:", indent + 2, "");
		for (char16_t c : text)
			Print(os, " %04X", static_cast<unsigned>(c));
		Print(os, "\n");
	}
}

std::string Textbox::DebugState() const
{
	using gxdebug::Format;
	namespace gdn = gxdebug::names;

	return Pane::DebugState() + Format(" textTop=%s textBottom=%s", gdn::Color(colors[0].r, colors[0].g, colors[0].b, colors[0].a).c_str(),
		gdn::Color(colors[1].r, colors[1].g, colors[1].b, colors[1].a).c_str());
}

void Textbox::ProcessHermiteKey(const KeyType& type, float value)
{
	if (type.type == ANIMATION_TYPE_VERTEX_COLOR)
	{
		if (type.target < 4)
		{
			(&colors[0].r)[type.target] = ClampToByte(value);
			return;
		}
		if (type.target >= 8 && type.target < 12)
		{
			(&colors[1].r)[type.target - 8] = ClampToByte(value);
			return;
		}
	}

	Base::ProcessHermiteKey(type, value);
}

}