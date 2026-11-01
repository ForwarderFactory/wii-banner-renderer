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
#include <string>
#include <set>
#include <cstdlib>

#include <algorithm>
#include <cmath>

#include <libwb/Material.h>
#include <libwb/Endian.h>
#include <libwb/Funcs.h>
#include <libwb/GXDebug.h>
#include <libwb/Layout.h>
#include <libwb/Texture.h>

#include "logger_holder.hpp"

namespace wb
{

void Material::Load(std::istream& file) {
	SetName(ReadFixedLengthString<NAME_LENGTH>(file));

	// read colors
	ReadBEArray(file, &color_regs->r, sizeof(color_regs) / sizeof(int16_t));
	ReadBEArray(file, &color_constants->r, sizeof(color_constants));

	file >> BE >> flags.value;

	//logger << "flags: " << flags.value << '\n';

	// texture map
	for (uint32_t i = 0; i != flags.texture_map; ++i)
	{
		TextureMap map{};
		file >> BE >> map.tex_index >> map.wrap_s >> map.wrap_t;

		texture_maps.push_back(map);
	}

	// texture srt
	for (uint32_t i = 0; i != flags.texture_srt; ++i)
	{
		TextureSrt srt{};

		file >> BE >> srt.translate_s >> srt.translate_t >> srt.rotate >> srt.scale_s >> srt.scale_t;

		texture_srts.push_back(srt);
	}

	// texture coord gen
	for (uint32_t i = 0; i != flags.texture_coord_gen; ++i)
	{
		TextureCoordGen coord{};

		file >> BE >> coord.tgen_type >> coord.tgen_src >> coord.mtrx_src;
		file.seekg(1, std::ios::cur);

		texture_coord_gens.push_back(coord);
	}

	// channel control
	if (flags.channel_control)
	{
		file >> BE >> chan_ctrl.color_mat_src >> chan_ctrl.alpha_mat_src;
		file.seekg(2, std::ios::cur);
	}

	// material color
	if (flags.material_color)
	{
		ReadBEArray(file, &color.r, sizeof(color));
	}
	else
	{
		memset(&color.r, 0xff, 4);
	}

	// tev swap table
	if (flags.tev_swap_table)
	{
		ReadBEArray(file, &tev_swap_table->value, sizeof(tev_swap_table));

		//logger << "TevSwapModeTable:\n";
		//for (unsigned int i = 0; i != 4; ++i)
		//	logger << "\t[" << i << "]: red: " << (int)tev_swap_mode_table[i].red
		//		<< " green: " << (int)tev_swap_mode_table[i].green
		//		<< " blue: " << (int)tev_swap_mode_table[i].blue
		//		<< " alpha: " << (int)tev_swap_mode_table[i].alpha << '\n';
	}
	else
	{
		tev_swap_table[0].r = 0;
		tev_swap_table[0].g = 1;
		tev_swap_table[0].b = 2;
		tev_swap_table[0].a = 3;

		for (unsigned int i = 1; i != 4; ++i)
		{
			tev_swap_table[i].r = i - 1;
			tev_swap_table[i].g = i - 1;
			tev_swap_table[i].b = i - 1;
			tev_swap_table[i].a = 3;
		}
	}

	// ind srt
	for (uint32_t i = 0; i != flags.ind_srt; ++i)
	{
		IndSrt srt{};

		file >> BE
			>> srt.translate_s
			>> srt.translate_t
			>> srt.rotate
			>> srt.scale_s
			>> srt.scale_t;

		ind_srts.push_back(srt);
	}

	// ind stage
	for (uint32_t i = 0; i != flags.ind_stage; ++i)
	{
		IndStage stage{};

		file >> BE >> stage.tex_coord >> stage.tex_map >> stage.scale_s >> stage.scale_t;

		ind_stages.push_back(stage);
	}

	// tev stage
	for (uint32_t i = 0; i != flags.tev_stage; ++i)
	{
		TevStage ts{};

		file.read(ts.data, sizeof(ts.data));

		tev_stages.push_back(ts);
	}
	if (!flags.tev_stage)
	{
		constexpr uint8_t CPREV = 0, C0 = 2, C1 = 4, TEXC = 8, RASC = 10, KONST = 14, CZERO = 15;
		constexpr uint8_t APREV = 0, A0 = 1, A1 = 2, TEXA = 4, RASA = 5, AKONST = 6, AZERO = 7;
		constexpr uint8_t DISABLED = 0xff;

		auto make_stage = [](uint8_t tex_map, uint8_t tex_coord,
			uint8_t ca, uint8_t cb, uint8_t cc, uint8_t cd,
			uint8_t aa, uint8_t ab, uint8_t ac, uint8_t ad,
			uint8_t kcolor_sel = 0, uint8_t kalpha_sel = 0)
		{
			TevStage tev{};
			memset(tev.data, 0, sizeof(tev.data));

			tev.tex_map = tex_map;
			tev.tex_coord = tex_coord;

			tev.color_in.a = ca;
			tev.color_in.b = cb;
			tev.color_in.c = cc;
			tev.color_in.d = cd;
			tev.color_in.clamp = 1;
			tev.color_in.constant_sel = kcolor_sel;

			tev.alpha_in.a = aa;
			tev.alpha_in.b = ab;
			tev.alpha_in.c = ac;
			tev.alpha_in.d = ad;
			tev.alpha_in.clamp = 1;
			tev.alpha_in.constant_sel = kalpha_sel;

			return tev;
		};

		if (flags.texture_map == 0)
		{
			tev_stages.push_back(make_stage(
				DISABLED, DISABLED,
				CZERO, C1, RASC, CZERO,
				AZERO, A1, RASA, AZERO));
		}
		else if (flags.texture_map == 1)
		{
			tev_stages.push_back(make_stage(
				0, 0,
				C0, C1, TEXC, CZERO,
				A0, A1, TEXA, AZERO));

			tev_stages.push_back(make_stage(
				DISABLED, DISABLED,
				CZERO, CPREV, RASC, CZERO,
				AZERO, APREV, RASA, AZERO));
		}
		else if (flags.texture_map == 2)
		{
			tev_stages.push_back(make_stage(
				0, 0,
				CZERO, CZERO, CZERO, TEXC,
				AZERO, AZERO, AZERO, TEXA));

			tev_stages.push_back(make_stage(
				1, 1,
				TEXC, CPREV, KONST, CZERO,
				TEXA, APREV, AKONST, AZERO,
				0x1f, 0x1f));

			tev_stages.push_back(make_stage(
				DISABLED, DISABLED,
				CZERO, CPREV, RASC, CZERO,
				AZERO, APREV, RASA, AZERO));
		}
		else
		{
			static const uint8_t k_select[] = { 0x1f, 0x1b, 0x17, 0x13, 0x1e, 0x1a, 0x16, 0x12 };

			for (uint32_t i = 0; i != flags.texture_map && i != MAX_TEV_STAGES; ++i)
			{
				const uint8_t cd = i ? CZERO : CPREV;
				const uint8_t ad = i ? AZERO : APREV;
				const uint8_t ksel = k_select[i < 8 ? i : 0];

				tev_stages.push_back(make_stage(
					(uint8_t)i, (uint8_t)i,
					CZERO, TEXC, KONST, cd,
					AZERO, TEXA, AKONST, ad,
					ksel, ksel));
			}

			tev_stages.push_back(make_stage(
				DISABLED, DISABLED,
				C0, C1, CPREV, CZERO,
				A0, A1, APREV, AZERO));

			tev_stages.push_back(make_stage(
				DISABLED, DISABLED,
				CZERO, CPREV, RASC, CZERO,
				AZERO, APREV, RASA, AZERO));
		}
	}

	// alpha compare
	if (flags.alpha_compare)
	{
		file >> BE >> alpha_compare.function >> alpha_compare.op
			>> alpha_compare.ref0 >> alpha_compare.ref1;

		//logger << "alpha compare:\t"
		//	<< " function: " << (int)alpha_compare.function
		//	<< " op: " << (int)alpha_compare.op << " ref0: " << (int)alpha_compare.ref0
		//	<< " ref1: " << (int)alpha_compare.ref1 << '\n';
		//std::cin.get();
	}
	else
	{
		alpha_compare.function = 0x66;
		alpha_compare.ref0 = 0;
		alpha_compare.ref1 = 0;
		//alpha_compare.op = ;
	}

	// blend mode
	if (flags.blend_mode)
	{
		file >> BE >> blend_mode.type >> blend_mode.src_factor >> blend_mode.dst_factor >> blend_mode.logical_op;

		//logger << "blend mode:\t"
		//	<< " type: " << (int)blend_mode.type
		//	<< " src: " << (int)blend_mode.src_factor << " dst: " << (int)blend_mode.dst_factor
		//	<< " op: " << (int)blend_mode.logical_op << '\n';
		//std::cin.get();
	}
	else
	{
		blend_mode.type = 1;
		blend_mode.src_factor = 4;
		blend_mode.dst_factor = 5;
		blend_mode.logical_op = 3;
	}

}

void Material::ApplyTextures(const Resources &resources) const {
	uint8_t tlut_name = 0;

	for (uint32_t i = 0; i < flags.texture_map; ++i) {
		const TextureMap& tr = texture_maps[i];

		if (palette_texture[i] == PALETTE_DEFAULT)
		{
			if (tr.tex_index < resources.textures.size())
				resources.textures[tr.tex_index]->Apply(
					tlut_name,
					i,
					tr.wrap_s,
					tr.wrap_t
				);
			else if (gxdebug::Tracing(gxdebug::TRACE_GX))
				gxdebug::Log("[gx] !! material '%s' texture map %u: texture index %u is out of range (%zu texture(s))",
					GetName().c_str(), i, static_cast<unsigned>(tr.tex_index), resources.textures.size());
		}
		else {
			if (palette_texture[i] == PALETTE_DEFAULT)
				continue;

			uint8_t palette_set = resources.cur_set;

			if (palette_set >= resources.palettes.size()) {
				palette_set = 0; // fallback, this is a hack
			}

			auto& palettes = resources.palettes.at(palette_set);

			if (palette_texture[i] >= palettes.size()) {
				logger << LogType::Warning
					<< "Palette index out of range: "
					<< static_cast<int>(palette_texture[i])
					<< "\n";
				return;
			}

			const auto& name = palettes[palette_texture[i]];

			for (auto texture : resources.textures) {
				if (texture->GetName() == name) {
					texture->Apply(
						tlut_name,
						i,
						tr.wrap_s,
						tr.wrap_t
					);
					break;
				}
			}
		}
	}

	//GX_InvalidateTexAll();
}

void Material::Apply(const Resources& resources) const
{
	if (gxdebug::Tracing(gxdebug::TRACE_GX))
		gxdebug::Log("[gx] --- Material::Apply '%s'", GetName().c_str());

	// alpha compare
	GX_SetAlphaCompare(alpha_compare.function & 0xf, alpha_compare.ref0,
		alpha_compare.op, alpha_compare.function >> 4, alpha_compare.ref1);

	// blend mode
	if (std::getenv("WB_LAYOUT_DEBUG"))
	{
		static std::set<std::string> printed;
		if (printed.insert(GetName()).second)
		{
			logger.printf("[blend] material '%s': type=%d src=%d dst=%d op=%d\n",
				GetName().c_str(), static_cast<int>(blend_mode.type),
				static_cast<int>(blend_mode.src_factor),
				static_cast<int>(blend_mode.dst_factor),
				static_cast<int>(blend_mode.logical_op));
		}
	}

	GX_SetBlendMode(blend_mode.type, blend_mode.src_factor, blend_mode.dst_factor,
		blend_mode.logical_op);

	// tev reg colors
	for (unsigned int i = 0; i != 3; ++i)
		GX_SetTevColorS10(GX_TEVREG0 + i, color_regs[i]);

	// konst colors
	for (unsigned int i = 0; i != 4; ++i)
		GX_SetTevKColor(i, color_constants[i]);

	// texture coord gen
	{
	unsigned int i = 0;
	for (auto& tcg : texture_coord_gens)
	{
		if (tcg.mtrx_src != GX_IDENTITY)
		{
			Mtx texture_mtx;
			guMtxIdentity(texture_mtx);

			if (tcg.tgen_type == GX_TG_MTX2x4)
			{
				const uint8_t mtrx = (tcg.mtrx_src - GX_TEXMTX0) / 3;

				if (mtrx < texture_srts.size())
				{
					const auto& srt = texture_srts[mtrx];

					Mtx step;

					guMtxTrans(texture_mtx, 0.5f, 0.5f, 0.f);

					guMtxRotDeg(step, 'z', srt.rotate);
					guMtxConcat(texture_mtx, step, texture_mtx);

					guMtxScale(step, srt.scale_s, srt.scale_t, 1.f);
					guMtxConcat(texture_mtx, step, texture_mtx);

					guMtxTrans(step, srt.translate_s / srt.scale_s - 0.5f, srt.translate_t / srt.scale_t - 0.5f, 0.f);
					guMtxConcat(texture_mtx, step, texture_mtx);
				}
				else if (gxdebug::Tracing(gxdebug::TRACE_GX))
				{
					gxdebug::Log("[gx] !! texgen %u uses texture matrix %u but the material only has %zu texture srt(s)",
						i, static_cast<unsigned>(mtrx), texture_srts.size());
				}
			}

			GX_LoadTexMtxImm(texture_mtx, tcg.mtrx_src, GX_MTX2x4);
		}

		GX_SetTexCoordGen(GX_TEXCOORD0 + i, tcg.tgen_type, tcg.tgen_src, tcg.mtrx_src);

		++i;
	}

	GX_SetNumTexGens(i);
	}

	// bind textures
	ApplyTextures(resources);

	{
	for (unsigned int i = 0; i != ind_srts.size() && i != MAX_IND_MATRICES; ++i)
	{
		const auto& srt = ind_srts[i];

		const float rotate_rad = srt.rotate * (3.14159265358979323846f / 180.0f);
		const float cosF = std::cos(rotate_rad);
		const float sinF = std::sin(rotate_rad);

		float mtx23[2][3];
		mtx23[0][0] = srt.scale_s * cosF;
		mtx23[0][1] = srt.scale_t * -sinF;
		mtx23[0][2] = srt.translate_s;

		mtx23[1][0] = srt.scale_s * sinF;
		mtx23[1][1] = srt.scale_t * cosF;
		mtx23[1][2] = srt.translate_t;

		float mtxabs23[2][3];
		for (int n = 0; n < 2; ++n)
			for (int m = 0; m < 3; ++m)
				mtxabs23[n][m] = std::fabs(mtx23[n][m]);

		int scale_exp = 0;

		auto any_ge_one = [&]()
		{
			for (auto & n : mtxabs23)
				for (float m : n)
					if (m >= 1.0f)
						return true;
			return false;
		};
		auto all_lt_half = [&]()
		{
			for (auto & n : mtxabs23)
				for (float m : n)
					if (m >= 0.5f)
						return false;
			return true;
		};

		if (any_ge_one())
		{
			while (scale_exp < 0x2E && any_ge_one())
			{
				for (int n = 0; n < 2; ++n)
					for (int m = 0; m < 3; ++m)
					{
						mtx23[n][m] *= 0.5f;
						mtxabs23[n][m] *= 0.5f;
					}
				++scale_exp;
			}
		}
		else if (all_lt_half())
		{
			while (scale_exp > -0x11 && all_lt_half())
			{
				for (int n = 0; n < 2; ++n)
					for (int m = 0; m < 3; ++m)
					{
						mtx23[n][m] *= 2.0f;
						mtxabs23[n][m] *= 2.0f;
					}
				--scale_exp;
			}
		}
		GX_SetIndTexMatrix(GX_ITM_0 + i, mtx23, (int8_t)scale_exp);
	}

	for (unsigned int i = 0; i != ind_stages.size() && i != MAX_IND_STAGES; ++i)
	{
		const auto& stage = ind_stages[i];
		GX_SetIndTexOrder(i, stage.tex_coord, stage.tex_map);
		GX_SetIndTexCoordScale(i, stage.scale_s, stage.scale_t);
	}

	GX_SetNumIndStages((uint8_t)ind_stages.size());
	}

	// tev stages
	{
	for (unsigned int i = 0; i != 4; ++i)
		GX_SetTevSwapModeTable(i, tev_swap_table[i].r, tev_swap_table[i].g,
			tev_swap_table[i].b, tev_swap_table[i].a);

	int i = 0;
	for (auto& ts : tev_stages)
	{
		GX_SetTevOrder(i, ts.tex_coord, ts.tex_map, ts.color);
		GX_SetTevSwapMode(i, ts.ras_sel, ts.tex_sel);

		GX_SetTevColorIn(i, ts.color_in.a, ts.color_in.b, ts.color_in.c, ts.color_in.d);
		GX_SetTevColorOp(i, ts.color_in.op, ts.color_in.bias, ts.color_in.scale, ts.color_in.clamp, ts.color_in.reg_id);
		GX_SetTevKColorSel(i, ts.color_in.constant_sel);

		GX_SetTevAlphaIn(i, ts.alpha_in.a, ts.alpha_in.b, ts.alpha_in.c, ts.alpha_in.d);
		GX_SetTevAlphaOp(i, ts.alpha_in.op, ts.alpha_in.bias, ts.alpha_in.scale, ts.alpha_in.clamp, ts.alpha_in.reg_id);
		GX_SetTevKAlphaSel(i, ts.alpha_in.constant_sel);

		GX_SetTevIndirect(i, ts.ind.tex_id, ts.ind.format, ts.ind.bias, ts.ind.mtx,
			ts.ind.wrap_s, ts.ind.wrap_t, ts.ind.add_prev, ts.ind.utc_lod, ts.ind.alpha);

		++i;
	}

	// enable correct number of tev stages
	GX_SetNumTevStages(i);
	}

	GX_SetChanMatColor(GX_COLOR0A0, color);
}

void Material::DebugPrintSrts() const
{
	logger.printf("[trace]     material '%s': %zu texture srt(s)\n",
		GetName().c_str(), texture_srts.size());

	for (size_t i = 0; i != texture_srts.size(); ++i)
	{
		const auto& srt = texture_srts[i];
		logger.printf("[trace]       srt[%zu] translate=(%.4f,%.4f) rotate=%.2f "
			"scale=(%.4f,%.4f)\n", i, srt.translate_s, srt.translate_t,
			srt.rotate, srt.scale_s, srt.scale_t);
	}

	for (size_t i = 0; i != texture_coord_gens.size(); ++i)
	{
		const auto& tcg = texture_coord_gens[i];
		logger.printf("[trace]       texgen[%zu] type=%d src=%d mtx=%d\n", i,
			static_cast<int>(tcg.tgen_type), static_cast<int>(tcg.tgen_src),
			static_cast<int>(tcg.mtrx_src));
	}
}

void Material::ProcessHermiteKey(const KeyType& type, float value) {
	if (type.type == ANIMATION_TYPE_TEXTURE_SRT) {
		if (type.target < 5 && type.index < texture_srts.size()) {
			auto& srt = texture_srts[type.index];

			float* const values[] = {
				&srt.translate_s,
				&srt.translate_t,

				&srt.rotate,

				&srt.scale_s,
				&srt.scale_t,
			};

			*values[type.target] = value;
		}
	} else if (type.type == ANIMATION_TYPE_IND_MATERIAL) {
		if (type.target < 5 && type.index < ind_srts.size()) {
			auto& srt = ind_srts[type.index];

			float* const values[] = {
				&srt.translate_s,
				&srt.translate_t,

				&srt.rotate,

				&srt.scale_s,
				&srt.scale_t,
			};

			logger << "IndSrt[" << type.index << "] target=" << type.target
		   << " -> S:" << srt.translate_s
		   << " T:" << srt.translate_t
		   << " R:" << srt.rotate
		   << " Sx:" << srt.scale_s
		   << " Sy:" << srt.scale_t << "\n";

			*values[type.target] = value;
		}
	} else if (type.type == ANIMATION_TYPE_MATERIAL_COLOR) {
		if (type.target < 4) {
			(&color.r)[type.target] = ClampToByte(value);
		} else if (type.target < 0x10) {
			// the colour registers are signed 10 bit values
			(&color_regs->r)[type.target - 4] = static_cast<int16_t>(
				std::clamp(value, -1024.f, 1023.f));
		} else if (type.target < 0x20) {
			(&color_constants->r)[type.target - 0x10] = ClampToByte(value);
		}
	} else {
		Base::ProcessHermiteKey(type, value);
	}
}

void Material::ProcessStepKey(const KeyType& type, StepKeyHandler::KeyData data)
{
	if (type.type == ANIMATION_TYPE_TEXTURE_PALETTE)	// tpl palette
	{
		if(type.index < MAX_TEX_MAP)
		{
			palette_texture[type.index] = data.data2;
			return;
		}
	}

	Base::ProcessStepKey(type, data);
}

void Material::DebugDescribe(std::ostream& os, const Resources& resources, int indent) const
{
	using gxdebug::Print;
	namespace gdn = gxdebug::names;

	const int in = indent;

	Print(os, "%*smaterial '%s'\n", in, "", GetName().c_str());
	Print(os, "%*sflags=0x%08X: texture_maps=%u texture_srts=%u texgens=%u tev_stages=%u ind_stages=%u ind_srts=%u\n"
		"%*s       tev_swap_table=%u alpha_compare=%u blend_mode=%u channel_control=%u material_color=%u\n",
		in + 2, "", static_cast<unsigned>(flags.value),
		static_cast<unsigned>(flags.texture_map), static_cast<unsigned>(flags.texture_srt),
		static_cast<unsigned>(flags.texture_coord_gen), static_cast<unsigned>(flags.tev_stage),
		static_cast<unsigned>(flags.ind_stage), static_cast<unsigned>(flags.ind_srt),
		in + 2, "", static_cast<unsigned>(flags.tev_swap_table), static_cast<unsigned>(flags.alpha_compare),
		static_cast<unsigned>(flags.blend_mode), static_cast<unsigned>(flags.channel_control),
		static_cast<unsigned>(flags.material_color));

	Print(os, "%*smaterial color %s%s, channel control: colour from %s, alpha from %s%s\n", in + 2, "",
		gdn::Color(color.r, color.g, color.b, color.a).c_str(), flags.material_color ? "" : " (default)",
		gdn::ChannelSource(chan_ctrl.color_mat_src).c_str(), gdn::ChannelSource(chan_ctrl.alpha_mat_src).c_str(),
		flags.channel_control ? "" : " (default)");

	for (unsigned i = 0; i != 3; ++i)
		Print(os, "%*stev colour register REG%u (s10): (%d,%d,%d,%d)\n", in + 2, "", i,
			static_cast<int>(color_regs[i].r), static_cast<int>(color_regs[i].g),
			static_cast<int>(color_regs[i].b), static_cast<int>(color_regs[i].a));

	for (unsigned i = 0; i != 4; ++i)
		Print(os, "%*skonst colour K%u: %s\n", in + 2, "", i,
			gdn::Color(color_constants[i].r, color_constants[i].g, color_constants[i].b, color_constants[i].a).c_str());

	for (size_t i = 0; i != texture_maps.size(); ++i)
	{
		const TextureMap& map = texture_maps[i];

		std::string texture_name;
		if (map.tex_index < resources.textures.size())
			texture_name = "'" + resources.textures[map.tex_index]->GetName() + "'";
		else
			texture_name = "!! OUT OF RANGE (" + std::to_string(resources.textures.size()) + " textures)";

		Print(os, "%*stexture map %zu: texture #%u %s wrap=%s/%s", in + 2, "", i,
			static_cast<unsigned>(map.tex_index), texture_name.c_str(),
			gdn::Wrap(map.wrap_s).c_str(), gdn::Wrap(map.wrap_t).c_str());

		if (i < MAX_TEX_MAP && palette_texture[i] != PALETTE_DEFAULT)
			Print(os, " palette override=%u", static_cast<unsigned>(palette_texture[i]));

		Print(os, "\n");
	}

	for (size_t i = 0; i != texture_srts.size(); ++i)
	{
		const TextureSrt& srt = texture_srts[i];

		Print(os, "%*stexture srt %zu: translate=(%g,%g) rotate=%g scale=(%g,%g)\n", in + 2, "", i,
			static_cast<double>(srt.translate_s), static_cast<double>(srt.translate_t),
			static_cast<double>(srt.rotate), static_cast<double>(srt.scale_s), static_cast<double>(srt.scale_t));
	}

	for (size_t i = 0; i != texture_coord_gens.size(); ++i)
	{
		const TextureCoordGen& tcg = texture_coord_gens[i];

		Print(os, "%*stexgen %zu: type=%s src=%s mtx=%s\n", in + 2, "", i,
			gdn::TexGenType(tcg.tgen_type).c_str(), gdn::TexGenSrc(tcg.tgen_src).c_str(),
			gdn::TexMtx(tcg.mtrx_src).c_str());
	}

	for (unsigned i = 0; i != 4; ++i)
		Print(os, "%*stev swap table %u: %s%s\n", in + 2, "", i,
			gdn::SwapTable(static_cast<uint8_t>(tev_swap_table[i].value)).c_str(),
			flags.tev_swap_table ? "" : " (default)");

	for (size_t i = 0; i != ind_srts.size(); ++i)
	{
		const IndSrt& srt = ind_srts[i];

		Print(os, "%*sindirect srt %zu: translate=(%g,%g) rotate=%g scale=(%g,%g)\n", in + 2, "", i,
			static_cast<double>(srt.translate_s), static_cast<double>(srt.translate_t),
			static_cast<double>(srt.rotate), static_cast<double>(srt.scale_s), static_cast<double>(srt.scale_t));
	}

	for (size_t i = 0; i != ind_stages.size(); ++i)
	{
		const IndStage& stage = ind_stages[i];

		Print(os, "%*sindirect stage %zu: coord=%s map=%s scale=%s/%s\n", in + 2, "", i,
			gdn::TexCoord(stage.tex_coord).c_str(), gdn::TexMap(stage.tex_map).c_str(),
			gdn::IndScale(stage.scale_s).c_str(), gdn::IndScale(stage.scale_t).c_str());
	}

	Print(os, "%*stev stages (%zu)%s:\n", in + 2, "", tev_stages.size(),
		flags.tev_stage ? "" : ", none stored so the defaults for this many texture maps were generated");

	for (size_t i = 0; i != tev_stages.size(); ++i)
	{
		const TevStage& ts = tev_stages[i];

		Print(os, "%*s[%zu] coord=%s map=%s chan=%s swap(ras=table%u tex=table%u)\n", in + 4, "", i,
			gdn::TexCoord(ts.tex_coord).c_str(), gdn::TexMap(ts.tex_map == 0x1FF ? 0xFF : ts.tex_map).c_str(),
			gdn::TevOrderChannel(ts.color).c_str(), static_cast<unsigned>(ts.ras_sel), static_cast<unsigned>(ts.tex_sel));

		Print(os, "%*s    color: a=%s b=%s c=%s d=%s op=%s bias=%s scale=%s clamp=%u -> %s\n", in + 4, "",
			gdn::TevColorArg(ts.color_in.a).c_str(), gdn::TevColorArg(ts.color_in.b).c_str(),
			gdn::TevColorArg(ts.color_in.c).c_str(), gdn::TevColorArg(ts.color_in.d).c_str(),
			gdn::TevOp(ts.color_in.op, false).c_str(), gdn::TevBias(ts.color_in.bias).c_str(),
			gdn::TevScale(ts.color_in.scale).c_str(), static_cast<unsigned>(ts.color_in.clamp),
			gdn::TevReg(ts.color_in.reg_id).c_str());

		Print(os, "%*s    alpha: a=%s b=%s c=%s d=%s op=%s bias=%s scale=%s clamp=%u -> %s\n", in + 4, "",
			gdn::TevAlphaArg(ts.alpha_in.a).c_str(), gdn::TevAlphaArg(ts.alpha_in.b).c_str(),
			gdn::TevAlphaArg(ts.alpha_in.c).c_str(), gdn::TevAlphaArg(ts.alpha_in.d).c_str(),
			gdn::TevOp(ts.alpha_in.op, true).c_str(), gdn::TevBias(ts.alpha_in.bias).c_str(),
			gdn::TevScale(ts.alpha_in.scale).c_str(), static_cast<unsigned>(ts.alpha_in.clamp),
			gdn::TevReg(ts.alpha_in.reg_id).c_str());

		Print(os, "%*s    konst: color=%s alpha=%s\n", in + 4, "",
			gdn::TevKSel(ts.color_in.constant_sel).c_str(), gdn::TevKSel(ts.alpha_in.constant_sel).c_str());

		if (ts.ind.mtx != GX_ITM_OFF)
			Print(os, "%*s    indirect: indtex=%u fmt=%s bias=%s mtx=%s wrap=%s/%s addprev=%u alpha=%s\n", in + 4, "",
				static_cast<unsigned>(ts.ind.tex_id), gdn::IndFormat(ts.ind.format).c_str(),
				gdn::IndBias(ts.ind.bias).c_str(), gdn::IndMtx(ts.ind.mtx).c_str(),
				gdn::IndWrap(ts.ind.wrap_s).c_str(), gdn::IndWrap(ts.ind.wrap_t).c_str(),
				static_cast<unsigned>(ts.ind.add_prev), gdn::IndAlpha(ts.ind.alpha).c_str());

		const bool uses_texture = ts.color_in.a == 8 || ts.color_in.b == 8 || ts.color_in.c == 8 || ts.color_in.d == 8 ||
			ts.color_in.a == 9 || ts.color_in.b == 9 || ts.color_in.c == 9 || ts.color_in.d == 9 ||
			ts.alpha_in.a == 4 || ts.alpha_in.b == 4 || ts.alpha_in.c == 4 || ts.alpha_in.d == 4;

		if (uses_texture && (ts.tex_map == 0x1FF || ts.tex_map == 0xFF || ts.tex_map >= flags.texture_map))
			Print(os, "%*s    !! reads the texture colour but texture map %s is not one of this material's %u map(s)\n",
				in + 4, "", gdn::TexMap(ts.tex_map == 0x1FF ? 0xFF : ts.tex_map).c_str(),
				static_cast<unsigned>(flags.texture_map));
	}

	Print(os, "%*salpha compare: comp0=%s comp1=%s op=%s ref0=%u ref1=%u%s\n", in + 2, "",
		gdn::Compare(alpha_compare.function & 0xF).c_str(), gdn::Compare(alpha_compare.function >> 4).c_str(),
		gdn::AlphaOp(alpha_compare.op).c_str(), static_cast<unsigned>(alpha_compare.ref0),
		static_cast<unsigned>(alpha_compare.ref1), flags.alpha_compare ? "" : " (default)");

	Print(os, "%*sblend mode: type=%s src=%s dst=%s logic op=%s%s\n", in + 2, "",
		gdn::BlendType(blend_mode.type).c_str(), gdn::BlendFactor(blend_mode.src_factor, true).c_str(),
		gdn::BlendFactor(blend_mode.dst_factor, false).c_str(), gdn::LogicOp(blend_mode.logical_op).c_str(),
		flags.blend_mode ? "" : " (default)");

	DebugDescribeKeys(os, in + 2);
}

std::string Material::DebugState() const
{
	using gxdebug::Format;
	namespace gdn = gxdebug::names;

	std::string state = Format("color=%s", gdn::Color(color.r, color.g, color.b, color.a).c_str());

	for (unsigned i = 0; i != 3; ++i)
		state += Format(" reg%u=(%d,%d,%d,%d)", i, static_cast<int>(color_regs[i].r), static_cast<int>(color_regs[i].g),
			static_cast<int>(color_regs[i].b), static_cast<int>(color_regs[i].a));

	for (unsigned i = 0; i != 4; ++i)
		state += Format(" konst%u=%s", i, gdn::Color(color_constants[i].r, color_constants[i].g, color_constants[i].b,
			color_constants[i].a).c_str());

	for (size_t i = 0; i != texture_srts.size(); ++i)
		state += Format(" srt%zu=(t=%g,%g;r=%g;s=%g,%g)", i, static_cast<double>(texture_srts[i].translate_s),
			static_cast<double>(texture_srts[i].translate_t), static_cast<double>(texture_srts[i].rotate),
			static_cast<double>(texture_srts[i].scale_s), static_cast<double>(texture_srts[i].scale_t));

	for (size_t i = 0; i != ind_srts.size(); ++i)
		state += Format(" indsrt%zu=(t=%g,%g;r=%g;s=%g,%g)", i, static_cast<double>(ind_srts[i].translate_s),
			static_cast<double>(ind_srts[i].translate_t), static_cast<double>(ind_srts[i].rotate),
			static_cast<double>(ind_srts[i].scale_s), static_cast<double>(ind_srts[i].scale_t));

	for (unsigned i = 0; i != MAX_TEX_MAP; ++i)
		if (palette_texture[i] != PALETTE_DEFAULT)
			state += Format(" palette%u=%u", i, static_cast<unsigned>(palette_texture[i]));

	return state;
}

}
