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

#ifdef _WIN32
#define GLEW_STATIC
#include <windows.h>
#endif
#include <GL/glew.h>
//#include <GL/glu.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <sstream>

// from dolphin
#include "TextureDecoder.h"

#include "../include/libwb/WrapGx.h"

#include "../include/libwb/Funcs.h"

static u8 g_texture_decode_buffer[1024 * 1024 * 4];

static float g_color_registers[3][4];
static float g_kcolor_registers[4][4];
static u8 g_tev_swap_tables[4] = { 0xe4, 0xc0, 0xd5, 0xea };

struct IndTexOrder
{
	u8 tex_coord = 0xFF;
	u8 tex_map = 0xFF;
};

struct IndTexCoordScale
{
	u8 scale_s = GX_ITS_1;
	u8 scale_t = GX_ITS_1;
};

struct IndTexMtx
{
	f32 m[2][3] = {};
	s8 scale_exp = 0;
	bool set = false;
};

static IndTexOrder g_ind_tex_order[4];
static IndTexCoordScale g_ind_tex_coord_scale[4];
static IndTexMtx g_ind_tex_mtx[12];
static u8 g_num_ind_stages = 0;

// TODO: make this 0, currently causes issues though, figure that out :p
static constexpr GLuint g_texmap_start_index = 1;

//static GLuint g_clip_texture;

// silly
GXFifoObj * 	GX_Init (void *base, u32 size)
{
	glewInit();

	//glGenTextures(1, &g_clip_texture);

	//glActiveTexture(GL_TEXTURE0);
	//glBindTexture(GL_TEXTURE_2D, g_clip_texture);
	//u8 pixels[1] = { 0xff };
	//glTexImage2D(GL_TEXTURE_2D, g_clip_texture, GL_ALPHA, 1, 1, 0, GL_ALPHA, GL_UNSIGNED_BYTE, pixels);

	//TexDecoder_SetTexFmtOverlayOptions(true, false);

	return nullptr;
}

//void 	GX_SetViewport (f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ)
//{
//	const GLenum target = GL_TEXTURE0;
//	glBegin(GL_QUADS);
//	//glMultiTexCoord2f(target, xOrig, yOrig);
//	glEnd();
//}

struct TlutObj
{
	void* lut;
	u16 entries;
	u8 fmt;
};

std::map<u32, TlutObj> g_tlut_names;

struct GLTexObj
{
	void* img_ptr;

	mutable GLuint tex; // ugly

	u16 wd, ht;
	u8 fmt;
	u32 tlut_name;
	u8 wrap_s, wrap_t;
	u8 minfilt, magfilt;

	u8 mipmap = 0;
	u8 max_lod = 0;
	f32 min_lod = 0.0f;
	f32 lod_bias = 0.0f;
	u8 bias_clamp = 0;
	u8 edge_lod = 0;

	GLTexObj() : img_ptr(nullptr), tex(0), wd(0), ht(0), fmt(0), tlut_name(0), wrap_s(0), wrap_t(0), minfilt(0),
	             magfilt(0) {
	}

	~GLTexObj()
	{
		glDeleteTextures(1, &tex);
	}

	bool operator<(const GLTexObj& rhs) const
	{
		// this is probably good enough
		return img_ptr < rhs.img_ptr;
	}

	void Bind() const
	{
		if (tex)
			glBindTexture(GL_TEXTURE_2D, tex);
		else
		{
			glGenTextures(1, &tex);
			glBindTexture(GL_TEXTURE_2D, tex);

			// copy palette data
			const auto& tlut = g_tlut_names[tlut_name];
			if (tlut.lut)
				memcpy(texMem, tlut.lut, tlut.entries * 2);

			GLenum gl_format = 0, gl_iformat = 0, gl_type = 0;

			const u32 num_levels = mipmap ? (u32(max_lod) + 1) : 1;

			u32 level_width = wd;
			u32 level_height = ht;
			const u8* level_src = reinterpret_cast<const u8*>(img_ptr);

			for (u32 level = 0; level < num_levels; ++level)
			{
				const u32 expanded_width  = RoundUp(level_width, TexDecoder_GetBlockWidthInTexels(fmt));
				const u32 expanded_height = RoundUp(level_height, TexDecoder_GetBlockHeightInTexels(fmt));

				// decode this level
				auto const pcfmt = TexDecoder_Decode(g_texture_decode_buffer,
					level_src, expanded_width, expanded_height, fmt, 0, tlut.fmt);

				switch (pcfmt)
				{
				default:
				case PC_TEX_FMT_NONE:
					std::cout << "Error decoding texture!!!\n";

				case PC_TEX_FMT_BGRA32:
					gl_format = GL_BGRA;
					gl_iformat = 4;
					gl_type = GL_UNSIGNED_BYTE;
					break;

				case PC_TEX_FMT_RGBA32:
					gl_format = GL_RGBA;
					gl_iformat = 4;
					gl_type = GL_UNSIGNED_BYTE;
					break;

				case PC_TEX_FMT_I4_AS_I8:
					gl_format = GL_LUMINANCE;
					gl_iformat = GL_INTENSITY4;
					gl_type = GL_UNSIGNED_BYTE;
					break;

				case PC_TEX_FMT_IA4_AS_IA8:
					gl_format = GL_LUMINANCE_ALPHA;
					gl_iformat = GL_LUMINANCE4_ALPHA4;
					gl_type = GL_UNSIGNED_BYTE;
					break;

				case PC_TEX_FMT_I8:
					gl_format = GL_LUMINANCE;
					gl_iformat = GL_INTENSITY8;
					gl_type = GL_UNSIGNED_BYTE;
					break;

				case PC_TEX_FMT_IA8:
					gl_format = GL_LUMINANCE_ALPHA;
					gl_iformat = GL_LUMINANCE8_ALPHA8;
					gl_type = GL_UNSIGNED_BYTE;
					break;

				case PC_TEX_FMT_RGB565:
					gl_format = GL_RGB;
					gl_iformat = GL_RGB;
					gl_type = GL_UNSIGNED_SHORT_5_6_5;
					break;
				}

				if (expanded_width != level_width)
					glPixelStorei(GL_UNPACK_ROW_LENGTH, expanded_width);

				glTexImage2D(GL_TEXTURE_2D, level, gl_iformat, level_width, level_height, 0, gl_format, gl_type, g_texture_decode_buffer);

				if (expanded_width != level_width)
					glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

				level_src += TexDecoder_GetTextureSizeInBytes(expanded_width, expanded_height, fmt);
				level_width  = std::max<u32>(1, level_width  >> 1);
				level_height = std::max<u32>(1, level_height >> 1);
			}

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, GLint(num_levels - 1));
		}
	}
};

std::set<GLTexObj> g_texture_cache;

u32 	GX_GetTexBufferSize (u16 wd, u16 ht, u32 fmt, u8 mipmap, u8 maxlod)
{
	const u32 num_levels = mipmap ? (u32(maxlod) + 1) : 1;

	u32 total = 0;
	u32 level_w = wd;
	u32 level_h = ht;

	for (u32 level = 0; level < num_levels; ++level)
	{
		total += TexDecoder_GetTextureSizeInBytes(
			RoundUp(level_w, TexDecoder_GetBlockWidthInTexels(fmt)),
			RoundUp(level_h, TexDecoder_GetBlockHeightInTexels(fmt)), fmt);

		level_w = std::max<u32>(1, level_w >> 1);
		level_h = std::max<u32>(1, level_h >> 1);
	}

	return total;
}

void 	GX_InitTexObjTlut (GXTexObj *obj, u32 tlut_name)
{
	GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	txobj.tlut_name = tlut_name;
}

void 	GX_InitTlutObj (GXTlutObj *obj, void *lut, u8 fmt, u16 entries)
{
	TlutObj& tlutobj = *reinterpret_cast<TlutObj*>(obj);

	tlutobj.lut = lut;
	tlutobj.fmt = fmt;
	tlutobj.entries = entries;
}

void 	GX_LoadTlut (GXTlutObj *obj, u32 tlut_name)
{
	TlutObj& tlutobj = *reinterpret_cast<TlutObj*>(obj);

	g_tlut_names[tlut_name] = tlutobj;
}

void 	GX_InitTexObj (GXTexObj *obj, void *img_ptr, u16 wd, u16 ht, u8 fmt, u8 wrap_s, u8 wrap_t, u8 mipmap)
{
	GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	txobj.img_ptr = img_ptr;
	txobj.wd = wd;
	txobj.ht = ht;
	txobj.fmt = fmt;
	txobj.wrap_s = wrap_s;
	txobj.wrap_t = wrap_t;
	txobj.mipmap = mipmap;

	// hax, invalidate cache entry
	g_texture_cache.erase(txobj);

	// generate texture
	//glGenTextures(1, &txobj.tex);
	//glActiveTexture(GL_TEXTURE0);
	//glBindTexture(GL_TEXTURE_2D, txobj.tex);

	// TODO: ?
	//edge_lod
	//wrap_s		// these 2 are handled by the materials values
	//wrap_t

	//GX_InitTexObjWrapMode(obj, wrap_s, wrap_t);
}

void 	GX_InitTexObjWrapMode (GXTexObj *obj, u8 wrap_s, u8 wrap_t)
{
	GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	txobj.wrap_s = wrap_s;
	txobj.wrap_t = wrap_t;
}

void 	GX_InitTexObjFilterMode (GXTexObj *obj, u8 minfilt, u8 magfilt)
{
	GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	txobj.minfilt = minfilt;
	txobj.magfilt = magfilt;
}

void 	GX_InitTexObjLOD (GXTexObj *obj, u8 minfilt, u8 magfilt, f32 minlod, f32 maxlod, f32 lodbias, u8 biasclamp, u8 edgelod, u8 maxaniso)
{
	GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	txobj.minfilt = minfilt;
	txobj.magfilt = magfilt;
	txobj.min_lod = minlod;
	txobj.max_lod = u8(maxlod);
	txobj.lod_bias = lodbias;
	txobj.bias_clamp = biasclamp;
	txobj.edge_lod = edgelod;
}

void 	GX_SetBlendMode (u8 type, u8 src_fact, u8 dst_fact, u8 op)
{
	static const GLenum blend_types[] =
	{
		0,	// none
		GL_FUNC_ADD,
		GL_FUNC_REVERSE_SUBTRACT,	// LOGIC??
		GL_FUNC_SUBTRACT,
	};

	if (type)
	{
		glEnable(GL_BLEND);
		glBlendEquation(blend_types[type & 0x3]);
	}
	else
	{
		glDisable(GL_BLEND);
	}

	// In GX the factor values 2 and 3 mean different things depending on which
	// side they are used on: SRCCLR/INVSRCCLR as the destination factor, but
	// DSTCLR/INVDSTCLR as the source factor.
	static const GLenum src_blend_factors[] =
	{
		GL_ZERO,
		GL_ONE,
		GL_DST_COLOR,
		GL_ONE_MINUS_DST_COLOR,
		GL_SRC_ALPHA,
		GL_ONE_MINUS_SRC_ALPHA,
		GL_DST_ALPHA,
		GL_ONE_MINUS_DST_ALPHA,
	};

	static const GLenum dst_blend_factors[] =
	{
		GL_ZERO,
		GL_ONE,
		GL_SRC_COLOR,
		GL_ONE_MINUS_SRC_COLOR,
		GL_SRC_ALPHA,
		GL_ONE_MINUS_SRC_ALPHA,
		GL_DST_ALPHA,
		GL_ONE_MINUS_DST_ALPHA,
	};

	glBlendFunc(src_blend_factors[src_fact & 0x7], dst_blend_factors[dst_fact & 0x7]);

	static const GLenum logic_ops[] =
	{
		GL_CLEAR,
		GL_AND,
		GL_AND_REVERSE,
		GL_COPY,
		GL_AND_INVERTED,
		GL_NOOP,
		GL_XOR,
		GL_OR,
		GL_NOR,
		GL_EQUIV,
		GL_INVERT,
		GL_OR_REVERSE,
		GL_COPY_INVERTED,
		GL_OR_INVERTED,
		GL_NAND,
		GL_SET,
	};

	glLogicOp(logic_ops[op & 0xf]);
}

// TODO: incomplete
void 	GX_SetAlphaCompare (u8 comp0, u8 ref0, u8 aop, u8 comp1, u8 ref1)
{
	static const GLenum alpha_funcs[] =
	{
		// GX order: NEVER, LESS, EQUAL, LEQUAL, GREATER, NEQUAL, GEQUAL, ALWAYS
		GL_NEVER,
		GL_LESS,
		GL_EQUAL,
		GL_LEQUAL,
		GL_GREATER,
		GL_NOTEQUAL,
		GL_GEQUAL,
		GL_ALWAYS,
	};

	glAlphaFunc(alpha_funcs[comp0 & 0x7], (float)ref0 / 255.f);
	//glAlphaFunc(alpha_funcs[comp1 & 0x7], (float)ref1 / 255.f);


	//glLogicOp();	// TODO: need to do this guy, but for alpha
}

struct TevStageProps
{
	TevStageProps()
	{
		memset(this, 0, sizeof(*this));
	}

	// color inputs
	u8 color_a : 4;
	u8 color_b : 4;

	u8 color_c : 4;
	u8 color_d : 4;

	// alpha inputs
	u8 alpha_a : 4;
	u8 alpha_b : 4;

	u8 alpha_c : 4;
	u8 alpha_d : 4;

	// tevops
	u8 color_op : 4;
	u8 alpha_op : 4;

	// operations and outputs
	u8 color_bias;
	u8 color_scale;
	u8 color_clamp;
	u8 color_regid;
	u8 alpha_bias;
	u8 alpha_scale;
	u8 alpha_clamp;
	u8 alpha_regid;
	u8 kcolor_sel;
	u8 kalpha_sel;
	u8 ras_swap;
	u8 tex_swap;

	u8 texcoord;

	u8 texmap;

	u8 ind_texid : 2;
	u8 ind_format : 2;
	u8 ind_bias : 3;
	u8 ind_addprev : 1;

	u8 ind_mtxid : 4;
	u8 ind_alpha : 2;
	u8 ind_pad : 2;

	u8 ind_wrap_s;
	u8 ind_wrap_t;

	bool operator<(const TevStageProps& rhs) const
	{
		return memcmp(this, &rhs, sizeof(*this)) < 0;
	}
};

typedef std::vector<TevStageProps> TevStages;

// one entry per active GX_TEXCOORDn slot, as set by GX_SetTexCoordGen
struct TexGenProps
{
	u8 tgen_typ;
	u8 tgen_src;
	u8 mtxsrc;

	bool operator<(const TexGenProps& rhs) const
	{
		return memcmp(this, &rhs, sizeof(*this)) < 0;
	}
};

typedef std::vector<TexGenProps> TexGens;

struct IndStageProps
{
	u8 tex_coord = 0xFF;
	u8 tex_map = 0xFF;
	u8 scale_s = GX_ITS_1;
	u8 scale_t = GX_ITS_1;

	bool operator<(const IndStageProps& rhs) const
	{
		return memcmp(this, &rhs, sizeof(*this)) < 0;
	}
};

typedef std::vector<IndStageProps> IndStages;

struct ShaderKey
{
	TevStages tev;
	TexGens texgens;
	IndStages ind_stages;

	bool operator<(const ShaderKey& rhs) const
	{
		if (tev < rhs.tev) return true;
		if (rhs.tev < tev) return false;
		if (texgens < rhs.texgens) return true;
		if (rhs.texgens < texgens) return false;
		return ind_stages < rhs.ind_stages;
	}
};

struct CompiledTevStages
{
	CompiledTevStages()
		: program(0)
		, fragment_shader(0)
		, vertex_shader(0)
	{}

	void Enable();
	void Compile(const TevStages& stages, const TexGens& texgens, const IndStages& ind_stages);

	GLuint program, fragment_shader, vertex_shader;
};

std::map<ShaderKey, CompiledTevStages> g_compiled_tev_stages;

TevStages g_active_stages;
TexGens g_active_texgens;

void CompiledTevStages::Enable()
{
	glUseProgram(program);

	// TODO: cache value of GetUniformLocation
	glUniform4fv(glGetUniformLocation(program, "registers"), 3, g_color_registers[0]);
	glUniform4fv(glGetUniformLocation(program, "kcolors"), 4, g_kcolor_registers[0]);

	float row0[12 * 3];
	float row1[12 * 3];
	for (unsigned int i = 0; i != 12; ++i)
	{
		const IndTexMtx& m = g_ind_tex_mtx[i];
		const float scale = m.set ? std::ldexp(1.0f, m.scale_exp) : 0.0f;

		row0[i * 3 + 0] = m.m[0][0] * scale;
		row0[i * 3 + 1] = m.m[0][1] * scale;
		row0[i * 3 + 2] = m.m[0][2] * scale;

		row1[i * 3 + 0] = m.m[1][0] * scale;
		row1[i * 3 + 1] = m.m[1][1] * scale;
		row1[i * 3 + 2] = m.m[1][2] * scale;
	}
	glUniform3fv(glGetUniformLocation(program, "ind_mtx_r0"), 12, row0);
	glUniform3fv(glGetUniformLocation(program, "ind_mtx_r1"), 12, row1);
}

static std::string GlslFloat(float v)
{
	std::ostringstream ss;
	ss << v;
	std::string s = ss.str();
	if (s.find_first_of(".eEnN") == std::string::npos)
		s += ".0";
	return s;
}

void CompiledTevStages::Compile(const TevStages& stages, const TexGens& texgens, const IndStages& ind_stages)
{
	// w.e good for now
	static const unsigned int sampler_count = 8;

	// generate vertex/fragment shader code
	std::ostringstream vert_ss;
	{

	vert_ss << "void main(){";

	vert_ss << "gl_FrontColor = gl_Color;";
	vert_ss << "gl_BackColor = gl_Color;";

	for (unsigned int i = 0; i != sampler_count; ++i)
	{
		if (i >= texgens.size())
		{
			vert_ss << "gl_TexCoord[" << i << "] = gl_TextureMatrix[" << i << "] * gl_MultiTexCoord" << i << ";";
			continue;
		}

		const TexGenProps& tg = texgens[i];

		std::string src_vec;

		if (tg.tgen_src >= GX_TG_TEX0 && tg.tgen_src <= GX_TG_TEX7)
		{
			const unsigned int src_index = tg.tgen_src - GX_TG_TEX0;
			src_vec = "vec4(gl_MultiTexCoord" + std::to_string(src_index) + ".st, 0.0, 1.0)";
		}
		else if (tg.tgen_src == GX_TG_POS)
		{
			src_vec = "vec4(gl_Vertex.xy, 0.0, 1.0)";
		}
		else
		{
			std::cout << "GX_SetTexCoordGen: unsupported tgen_src " << (int)tg.tgen_src
				<< " on texcoord " << i << ", falling back to gl_MultiTexCoord" << i << "\n";
			src_vec = "vec4(gl_MultiTexCoord" + std::to_string(i) + ".st, 0.0, 1.0)";
		}

		std::string mtx_expr;

		if (tg.mtxsrc == GX_IDENTITY)
		{
			mtx_expr = "mat4(1.0)";
		}
		else if (tg.mtxsrc >= GX_TEXMTX0 && tg.mtxsrc <= GX_TEXMTX9 && (tg.mtxsrc - GX_TEXMTX0) % 3 == 0)
		{
			const unsigned int mtx_index = (tg.mtxsrc - GX_TEXMTX0) / 3;
			mtx_expr = "gl_TextureMatrix[" + std::to_string(mtx_index) + "]";
		}
		else
		{
			std::cout << "GX_SetTexCoordGen: unrecognized mtxsrc " << (int)tg.mtxsrc
				<< " on texcoord " << i << ", using identity\n";
			mtx_expr = "mat4(1.0)";
		}

		if (tg.tgen_typ != GX_TG_MTX2x4 && tg.tgen_typ != GX_TG_MTX3x4)
			std::cout << "GX_SetTexCoordGen: unsupported tgen_typ " << (int)tg.tgen_typ
				<< " on texcoord " << i << ", treating as GX_TG_MTX2x4\n";

		vert_ss << "gl_TexCoord[" << i << "] = " << mtx_expr << " * " << src_vec << ";";
	}

	vert_ss << "gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;";

	vert_ss << '}';

	vertex_shader = glCreateShader(GL_VERTEX_SHADER);

	{
	const auto& vert_src_str = vert_ss.str();
	const GLchar* vert_src = vert_src_str.c_str();
	glShaderSource(vertex_shader, 1, &vert_src, nullptr);
	}

	}	// done generating vertex shader

	glCompileShader(vertex_shader);

	// generate fragment shader code
	std::ostringstream frag_ss;
	frag_ss << "#extension GL_ARB_shader_texture_lod : enable\n";
	{

	// uniforms
	for (unsigned int i = 0; i != sampler_count; ++i)
		frag_ss << "uniform sampler2D textures" << i << ';';
	frag_ss << "uniform vec4 registers[3]" ";";
	frag_ss << "uniform vec4 kcolors[4]" ";";
	frag_ss << "uniform vec3 ind_mtx_r0[12]" ";";
	frag_ss << "uniform vec3 ind_mtx_r1[12]" ";";

	frag_ss << "void main(){";

	frag_ss << "vec4 color_previous = vec4(0.0)" ";";
	frag_ss << "vec4 color_texture = vec4(0.0)" ";";
	frag_ss << "vec4 color_constant = vec4(0.0)" ";";
	frag_ss << "vec4 color_raster = vec4(0.0)" ";";
	frag_ss << "vec2 ind_offset = vec2(0.0)" ";";
	for (unsigned int i = 0; i != 3; ++i)
		frag_ss << "vec4 color_registers" << i << " = registers[" << i << "]" ";";

	static const char* const color_inputs[] =
	{
		"color_previous" ".rgb",
		"color_previous" ".aaa",
		"color_registers" "0" ".rgb",
		"color_registers" "0" ".aaa",
		"color_registers" "1" ".rgb",
		"color_registers" "1" ".aaa",
		"color_registers" "2" ".rgb",
		"color_registers" "2" ".aaa",
		"color_texture" ".rgb",
		"color_texture" ".aaa",
		"color_raster" ".rgb",
		"color_raster" ".aaa",
		"vec3(1.0)",
		"vec3(0.5)",
		"color_constant" ".rgb",
		"vec3(0.0)",
	};

	static const char* const alpha_inputs[] =
	{
		"color_previous" ".a",
		"color_registers" "0" ".a",
		"color_registers" "1" ".a",
		"color_registers" "2" ".a",
		"color_texture" ".a",
		"color_raster" ".a",
		"color_constant" ".a",
		"0.0",
	};

	static const char* const output_registers[] =
	{
		"color_previous",
		"color_registers" "0",
		"color_registers" "1",
		"color_registers" "2",
	};

	frag_ss << "const vec3 comp16 = vec3(1.0, 255.0, 0.0), comp24 = vec3(1.0, 255.0, 255.0 * 255.0);";

	for (auto& stage : stages)
	{
		const bool stage_has_ind = (stage.ind_mtxid >= GX_ITM_0 && stage.ind_mtxid <= GX_ITM_2)
			&& (stage.ind_texid < ind_stages.size());

		if (stage.ind_mtxid != GX_ITM_OFF && !stage_has_ind)
		{
			std::cout << "Material: unsupported indirect matrix id " << (int)stage.ind_mtxid
				<< " (or indirect stage " << (int)stage.ind_texid << " not configured)"
				<< ", disabling indirect for this stage\n";
		}

		if (stage_has_ind)
		{
			const IndStageProps& ind = ind_stages[stage.ind_texid];
			const unsigned int mtx_index = stage.ind_mtxid - GX_ITM_0;

			frag_ss << '{';

			frag_ss << "vec3 ind_tex = vec3(0.0);";
			if (ind.tex_map < sampler_count && ind.tex_coord < sampler_count)
			{
				static const float scale_divisors[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256 };
				const float div_s = scale_divisors[ind.scale_s < 9 ? ind.scale_s : 0];
				const float div_t = scale_divisors[ind.scale_t < 9 ? ind.scale_t : 0];

				frag_ss << "ind_tex = texture2D(textures" << (int)ind.tex_map
					<< ", gl_TexCoord[" << (int)ind.tex_coord << "].xy / vec2("
					<< GlslFloat(div_s) << ',' << GlslFloat(div_t) << ")).rgb;";
			}

			static const float format_bases[] = { 256.0f, 32.0f, 16.0f, 8.0f };
			const float base = format_bases[stage.ind_format];
			const float bias_amount = base * 0.5f;

			frag_ss << "vec3 ind_raw = mod(ind_tex * 255.0, " << GlslFloat(base) << ");";

			if (stage.ind_bias & 1) frag_ss << "ind_raw.r -= " << GlslFloat(bias_amount) << ";";
			if (stage.ind_bias & 2) frag_ss << "ind_raw.g -= " << GlslFloat(bias_amount) << ";";
			if (stage.ind_bias & 4) frag_ss << "ind_raw.b -= " << GlslFloat(bias_amount) << ";";

			frag_ss << "vec2 stage_ind_offset = vec2("
				"dot(ind_mtx_r0[" << mtx_index << "], vec3(ind_raw.rg, 1.0)),"
				"dot(ind_mtx_r1[" << mtx_index << "], vec3(ind_raw.rg, 1.0)))"
				" / 255.0;";

			if (stage.ind_addprev)
				frag_ss << "stage_ind_offset += ind_offset;";

			frag_ss << "ind_offset = stage_ind_offset;";

			if (stage.ind_wrap_s != GX_ITW_OFF || stage.ind_wrap_t != GX_ITW_OFF)
			{
				std::cout << "Material: indirect wrap modes (" << (int)stage.ind_wrap_s
					<< ", " << (int)stage.ind_wrap_t << ") aren't emulated, ignoring\n";
			}

			if (stage.ind_alpha != GX_ITBA_OFF)
			{
				static const char* const bump_components[] = { "", "r", "g", "b" };
				frag_ss << "color_raster.a = ind_tex." << bump_components[stage.ind_alpha] << ";";
			}

			frag_ss << '}';
		}

		// current texture color
		// 0xff is a common value for a disabled texture
		frag_ss << "color_texture = vec4(0.0);";
		if (stage.texmap < sampler_count && stage.texcoord < sampler_count)
		{
			if (stage_has_ind)
			{
				frag_ss << "{"
					"vec2 base_coord = gl_TexCoord[" << (int)stage.texcoord << "].xy;"
					"color_texture = texture2DGradARB(textures" << (int)stage.texmap
					<< ", base_coord + ind_offset, dFdx(base_coord), dFdy(base_coord));"
					"}";
			}
			else
			{
				frag_ss << "color_texture = texture2D(textures" << (int)stage.texmap
					<< ", gl_TexCoord[" << (int)stage.texcoord << "].xy);";
			}
		}

		static const char components[] = { 'r', 'g', 'b', 'a' };
		auto const write_swizzle = [&](u8 swap)
		{
			for (unsigned int i = 0; i != 4; ++i)
				frag_ss << components[(swap >> (i * 2)) & 3];
		};

		frag_ss << "color_texture = color_texture.";
		write_swizzle(stage.tex_swap);
		frag_ss << ";color_raster = gl_Color.";
		write_swizzle(stage.ras_swap);
		frag_ss << ';';

		static const char* const konst_fractions[] =
		{
			"1.0", "0.874509804", "0.749019608", "0.623529412",
			"0.501960784", "0.376470588", "0.250980392", "0.125490196",
		};

		if (stage.kcolor_sel < 8)
			frag_ss << "color_constant.rgb = vec3(" << konst_fractions[stage.kcolor_sel] << ");";
		else if (stage.kcolor_sel < 12)
			frag_ss << "color_constant.rgb = vec3(0.0);";
		else if (stage.kcolor_sel < 16)
			frag_ss << "color_constant.rgb = kcolors[" << (int)(stage.kcolor_sel - 12) << "].rgb;";
		else
		{
			static const char* const swizzles[] = { "rrr", "ggg", "bbb", "aaa" };
			const u8 selector = stage.kcolor_sel - 16;
			frag_ss << "color_constant.rgb = kcolors[" << (int)(selector & 3) << "]."
				<< swizzles[selector >> 2] << ";";
		}

		if (stage.kalpha_sel < 8)
			frag_ss << "color_constant.a = " << konst_fractions[stage.kalpha_sel] << ";";
		else if (stage.kalpha_sel < 16)
			frag_ss << "color_constant.a = 0.0;";
		else
		{
			static const char components[] = { 'r', 'g', 'b', 'a' };
			const u8 selector = stage.kalpha_sel - 16;
			frag_ss << "color_constant.a = kcolors[" << (int)(selector & 3) << "]."
				<< components[selector >> 2] << ";";
		}

		frag_ss << '{';

		// all 4 inputs
		frag_ss << "vec4 a = vec4("
			<< color_inputs[stage.color_a] << ','
			<< alpha_inputs[stage.alpha_a] << ");";

		frag_ss << "vec4 b = vec4("
			<< color_inputs[stage.color_b] << ','
			<< alpha_inputs[stage.alpha_b] << ");";

		frag_ss << "vec4 c = vec4("
			<< color_inputs[stage.color_c] << ','
			<< alpha_inputs[stage.alpha_c] << ");";

		frag_ss << "vec4 d = vec4("
			<< color_inputs[stage.color_d] << ','
			<< alpha_inputs[stage.alpha_d] << ");";

		auto const write_tevop = [&](u8 tevop, u8 tevbias, u8 tevscale, u8 clamp, const char swiz[])
		{
			std::string condition_end(" ? c : vec4(0.0))");
			condition_end += swiz;

			const char* const compare_op = (tevop & 1) ? "==" : ">";

			switch (tevop)
			{
			case 0: // ADD
			case 1: // SUB
			{
				static const char* const biases[] = { "0.0", "0.5", "-0.5", "0.0" };
				static const char* const scales[] = { "1.0", "2.0", "4.0", "0.5" };
				const bool is_rgb = swiz[1] == 'r';
				const char* const clamp_min = is_rgb
					? (clamp ? "vec3(0.0)" : "vec3(-4.0)")
					: (clamp ? "0.0" : "-4.0");
				const char* const clamp_max = is_rgb
					? (clamp ? "vec3(1.0)" : "vec3(4.0)")
					: (clamp ? "1.0" : "4.0");

				frag_ss << "result" << swiz << " = clamp((d" << swiz
					<< ((1 == tevop) ? '-' : '+')
					<< "mix(a" << swiz << ", b" << swiz << ", c" << swiz << ") + "
					<< biases[tevbias & 3] << ") * " << scales[tevscale & 3]
					<< ", " << clamp_min << ", " << clamp_max << ")";
				break;
			}

			case 8: // COMP_R8_GT
			case 9: // COMP_R8_EQ
				frag_ss << "result" << swiz << " = d" << swiz << "+((a.r "
					<< compare_op << " b.r)" << condition_end;
				break;

			case 10: // COMP_GR16_GT
			case 11: // COMP_GR16_EQ
				frag_ss << "result" << swiz << " = d" << swiz
					<< "+((dot(a.rgb, comp16) " << compare_op << " dot(b.rgb, comp16))" << condition_end;
				break;

			case 12: // COMP_BGR24_GT
			case 13: // COMP_BGR24_EQ
				frag_ss << "result" << swiz << " = d" << swiz
					<< "+((dot(a.rgb, comp24) " << compare_op << " dot(b.rgb, comp24))" << condition_end;
				break;

			case 14: // COMP_RGB8_GT / COMP_A8_GT
			case 15: // COMP_RGB8_EQ / COMP_A8_EQ
			{
				const bool is_rgb = swiz[1] == 'r';

				if (is_rgb)
				{
					const char* cmp = (tevop == 14)
						? "greaterThan(a.rgb, b.rgb)"
						: "equal(a.rgb, b.rgb)";

					frag_ss << "result" << swiz
							<< " = d" << swiz
							<< "+((all(" << cmp << "))"
							<< condition_end;
				}
				else
				{
					const char* cmp = (tevop == 14)
						? "a.a > b.a"
						: "a.a == b.a";

					frag_ss << "result" << swiz
							<< " = d" << swiz
							<< "+((" << cmp << ")"
							<< condition_end;
				}

				break;
			}

			default:
				frag_ss << "result" << swiz << " = d" << swiz;
				std::cout << "Unsupported tevop!! " << (int)tevop << '\n';
				break;
			}

			frag_ss << ';';
		};

		// TODO: could eliminate this result variable
		frag_ss << "vec4 result;";

		write_tevop(stage.color_op, stage.color_bias, stage.color_scale, stage.color_clamp, ".rgb");
		write_tevop(stage.alpha_op, stage.alpha_bias, stage.alpha_scale, stage.alpha_clamp, ".a");

		// output register
		frag_ss << output_registers[stage.color_regid] << ".rgb = result" ".rgb;";
		frag_ss << output_registers[stage.alpha_regid] << ".a = result" ".a;";

		frag_ss << '}';
	}

	frag_ss << "gl_FragColor = color_previous;";

	frag_ss << '}';

	//std::cout << frag_ss.str() << '\n';

	// create/compile fragment shader
	fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);

	{
	const auto& frag_src_str = frag_ss.str();
	const GLchar* frag_src = frag_src_str.c_str();
	glShaderSource(fragment_shader, 1, &frag_src, nullptr);
	}

	}	// done generating fragment shader

	glCompileShader(fragment_shader);

	// check compile status of both shaders
	{

	GLint
		vert_compiled = false,
		frag_compiled = false;

	glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &vert_compiled);
	glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &frag_compiled);

	if (!vert_compiled) {
		std::cout << "Failed to compile vertex shader\n";
		std::cout << vert_ss.str() << "\n";
	}

	if (!frag_compiled)
	{
		std::cout << "Failed to compile fragment shader\n";
		std::cout << frag_ss.str() << "\n";
	}

	}

	// create program, attach shaders
	program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);

	// link program, check link status
	glLinkProgram(program);
	GLint link_status;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);

	if (!link_status)
		std::cout << "Failed to link program!\n";

	glUseProgram(program);

	// set uniforms
	for (unsigned int i = 0; i != sampler_count; ++i)
	{
		std::ostringstream ss;
		ss << "textures" << i;
		glUniform1i(glGetUniformLocation(program, ss.str().c_str()), g_texmap_start_index + i);
	}

	// print log
	{
	GLchar infolog[10240] = {};
	glGetProgramInfoLog(program, 10240, nullptr, infolog);
	std::cout << infolog;
	}

	// pause
	//std::cin.get();
}

void 	GX_LoadTexObj (GXTexObj *obj, u8 mapid)
{
	const GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	glActiveTexture(GL_TEXTURE0 + g_texmap_start_index + mapid);
	//glBindTexture(GL_TEXTURE_2D, txobj.tex);

	auto entry = g_texture_cache.find(txobj);

	if (entry == g_texture_cache.end())
	{
		g_texture_cache.insert(txobj);
		entry = g_texture_cache.find(txobj);
	}

	entry->Bind();

	// texture wrap
	static const GLenum wraps[] =
	{
		GL_CLAMP_TO_EDGE,
		GL_REPEAT,
		GL_MIRRORED_REPEAT,
		GL_REPEAT,
	};

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wraps[txobj.wrap_s & 0x3]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wraps[txobj.wrap_t & 0x3]);

	// texture filter
	const GLint filters[] =
	{
		GL_NEAREST,
		GL_LINEAR,
		GL_NEAREST_MIPMAP_NEAREST,
		GL_LINEAR_MIPMAP_NEAREST,
		GL_NEAREST_MIPMAP_LINEAR,
		GL_LINEAR_MIPMAP_LINEAR,
		GL_NEAREST,	// blah
		GL_NEAREST,
	};

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filters[txobj.minfilt & 0x7]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filters[txobj.magfilt & 0x7]);

	if (txobj.mipmap) {
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, txobj.min_lod);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, float(txobj.max_lod));
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, txobj.bias_clamp ? txobj.lod_bias : 0.0f);
	} else {
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, -1000.0f);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 1000.0f);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, 0.0f);
	}
}

inline void ActiveStage(u8 stage)
{
	g_active_stages.resize(std::max(g_active_stages.size(), (size_t)stage + 1));
}

void 	GX_SetTevOrder (u8 tevstage, u8 texcoord, u32 texmap, u8 color)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.texmap = texmap;
	ts.texcoord = texcoord;

	//glActiveTexture(GL_TEXTURE0 + g_texmap_start_index + tevstage);
}

void 	GX_SetTevSwapMode (u8 tevstage, u8 ras_sel, u8 tex_sel)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.ras_swap = g_tev_swap_tables[ras_sel & 3];
	ts.tex_swap = g_tev_swap_tables[tex_sel & 3];
}

void 	GX_SetTevSwapModeTable (u8 table, u8 r, u8 g, u8 b, u8 a)
{
	g_tev_swap_tables[table & 3] = (r & 3) | ((g & 3) << 2)
		| ((b & 3) << 4) | ((a & 3) << 6);
}

void 	GX_SetTevIndirect (u8 tevstage, u8 indtexid, u8 format, u8 bias, u8 mtxid,
	u8 wrap_s, u8 wrap_t, u8 addprev, u8 utclod, u8 a)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.ind_texid = indtexid & 0x3;
	ts.ind_format = format & 0x3;
	ts.ind_bias = bias & 0x7;
	ts.ind_mtxid = mtxid & 0xF;
	ts.ind_wrap_s = wrap_s;
	ts.ind_wrap_t = wrap_t;
	ts.ind_addprev = addprev ? 1 : 0;
	ts.ind_alpha = a & 0x3;

	(void)utclod;
}

void 	GX_SetIndTexOrder (u8 ind_stage, u8 tex_coord, u8 tex_map)
{
	if (ind_stage < 4)
	{
		g_ind_tex_order[ind_stage].tex_coord = tex_coord;
		g_ind_tex_order[ind_stage].tex_map = tex_map;
	}
}

void 	GX_SetIndTexCoordScale (u8 ind_stage, u8 scale_s, u8 scale_t)
{
	if (ind_stage < 4)
	{
		g_ind_tex_coord_scale[ind_stage].scale_s = scale_s;
		g_ind_tex_coord_scale[ind_stage].scale_t = scale_t;
	}
}

void GX_SetIndTexMatrix(u8 mtx_ind, Mtx23 offset_mtx, s8 scale_exp)
{
    if (mtx_ind >= GX_ITM_0 && mtx_ind <= GX_ITM_2)
    {
        const unsigned int index = mtx_ind - GX_ITM_0;

        IndTexMtx& m = g_ind_tex_mtx[index];

        memcpy(m.m, offset_mtx, sizeof(m.m));
        m.scale_exp = scale_exp;
        m.set = true;
    }
}

void 	GX_SetNumIndStages (u8 num_stages)
{
	g_num_ind_stages = num_stages;
}

void 	GX_SetTevColorS10 (u8 tev_regid, GXColorS10 color)
{
	for (unsigned int i = 0; i != 4; ++i)
		g_color_registers[tev_regid - 1][i] = (float)(&color.r)[i] / 255;
}

void 	GX_SetTevKColor (u8 tev_regid, GXColor color)
{
	for (unsigned int i = 0; i != 4; ++i)
		g_kcolor_registers[tev_regid][i] = (float)(&color.r)[i] / 255;
}

void 	GX_SetTevKAlphaSel (u8 tevstage, u8 sel)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.kalpha_sel = sel & 0x1f;
}

void 	GX_SetTevKColorSel (u8 tevstage, u8 sel)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.kcolor_sel = sel & 0x1f;
}

void 	GX_SetTevAlphaIn (u8 tevstage, u8 a, u8 b, u8 c, u8 d)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.alpha_a = a & 0x7;
	ts.alpha_b = b & 0x7;
	ts.alpha_c = c & 0x7;
	ts.alpha_d = d & 0x7;
}

void 	GX_SetTevAlphaOp (u8 tevstage, u8 tevop, u8 tevbias, u8 tevscale, u8 clamp, u8 tevregid)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.alpha_regid = tevregid;
	ts.alpha_op = tevop;
	ts.alpha_bias = tevbias;
	ts.alpha_scale = tevscale;
	ts.alpha_clamp = clamp;
}

void 	GX_SetTevColorIn (u8 tevstage, u8 a, u8 b, u8 c, u8 d)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.color_a = a & 0xf;
	ts.color_b = b & 0xf;
	ts.color_c = c & 0xf;
	ts.color_d = d & 0xf;
}

void 	GX_SetTevColorOp (u8 tevstage, u8 tevop, u8 tevbias, u8 tevscale, u8 clamp, u8 tevregid)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.color_regid = tevregid;
	ts.color_op = tevop;
	ts.color_bias = tevbias;
	ts.color_scale = tevscale;
	ts.color_clamp = clamp;
}

void 	GX_SetTexCoordGen (u8 texcoord, u8 tgen_typ, u8 tgen_src, u8 mtxsrc)
{
	if (texcoord >= g_active_texgens.size())
		g_active_texgens.resize(texcoord + 1);

	TexGenProps& tg = g_active_texgens[texcoord];
	tg.tgen_typ = tgen_typ;
	tg.tgen_src = tgen_src;
	tg.mtxsrc = mtxsrc;
}

void 	GX_SetNumTexGens (u8 num)
{
	g_active_texgens.resize(num);
}

void 	GX_SetNumTevStages (u8 num)
{
	g_active_stages.resize(num);

	IndStages ind_stages;
	for (unsigned int i = 0; i != g_num_ind_stages && i != 4; ++i)
	{
		IndStageProps isp;
		isp.tex_coord = g_ind_tex_order[i].tex_coord;
		isp.tex_map = g_ind_tex_order[i].tex_map;
		isp.scale_s = g_ind_tex_coord_scale[i].scale_s;
		isp.scale_t = g_ind_tex_coord_scale[i].scale_t;
		ind_stages.push_back(isp);
	}

	const ShaderKey key{ g_active_stages, g_active_texgens, ind_stages };
	CompiledTevStages& comptevs = g_compiled_tev_stages[key];

	// compile program if needed
	if (!comptevs.program)
		comptevs.Compile(g_active_stages, g_active_texgens, ind_stages);

	// enable the program
	comptevs.Enable();
}