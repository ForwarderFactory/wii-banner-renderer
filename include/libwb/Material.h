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

#pragma once

#include <libwb/Animator.h>
#include <libwb/Texture.h>
#include <libwb/Funcs.h>
#include <libwb/WrapGx.h>

namespace WiiBanner
{

class Resources; // forward declaration, Layout.h

class Material : public Animator {
public:
	typedef Animator Base;

	enum
	{
		NAME_LENGTH = 20
	};

	static constexpr uint32_t MAX_TEX_MAP = 8;
	static constexpr uint32_t MAX_TEX_SRT = 10;
	static constexpr uint32_t MAX_TEX_GEN = 8;
	static constexpr uint32_t MAX_IND_STAGES = 4;
	static constexpr uint32_t MAX_IND_MATRICES = 3;
	static constexpr uint32_t MAX_TEV_STAGES = 16;

	static constexpr uint8_t PALETTE_DEFAULT = 0xFF;

	void Load(std::istream& file);
	Material() {
		for(unsigned char & i : palette_texture)
			i = PALETTE_DEFAULT;
	}
	void Apply(const Resources& resources) const;
	void ApplyTextures(const Resources& resources) const;

	const GXColor& GetColor() const { return color; }

	uint8_t GetColorMatSrc() const { return chan_ctrl.color_mat_src; }
	uint8_t GetAlphaMatSrc() const { return chan_ctrl.alpha_mat_src; }

	GXColor GetChannelColor(const GXColor& vertex_color, uint8_t render_alpha) const
	{
		const GXColor& rgb_source =
			(chan_ctrl.color_mat_src == GX_SRC_VTX) ? vertex_color : color;

		const uint8_t alpha_source =
			(chan_ctrl.alpha_mat_src == GX_SRC_VTX) ? vertex_color.a : color.a;

		return GXColor{
			rgb_source.r,
			rgb_source.g,
			rgb_source.b,
			MultiplyColors(alpha_source, render_alpha)
		};
	}

protected:
	void ProcessHermiteKey(const KeyType& type, float value) override;
	void ProcessStepKey(const KeyType& type, StepKeyHandler::KeyData data) override;

private:
	union
	{
		uint32_t value;

		struct
		{
			uint32_t texture_map : 4;
			uint32_t texture_srt : 4;
			uint32_t texture_coord_gen : 4;
			uint32_t tev_swap_table : 1;
			uint32_t ind_srt : 2;
			uint32_t ind_stage : 3;
			uint32_t tev_stage : 5;
			uint32_t alpha_compare : 1;
			uint32_t blend_mode : 1;
			uint32_t channel_control : 1;
			uint32_t pad : 1;
			uint32_t material_color : 1;
			uint32_t pad2 : 4;
		};

	} flags{};

	uint8_t palette_texture[MAX_TEX_MAP]{};

	struct TextureMap
	{
		uint16_t tex_index;
		uint8_t wrap_s, wrap_t;
	};
	std::vector<TextureMap> texture_maps;

	struct TextureCoordGen
	{
		uint8_t tgen_type, tgen_src, mtrx_src;
	};
	std::vector<TextureCoordGen> texture_coord_gens;

	struct TextureSrt
	{
		float translate_s{0.0f};
		float translate_t{0.0f};
		float scale_s{1.0f};
		float scale_t{1.0f};
		float rotate{0.0};
	};
	std::vector<TextureSrt> texture_srts;

	struct
	{
		uint8_t type, src_factor, dst_factor, logical_op;

	} blend_mode{};

	struct
	{
		uint8_t function, op, ref0, ref1;

	} alpha_compare{};

	union {
		uint8_t value;

		struct {
			uint8_t r : 2;
			uint8_t g : 2;
			uint8_t b : 2;
			uint8_t a : 2;
		};

	} tev_swap_table[4]{};

	struct IndSrt {
		float translate_s;
		float translate_t;
		float scale_s;
		float scale_t;
		float rotate;
	};

	std::vector<IndSrt> ind_srts;

	struct IndStage {
		uint8_t tex_coord, tex_map, scale_s, scale_t;
	};

	std::vector<IndStage> ind_stages;

	union TevStage
	{
		char data[0x10];

		struct
		{
			uint8_t tex_coord;
			uint8_t color;

			uint16_t tex_map : 9;
			uint16_t ras_sel : 2;
			uint16_t tex_sel : 2;
			uint16_t empty1 : 3;

			struct
			{
				uint8_t a : 4;
				uint8_t b : 4;

				uint8_t c : 4;
				uint8_t d : 4;

				uint8_t op : 4;
				uint8_t bias: 2;
				uint8_t scale : 2;

				uint8_t clamp : 1;
				uint8_t reg_id : 2;
				uint8_t constant_sel : 5;

			} color_in, alpha_in;

			struct
			{
				uint8_t tex_id : 2;
				uint8_t empty1 : 6;

				uint8_t bias : 3;
				uint8_t mtx : 4;
				uint8_t empty2 : 1;

				uint8_t wrap_s : 3;
				uint8_t wrap_t : 3;
				uint8_t empty3 : 2;

				uint8_t format : 2;
				uint8_t add_prev : 1;
				uint8_t utc_lod : 1;
				uint8_t alpha : 2;
				uint8_t empty4 : 2;

			} ind;
		};
	};
	std::vector<TevStage> tev_stages;

	GXColor color{};	// material register color, used when a channel's matsrc is GX_SRC_REG

	struct
	{
		uint8_t color_mat_src = GX_SRC_VTX;
		uint8_t alpha_mat_src = GX_SRC_VTX;

	} chan_ctrl{};

	GXColorS10 color_regs[3]{};
	GXColor color_constants[4]{};
};

class MaterialList : public std::vector<Material*>
{
public:
	static constexpr uint32_t BINARY_MAGIC = MAKE_FOURCC('m', 'a', 't', '1');
};

}