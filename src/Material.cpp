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

#include <GL/glew.h>

#include <cmath>

#include <libwb/Material.h>
#include <libwb/Endian.h>
#include <libwb/Funcs.h>
#include <libwb/Layout.h>
#include <libwb/Texture.h>

namespace WiiBanner
{

void Material::Load(std::istream& file) {
	SetName(ReadFixedLengthString<NAME_LENGTH>(file));

	// read colors
	ReadBEArray(file, &color_regs->r, sizeof(color_regs) / sizeof(int16_t));
	ReadBEArray(file, &color_constants->r, sizeof(color_constants));

	file >> BE >> flags.value;

	//std::cout << "flags: " << flags.value << '\n';

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

		//std::cout << "TevSwapModeTable:\n";
		//for (unsigned int i = 0; i != 4; ++i)
		//	std::cout << "\t[" << i << "]: red: " << (int)tev_swap_mode_table[i].red
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

		//std::cout << "alpha compare:\t"
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

		//std::cout << "blend mode:\t"
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
				std::cerr
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
	// alpha compare
	GX_SetAlphaCompare(alpha_compare.function & 0xf, alpha_compare.ref0,
		alpha_compare.op, alpha_compare.function >> 4, alpha_compare.ref1);

	// blend mode
	GX_SetBlendMode(blend_mode.type, blend_mode.src_factor, blend_mode.dst_factor,
		blend_mode.logical_op);

	// tev reg colors
	for (unsigned int i = 0; i != 3; ++i)
		GX_SetTevColorS10(GX_TEVREG0 + i, color_regs[i]);

	// konst colors
	for (unsigned int i = 0; i != 4; ++i)
		GX_SetTevKColor(i, color_constants[i]);

	// texture coord gen
	glMatrixMode(GL_TEXTURE);
	{
	unsigned int i = 0;
	for (auto& tcg : texture_coord_gens)
	{
		GX_SetTexCoordGen(GX_TEXCOORD0 + i, tcg.tgen_type, tcg.tgen_src, tcg.mtrx_src);

		glActiveTexture(GL_TEXTURE0 + i);
		glLoadIdentity();

		if (tcg.tgen_type == GX_TG_MTX2x4 && tcg.mtrx_src != GX_IDENTITY)
		{
			const uint8_t mtrx = (tcg.mtrx_src - GX_TEXMTX0) / 3;

			if (mtrx < texture_srts.size())
			{
				const auto& srt = texture_srts[mtrx];

				glTranslatef(0.5f, 0.5f, 0.f);
				glRotatef(srt.rotate, 0.f, 0.f, 1.f);

				glScalef(srt.scale_s, srt.scale_t, 1.f);

				glTranslatef(srt.translate_s / srt.scale_s - 0.5f, srt.translate_t / srt.scale_t -0.5f, 0.f);
			}
		}

		++i;
	}

	GX_SetNumTexGens(i);

	// TODO: is this needed?
	for (; i != 8; ++i)
	{
		glActiveTexture(GL_TEXTURE0 + i);
		glLoadIdentity();
	}
	}
	glMatrixMode(GL_MODELVIEW);

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

	glColor4ubv(&color.r);
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

			std::cout << "IndSrt[" << type.index << "] target=" << type.target
		   << " -> S:" << srt.translate_s
		   << " T:" << srt.translate_t
		   << " R:" << srt.rotate
		   << " Sx:" << srt.scale_s
		   << " Sy:" << srt.scale_t << "\n";

			*values[type.target] = value;
		}
	} else if (type.type == ANIMATION_TYPE_MATERIAL_COLOR) {
		if (type.target < 4) {
			(&color.r)[type.target] = (uint8_t)value;
		} else if (type.target < 0x10) {
			(&color_regs->r)[type.target - 4] = static_cast<uint16_t>(value);
		} else if (type.target < 0x20) {
			(&color_constants->r)[type.target - 0x10] = (uint8_t)value;
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

}