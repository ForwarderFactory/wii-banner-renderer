/*
Copyright (c) 2010 - Wii Banner Player Project
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


#include <cstdio>

#include <libwb/Pane.h>
#include <libwb/Layout.h>
#include <libwb/Endian.h>
#include <libwb/Funcs.h>
#include <libwb/GXDebug.h>
#include <libwb/Material.h>
#include <libwb/WrapGx.h>
#include <algorithm>

#include "logger_holder.hpp"

namespace wb
{

void Pane::Load(std::istream& file)
{
	hide = false;

	file >> BE >> flags >> origin >> alpha;
	file.seekg(1, std::ios::cur);

	SetName(ReadFixedLengthString<NAME_LENGTH>(file));

	ReadFixedLengthString<USER_DATA_LENGTH>(file);	// user data

	file >> BE
		>> translate.x >> translate.y >> translate.z
		>> rotate.x >> rotate.y >> rotate.z
		>> scale.x >> scale.y
		>> width >> height;
}

Pane::~Pane()
{
	// delete children
	for (Pane* pane : panes)
		delete pane;
}

void Pane::SetFrame(FrameNumber frame, uint8_t key_set)
{
	// setframe on self
	Animator::SetFrame(frame, key_set);

	// setframe on children
	for (Pane* pane : panes)
		pane->SetFrame(frame, key_set);
}

namespace {
	int g_render_depth = 0;

	struct RenderDepth
	{
		RenderDepth() { ++g_render_depth; }
		~RenderDepth() { --g_render_depth; }
	};
}

void Pane::Render(const Resources& resources, uint8_t parent_alpha, bool widescreen, Mtx parent) const {
	const bool trace = gxdebug::Tracing(gxdebug::TRACE_DRAW);

	if (!GetVisible() || GetHide())
	{
		++gxdebug::GetStats().panes_skipped;

		if (trace)
			gxdebug::Log("[draw] %*sskip '%s' (%s): %s", g_render_depth * 2, "", GetName().c_str(), TypeName(),
				GetVisible() ? "hidden (language group)" : "not visible");
		return;
	}

	uint8_t render_alpha =
		GetInfluencedAlpha()
			? MultiplyColors(parent_alpha, GetAlpha())
			: GetAlpha();

	// kinda copied from usb loader gx
	float ws_scale = 1.f;

	if (widescreen) {
		ws_scale = WIDESCREEN_SCALE;
		widescreen = false;
	}

	++gxdebug::GetStats().panes_drawn;

	if (trace)
		gxdebug::Log("[draw] %*s'%s' (%s) alpha=%u (parent %u * own %u, influenced=%d) translate=(%g,%g,%g) rotate=(%g,%g,%g) scale=(%g,%g)%s",
			g_render_depth * 2, "", GetName().c_str(), TypeName(), static_cast<unsigned>(render_alpha),
			static_cast<unsigned>(parent_alpha), static_cast<unsigned>(GetAlpha()), GetInfluencedAlpha() ? 1 : 0,
			static_cast<double>(GetTranslate().x), static_cast<double>(GetTranslate().y), static_cast<double>(GetTranslate().z),
			static_cast<double>(GetRotate().x), static_cast<double>(GetRotate().y), static_cast<double>(GetRotate().z),
			static_cast<double>(GetScale().x * ws_scale), static_cast<double>(GetScale().y),
			ws_scale != 1.f ? " (widescreen squeeze applied)" : "");

	RenderDepth depth;

	Mtx local, step, matrix;

	guMtxTrans(local, GetTranslate().x, GetTranslate().y, GetTranslate().z);

	guMtxRotDeg(step, 'z', GetRotate().z);
	guMtxConcat(local, step, local);
	guMtxRotDeg(step, 'y', GetRotate().y);
	guMtxConcat(local, step, local);
	guMtxRotDeg(step, 'x', GetRotate().x);
	guMtxConcat(local, step, local);

	guMtxScale(step, GetScale().x * ws_scale, GetScale().y, 1.f);
	guMtxConcat(local, step, local);

	guMtxConcat(parent, local, matrix);

	Draw(resources, render_alpha, matrix);

	for (Pane* pane : panes)
		pane->Render(resources, render_alpha, widescreen, matrix);
}

Pane* Pane::FindPane(const std::string& find_name)
{
	if (find_name == GetName())
		return this;

	Pane* found = nullptr;

	for (Pane* pane : panes)
	{
		found = pane->FindPane(find_name);
		if (found)
			break;
	}

	return found;
}

void Pane::ProcessHermiteKey(const KeyType& type, float value)
{
	if (type.type == ANIMATION_TYPE_VERTEX_COLOR)	// vertex color
	{
		// only alpha is supported for Panes afaict
		if (0x10 == type.target)
		{
			alpha = ClampToByte(value);
			return;
		}
	}
	else if (type.type == ANIMATION_TYPE_PANE)	// pane animation
	{
		if (type.target < 10)
		{
			float* const values[] =
			{
				&translate.x,
				&translate.y,
				&translate.z,

				&rotate.x,
				&rotate.y,
				&rotate.z,

				&scale.x,
				&scale.y,

				&width,
				&height,
			};

			*values[type.target] = value;
			return;
		}
	}

	Base::ProcessHermiteKey(type, value);
}

void Pane::ProcessStepKey(const KeyType& type, StepKeyHandler::KeyData data)
{
	if (type.type == ANIMATION_TYPE_VISIBILITY)	// visibility
	{
		SetVisible(!!data.data2);
		return;
	}

	Base::ProcessStepKey(type, data);
}

void Quad::Load(std::istream& file)
{
	ReadBEArray(file, &vertex_colors->r, sizeof(vertex_colors));

	uint8_t tex_coord_count;

	file >> BE >> material_index >> tex_coord_count;
	file.seekg(1, std::ios::cur);

	tex_coords.resize(tex_coord_count);
	if (tex_coord_count)
		ReadBEArray(file, &tex_coords[0].coords->s, sizeof(TexCoords) / sizeof(float) * tex_coord_count);
}

void Quad::DebugPrintTexInfo(const Resources& resources) const
{
	logger.printf("[trace]     material index %u, %zu texcoord set(s)\n",
		static_cast<unsigned>(material_index), tex_coords.size());

	for (size_t i = 0; i != tex_coords.size(); ++i) {
		const auto& c = tex_coords[i].coords;
		logger.printf("[trace]       texcoords[%zu] TL=(%.4f,%.4f) TR=(%.4f,%.4f) "
			"BL=(%.4f,%.4f) BR=(%.4f,%.4f)\n", i, c[0].s, c[0].t, c[1].s,
			c[1].t, c[2].s, c[2].t, c[3].s, c[3].t);
	}

	if (material_index < resources.materials.size())
		resources.materials[material_index]->DebugPrintSrts();
}

void Quad::Draw(const Resources& resources, uint8_t render_alpha, Mtx mtx) const
{
	const Material* material = nullptr;

	if (material_index < resources.materials.size())
	{
		material = resources.materials[material_index];
		material->Apply(resources);
	}

	auto vertex_color = [this, material, render_alpha](unsigned int v)
	{
		return material
			? material->GetChannelColor(vertex_colors[v], render_alpha)
			: GXColor{
				vertex_colors[v].r,
				vertex_colors[v].g,
				vertex_colors[v].b,
				MultiplyColors(vertex_colors[v].a, render_alpha)
			};
	};

	++gxdebug::GetStats().quads;

	if (gxdebug::Tracing(gxdebug::TRACE_DRAW))
	{
		namespace gdn = gxdebug::names;

		std::string text = gxdebug::Format("[draw] %*squad %gx%g origin=(%u,%u) material=",
			g_render_depth * 2, "", static_cast<double>(GetWidth()), static_cast<double>(GetHeight()),
			static_cast<unsigned>(GetOriginX()), static_cast<unsigned>(GetOriginY()));

		if (material)
			text += "#" + std::to_string(material_index) + " '" + material->GetName() + "'";
		else
			text += "#" + std::to_string(material_index) + " !! OUT OF RANGE (" + std::to_string(resources.materials.size()) + " materials), drawn without a material";

		static const char* const corner_names[] = { "TL", "TR", "BL", "BR" };
		for (unsigned v = 0; v != 4; ++v)
		{
			const GXColor c = vertex_color(v);
			text += std::string(" ") + corner_names[v] + "=" + gdn::Color(c.r, c.g, c.b, c.a);
		}

		if (tex_coords.empty())
			text += " (no texcoords)";

		gxdebug::Log("%s", text.c_str());
	}

	Mtx scale, translate, quad_mtx;

	guMtxScale(scale, GetWidth(), GetHeight(), 1.f);
	guMtxTrans(translate, -0.5f * GetOriginX(), -0.5f * GetOriginY(), 1.f);

	guMtxConcat(mtx, scale, quad_mtx);
	guMtxConcat(quad_mtx, translate, quad_mtx);

	GX_LoadPosMtxImm(quad_mtx, GX_PNMTX0);

	const bool shared_texcoords = tex_coords.size() == 1;
	const unsigned texcoord_sets = shared_texcoords ? 8 : static_cast<unsigned>(std::min<size_t>(tex_coords.size(), 8));

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

	for (unsigned i = 0; i != texcoord_sets; ++i)
	{
		GX_SetVtxDesc(GX_VA_TEX0 + i, GX_DIRECT);
		GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0 + i, GX_TEX_ST, GX_F32, 0);
	}

	auto quad_vertex = [&](unsigned int v, float x, float y)
	{
		GX_Position2f32(x, y);

		const GXColor draw_color = vertex_color(v);
		GX_Color4u8(draw_color.r, draw_color.g, draw_color.b, draw_color.a);

		for (unsigned i = 0; i != texcoord_sets; ++i)
		{
			const auto& tc = shared_texcoords ? tex_coords.front() : tex_coords[i];
			GX_TexCoord2f32(tc.coords[v].s, tc.coords[v].t);
		}
	};

	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);

	quad_vertex(2, 0.f, 0.f);
	quad_vertex(3, 1.f, 0.f);
	quad_vertex(1, 1.f, 1.f);
	quad_vertex(0, 0.f, 1.f);

	GX_End();
}

void Quad::ProcessHermiteKey(const KeyType& type, float value)
{
	if (type.type == ANIMATION_TYPE_VERTEX_COLOR)	// vertex color
	{
		if (type.target < 0x10)
		{
			// vertex colors
			(&vertex_colors->r)[type.target] = ClampToByte(value);
			return;
		}
	}

	Base::ProcessHermiteKey(type, value);
}

void Pane::DebugDescribe(std::ostream& os, const Resources& resources, int indent) const
{
	using gxdebug::Print;

	Print(os, "%*s%s '%s'\n", indent, "", TypeName(), GetName().c_str());
	Print(os, "%*svisible=%d hide(language)=%d alpha=%u influenced_alpha=%d position_adjust=%d flags=0x%02X origin=%u (x=%u,y=%u)\n",
		indent + 2, "", GetVisible() ? 1 : 0, GetHide() ? 1 : 0, static_cast<unsigned>(GetAlpha()),
		GetInfluencedAlpha() ? 1 : 0, GetPositionAdjust() ? 1 : 0, static_cast<unsigned>(GetFlags()),
		static_cast<unsigned>(GetOriginRaw()), static_cast<unsigned>(GetOriginX()), static_cast<unsigned>(GetOriginY()));
	Print(os, "%*stranslate=(%g,%g,%g) rotate=(%g,%g,%g) scale=(%g,%g) size=%gx%g\n", indent + 2, "",
		static_cast<double>(translate.x), static_cast<double>(translate.y), static_cast<double>(translate.z),
		static_cast<double>(rotate.x), static_cast<double>(rotate.y), static_cast<double>(rotate.z),
		static_cast<double>(scale.x), static_cast<double>(scale.y), static_cast<double>(width), static_cast<double>(height));

	DebugDescribeKeys(os, indent + 2);
}

std::string Pane::DebugState() const
{
	using gxdebug::Format;

	return Format("visible=%d hide=%d alpha=%u translate=(%g,%g,%g) rotate=(%g,%g,%g) scale=(%g,%g) size=%gx%g",
		GetVisible() ? 1 : 0, GetHide() ? 1 : 0, static_cast<unsigned>(alpha),
		static_cast<double>(translate.x), static_cast<double>(translate.y), static_cast<double>(translate.z),
		static_cast<double>(rotate.x), static_cast<double>(rotate.y), static_cast<double>(rotate.z),
		static_cast<double>(scale.x), static_cast<double>(scale.y), static_cast<double>(width), static_cast<double>(height));
}

void Quad::DebugDescribe(std::ostream& os, const Resources& resources, int indent) const
{
	using gxdebug::Print;
	namespace gdn = gxdebug::names;

	Pane::DebugDescribe(os, resources, indent);

	Print(os, "%*smaterial #%u", indent + 2, "", static_cast<unsigned>(material_index));
	if (material_index < resources.materials.size())
		Print(os, " '%s'\n", resources.materials[material_index]->GetName().c_str());
	else
		Print(os, " !! OUT OF RANGE (%zu materials): the quad is drawn without a material\n", resources.materials.size());

	static const char* const corner_names[] = { "TL", "TR", "BL", "BR" };

	Print(os, "%*svertex colours:", indent + 2, "");
	for (unsigned i = 0; i != 4; ++i)
		Print(os, " %s=%s", corner_names[i], gdn::Color(vertex_colors[i].r, vertex_colors[i].g, vertex_colors[i].b, vertex_colors[i].a).c_str());
	Print(os, "\n");

	for (size_t i = 0; i != tex_coords.size(); ++i)
	{
		const auto& c = tex_coords[i].coords;

		Print(os, "%*stexcoord set %zu: TL=(%g,%g) TR=(%g,%g) BL=(%g,%g) BR=(%g,%g)\n", indent + 2, "", i,
			static_cast<double>(c[0].s), static_cast<double>(c[0].t), static_cast<double>(c[1].s), static_cast<double>(c[1].t),
			static_cast<double>(c[2].s), static_cast<double>(c[2].t), static_cast<double>(c[3].s), static_cast<double>(c[3].t));
	}

	if (tex_coords.empty())
		Print(os, "%*sno texcoord sets\n", indent + 2, "");
}

std::string Quad::DebugState() const
{
	using gxdebug::Format;
	namespace gdn = gxdebug::names;

	std::string state = Pane::DebugState();

	static const char* const corner_names[] = { "TL", "TR", "BL", "BR" };
	for (unsigned i = 0; i != 4; ++i)
		state += Format(" vtx%s=%s", corner_names[i],
			gdn::Color(vertex_colors[i].r, vertex_colors[i].g, vertex_colors[i].b, vertex_colors[i].a).c_str());

	return state;
}

}
