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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstdlib>
#include <sstream>

#include "../include/libwb/WrapGx.h"

#include "logger_holder.hpp"
#include "../include/libwb/GXTexture.h"

#include "../include/libwb/Funcs.h"

static uint8_t g_texture_decode_buffer[1024 * 1024 * 4];

static float g_color_registers[3][4];
static float g_kcolor_registers[4][4];
static uint8_t g_tev_swap_tables[4] = { 0xe4, 0xc0, 0xd5, 0xea };

struct IndTexOrder
{
	uint8_t tex_coord = 0xFF;
	uint8_t tex_map = 0xFF;
};

struct IndTexCoordScale
{
	uint8_t scale_s = GX_ITS_1;
	uint8_t scale_t = GX_ITS_1;
};

struct IndTexMtx
{
	f32 m[2][3] = {};
	int8_t scale_exp = 0;
	bool set = false;
};

static IndTexOrder g_ind_tex_order[4];
static IndTexCoordScale g_ind_tex_coord_scale[4];
static IndTexMtx g_ind_tex_mtx[12];
static uint8_t g_num_ind_stages = 0;

static float g_texmap_dims[8][2];

// TODO: make this 0, currently causes issues though, figure that out :p
static constexpr GLuint g_texmap_start_index = 0;
// idk what these issues are im just gonna make this 0 (was 1)
// if we have some random weird issues that make no fucking sense try setting this to 1 :)

//static GLuint g_clip_texture;

// silly
GXFifoObj * 	GX_Init (void *base, uint32_t size)
{
	glewInit();

	//glGenTextures(1, &g_clip_texture);

	//glActiveTexture(GL_TEXTURE0);
	//glBindTexture(GL_TEXTURE_2D, g_clip_texture);
	//uint8_t pixels[1] = { 0xff };
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
	uint16_t entries;
	uint8_t fmt;
};

std::map<uint32_t, TlutObj> g_tlut_names;

struct GLTexObj
{
	void* img_ptr;

	mutable GLuint tex; // ugly

	uint16_t wd, ht;
	uint8_t fmt;
	uint32_t tlut_name;
	uint8_t wrap_s, wrap_t;
	uint8_t minfilt, magfilt;

	uint8_t mipmap = 0;
	uint8_t max_lod = 0;
	f32 min_lod = 0.0f;
	f32 lod_bias = 0.0f;
	uint8_t bias_clamp = 0;
	uint8_t edge_lod = 0;

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
		{
			glBindTexture(GL_TEXTURE_2D, tex);
			return;
		}

		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);

		// the palette, for the colour index formats
		const auto& tlut = g_tlut_names[tlut_name];

		wb::TexturePalette palette;
		palette.data = static_cast<const uint8_t*>(tlut.lut);
		palette.entries = tlut.entries;
		palette.format = tlut.fmt;

		const uint32_t num_levels = mipmap ? (uint32_t(max_lod) + 1) : 1;

		uint32_t level_width = wd;
		uint32_t level_height = ht;
		const uint8_t* level_src = reinterpret_cast<const uint8_t*>(img_ptr);

		// rows of the decoded levels are tightly packed, not padded to 4 bytes
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		for (uint32_t level = 0; level < num_levels; ++level)
		{
			// textures are stored in whole blocks, so the stored size is rounded up
			const uint32_t expanded_width  = wb::RoundUp(level_width, wb::GxBlockWidth(fmt));
			const uint32_t expanded_height = wb::RoundUp(level_height, wb::GxBlockHeight(fmt));

			wb::DecodedFormat decoded = wb::DecodedFormat::Invalid;

			if (expanded_width && expanded_height &&
				(size_t(expanded_width) * expanded_height * 4) <= sizeof(g_texture_decode_buffer))
			{
				decoded = wb::DecodeGxTexture(g_texture_decode_buffer, level_src,
					expanded_width, expanded_height, fmt, tlut.lut ? &palette : nullptr);
			}

			GLenum gl_format = GL_RGBA;
			GLenum gl_iformat = GL_RGBA8;

			switch (decoded)
			{
			case wb::DecodedFormat::Luminance8:
				// intensity formats also use their value as alpha
				gl_format = GL_LUMINANCE;
				gl_iformat = GL_INTENSITY8;
				break;

			case wb::DecodedFormat::LuminanceAlpha8:
				gl_format = GL_LUMINANCE_ALPHA;
				gl_iformat = GL_LUMINANCE8_ALPHA8;
				break;

			case wb::DecodedFormat::Rgba8:
				break;

			default:
				// not a texture we can decode: make it fully transparent
				wb::logger << "Error decoding texture!!! (format " << int(fmt) << ")\n";
				std::memset(g_texture_decode_buffer, 0,
					std::min<size_t>(sizeof(g_texture_decode_buffer), size_t(level_width) * level_height * 4));
				break;
			}

			// the decoded pixels are rows of expanded_width pixels, of which only
			// the first level_width are part of the texture
			const uint32_t row_length = decoded == wb::DecodedFormat::Invalid ? level_width : expanded_width;

			if (row_length != level_width)
				glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);

			glTexImage2D(GL_TEXTURE_2D, level, gl_iformat, level_width, level_height, 0, gl_format,
				GL_UNSIGNED_BYTE, g_texture_decode_buffer);

			if (row_length != level_width)
				glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

			level_src += wb::GxTextureSize(expanded_width, expanded_height, fmt);
			level_width  = std::max<uint32_t>(1, level_width  >> 1);
			level_height = std::max<uint32_t>(1, level_height >> 1);
		}

		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, GLint(num_levels - 1));
	}
};

std::set<GLTexObj> g_texture_cache;

uint32_t 	GX_GetTexBufferSize (uint16_t wd, uint16_t ht, uint32_t fmt, uint8_t mipmap, uint8_t maxlod)
{
	const uint32_t num_levels = mipmap ? (uint32_t(maxlod) + 1) : 1;

	uint32_t total = 0;
	uint32_t level_w = wd;
	uint32_t level_h = ht;

	for (uint32_t level = 0; level < num_levels; ++level)
	{
		total += wb::GxTextureSize(
			wb::RoundUp(level_w, wb::GxBlockWidth(fmt)),
			wb::RoundUp(level_h, wb::GxBlockHeight(fmt)), fmt);

		level_w = std::max<uint32_t>(1, level_w >> 1);
		level_h = std::max<uint32_t>(1, level_h >> 1);
	}

	return total;
}

void 	GX_InitTexObjTlut (GXTexObj *obj, uint32_t tlut_name)
{
	GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	txobj.tlut_name = tlut_name;
}

void 	GX_InitTlutObj (GXTlutObj *obj, void *lut, uint8_t fmt, uint16_t entries)
{
	TlutObj& tlutobj = *reinterpret_cast<TlutObj*>(obj);

	tlutobj.lut = lut;
	tlutobj.fmt = fmt;
	tlutobj.entries = entries;
}

void 	GX_LoadTlut (GXTlutObj *obj, uint32_t tlut_name)
{
	TlutObj& tlutobj = *reinterpret_cast<TlutObj*>(obj);

	g_tlut_names[tlut_name] = tlutobj;
}

void 	GX_InitTexObj (GXTexObj *obj, void *img_ptr, uint16_t wd, uint16_t ht, uint8_t fmt, uint8_t wrap_s, uint8_t wrap_t, uint8_t mipmap)
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

void 	GX_InitTexObjWrapMode (GXTexObj *obj, uint8_t wrap_s, uint8_t wrap_t)
{
	GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	txobj.wrap_s = wrap_s;
	txobj.wrap_t = wrap_t;
}

void 	GX_InitTexObjFilterMode (GXTexObj *obj, uint8_t minfilt, uint8_t magfilt)
{
	GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	txobj.minfilt = minfilt;
	txobj.magfilt = magfilt;
}

void 	GX_InitTexObjLOD (GXTexObj *obj, uint8_t minfilt, uint8_t magfilt, f32 minlod, f32 maxlod, f32 lodbias, uint8_t biasclamp, uint8_t edgelod, uint8_t maxaniso)
{
	GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	txobj.minfilt = minfilt;
	txobj.magfilt = magfilt;
	txobj.min_lod = minlod;
	txobj.max_lod = uint8_t(maxlod);
	txobj.lod_bias = lodbias;
	txobj.bias_clamp = biasclamp;
	txobj.edge_lod = edgelod;
}

void 	GX_SetBlendMode (uint8_t type, uint8_t src_fact, uint8_t dst_fact, uint8_t op)
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
void 	GX_SetAlphaCompare (uint8_t comp0, uint8_t ref0, uint8_t aop, uint8_t comp1, uint8_t ref1)
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
	uint8_t color_a : 4;
	uint8_t color_b : 4;

	uint8_t color_c : 4;
	uint8_t color_d : 4;

	// alpha inputs
	uint8_t alpha_a : 4;
	uint8_t alpha_b : 4;

	uint8_t alpha_c : 4;
	uint8_t alpha_d : 4;

	// tevops
	uint8_t color_op : 4;
	uint8_t alpha_op : 4;

	// operations and outputs
	uint8_t color_bias;
	uint8_t color_scale;
	uint8_t color_clamp;
	uint8_t color_regid;
	uint8_t alpha_bias;
	uint8_t alpha_scale;
	uint8_t alpha_clamp;
	uint8_t alpha_regid;
	uint8_t kcolor_sel;
	uint8_t kalpha_sel;
	uint8_t ras_swap;
	uint8_t tex_swap;

	uint8_t texcoord;

	uint8_t texmap;

	uint8_t ind_texid : 2;
	uint8_t ind_format : 2;
	uint8_t ind_bias : 3;
	uint8_t ind_addprev : 1;

	uint8_t ind_mtxid : 4;
	uint8_t ind_alpha : 2;
	uint8_t ind_pad : 2;

	uint8_t ind_wrap_s;
	uint8_t ind_wrap_t;

	bool operator<(const TevStageProps& rhs) const
	{
		return memcmp(this, &rhs, sizeof(*this)) < 0;
	}
};

typedef std::vector<TevStageProps> TevStages;

// one entry per active GX_TEXCOORDn slot, as set by GX_SetTexCoordGen
struct TexGenProps
{
	uint8_t tgen_typ;
	uint8_t tgen_src;
	uint8_t mtxsrc;

	bool operator<(const TexGenProps& rhs) const
	{
		return memcmp(this, &rhs, sizeof(*this)) < 0;
	}
};

typedef std::vector<TexGenProps> TexGens;

struct IndStageProps
{
	uint8_t tex_coord = 0xFF;
	uint8_t tex_map = 0xFF;
	uint8_t scale_s = GX_ITS_1;
	uint8_t scale_t = GX_ITS_1;

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
	glUniform2fv(glGetUniformLocation(program, "tex_dims"), 8, &g_texmap_dims[0][0]);
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
			wb::logger << "GX_SetTexCoordGen: unsupported tgen_src " << (int)tg.tgen_src
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
			wb::logger << "GX_SetTexCoordGen: unrecognized mtxsrc " << (int)tg.mtxsrc
				<< " on texcoord " << i << ", using identity\n";
			mtx_expr = "mat4(1.0)";
		}

		if (tg.tgen_typ != GX_TG_MTX2x4 && tg.tgen_typ != GX_TG_MTX3x4)
			wb::logger << "GX_SetTexCoordGen: unsupported tgen_typ " << (int)tg.tgen_typ
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
	frag_ss << "uniform vec2 tex_dims[8];";

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
			wb::logger << "Material: unsupported indirect matrix id " << (int)stage.ind_mtxid
				<< " (or indirect stage " << (int)stage.ind_texid << " not configured)"
				<< ", disabling indirect for this stage\n";
		}

		if (stage_has_ind) {
			const IndStageProps& ind = ind_stages[stage.ind_texid];
			const unsigned int mtx_index = stage.ind_mtxid - GX_ITM_0;

			frag_ss << '{';

			static const float scale_divisors[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256 };
			const float div_s = scale_divisors[ind.scale_s < 9 ? ind.scale_s : 0];
			const float div_t = scale_divisors[ind.scale_t < 9 ? ind.scale_t : 0];

			frag_ss << "vec4 ind_texel = vec4(0.0);";

			if (ind.tex_map < sampler_count && ind.tex_coord < sampler_count) {
				frag_ss << "ind_texel = texture2D(textures" << (int)ind.tex_map
					<< ", gl_TexCoord[" << (int)ind.tex_coord << "].xy / vec2("
					<< GlslFloat(div_s) << ',' << GlslFloat(div_t) << "));";
			}

			frag_ss << "vec3 ind_tex = ind_texel.abg;";

			static const float format_bases[] = { 256.0f, 32.0f, 16.0f, 8.0f };
			static const float bias_amounts[] = { -128.0f, 1.0f, 1.0f, 1.0f };

			const float base = format_bases[stage.ind_format & 3];
			const float bias_amount = bias_amounts[stage.ind_format & 3];

			frag_ss << "vec3 ind_raw = floor(mod(ind_tex * 255.0 + 0.5, " << GlslFloat(base) << "));";

			if (stage.ind_bias & 1) frag_ss << "ind_raw.x += " << GlslFloat(bias_amount) << ";";
			if (stage.ind_bias & 2) frag_ss << "ind_raw.y += " << GlslFloat(bias_amount) << ";";
			if (stage.ind_bias & 4) frag_ss << "ind_raw.z += " << GlslFloat(bias_amount) << ";";

			frag_ss << "vec2 stage_ind_offset = vec2("
				"dot(ind_mtx_r0[" << mtx_index << "], ind_raw),"
				"dot(ind_mtx_r1[" << mtx_index << "], ind_raw))";

			if (stage.texmap < 8)
				frag_ss << " / max(tex_dims[" << (int)stage.texmap << "], vec2(1.0))";

			frag_ss << ";";

			if (stage.ind_addprev)
				frag_ss << "stage_ind_offset += ind_offset;";

			frag_ss << "ind_offset = stage_ind_offset;";

			if (stage.ind_wrap_s != GX_ITW_OFF || stage.ind_wrap_t != GX_ITW_OFF)
			{
				wb::logger << "Material: indirect wrap modes (" << (int)stage.ind_wrap_s
					<< ", " << (int)stage.ind_wrap_t << ") aren't emulated, ignoring\n";
			}

			if (stage.ind_alpha != GX_ITBA_OFF)
			{
				// bump alpha comes from one of the S, T and U components
				static const char* const bump_components[] = { "", "x", "y", "z" };
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
		auto const write_swizzle = [&](uint8_t swap)
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
			const uint8_t selector = stage.kcolor_sel - 16;
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
			const uint8_t selector = stage.kalpha_sel - 16;
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

		auto const write_tevop = [&](uint8_t tevop, uint8_t tevbias, uint8_t tevscale, uint8_t clamp, const char swiz[])
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
				wb::logger << "Unsupported tevop!! " << (int)tevop << '\n';
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

	//wb::logger << frag_ss.str() << '\n';

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
		wb::logger << "Failed to compile vertex shader\n";
		wb::logger << vert_ss.str() << "\n";
	}

	if (!frag_compiled)
	{
		wb::logger << "Failed to compile fragment shader\n";
		wb::logger << frag_ss.str() << "\n";
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
		wb::logger << "Failed to link program!\n";

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
	wb::logger << infolog;
	}

	// pause
	//std::cin.get();
}

void 	GX_LoadTexObj (GXTexObj *obj, uint8_t mapid)
{
	const GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	if (mapid < 8)
	{
		g_texmap_dims[mapid][0] = static_cast<float>(txobj.wd);
		g_texmap_dims[mapid][1] = static_cast<float>(txobj.ht);
	}

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

inline void ActiveStage(uint8_t stage)
{
	g_active_stages.resize(std::max(g_active_stages.size(), (size_t)stage + 1));
}

void 	GX_SetTevOrder (uint8_t tevstage, uint8_t texcoord, uint32_t texmap, uint8_t color)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.texmap = texmap;
	ts.texcoord = texcoord;

	//glActiveTexture(GL_TEXTURE0 + g_texmap_start_index + tevstage);
}

void 	GX_SetTevSwapMode (uint8_t tevstage, uint8_t ras_sel, uint8_t tex_sel)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.ras_swap = g_tev_swap_tables[ras_sel & 3];
	ts.tex_swap = g_tev_swap_tables[tex_sel & 3];
}

void 	GX_SetTevSwapModeTable (uint8_t table, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	g_tev_swap_tables[table & 3] = (r & 3) | ((g & 3) << 2)
		| ((b & 3) << 4) | ((a & 3) << 6);
}

void 	GX_SetTevIndirect (uint8_t tevstage, uint8_t indtexid, uint8_t format, uint8_t bias, uint8_t mtxid,
	uint8_t wrap_s, uint8_t wrap_t, uint8_t addprev, uint8_t utclod, uint8_t a)
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

void 	GX_SetIndTexOrder (uint8_t ind_stage, uint8_t tex_coord, uint8_t tex_map)
{
	if (ind_stage < 4)
	{
		g_ind_tex_order[ind_stage].tex_coord = tex_coord;
		g_ind_tex_order[ind_stage].tex_map = tex_map;
	}
}

void 	GX_SetIndTexCoordScale (uint8_t ind_stage, uint8_t scale_s, uint8_t scale_t)
{
	if (ind_stage < 4)
	{
		g_ind_tex_coord_scale[ind_stage].scale_s = scale_s;
		g_ind_tex_coord_scale[ind_stage].scale_t = scale_t;
	}
}

void GX_SetIndTexMatrix(uint8_t mtx_ind, Mtx23 offset_mtx, int8_t scale_exp)
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

void 	GX_SetNumIndStages (uint8_t num_stages)
{
	g_num_ind_stages = num_stages;
}

void 	GX_SetTevColorS10 (uint8_t tev_regid, GXColorS10 color)
{
	for (unsigned int i = 0; i != 4; ++i)
		g_color_registers[tev_regid - 1][i] = (float)(&color.r)[i] / 255;
}

void 	GX_SetTevKColor (uint8_t tev_regid, GXColor color)
{
	for (unsigned int i = 0; i != 4; ++i)
		g_kcolor_registers[tev_regid][i] = (float)(&color.r)[i] / 255;
}

void 	GX_SetTevKAlphaSel (uint8_t tevstage, uint8_t sel)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.kalpha_sel = sel & 0x1f;
}

void 	GX_SetTevKColorSel (uint8_t tevstage, uint8_t sel)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.kcolor_sel = sel & 0x1f;
}

void 	GX_SetTevAlphaIn (uint8_t tevstage, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.alpha_a = a & 0x7;
	ts.alpha_b = b & 0x7;
	ts.alpha_c = c & 0x7;
	ts.alpha_d = d & 0x7;
}

void 	GX_SetTevAlphaOp (uint8_t tevstage, uint8_t tevop, uint8_t tevbias, uint8_t tevscale, uint8_t clamp, uint8_t tevregid)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.alpha_regid = tevregid;
	ts.alpha_op = tevop;
	ts.alpha_bias = tevbias;
	ts.alpha_scale = tevscale;
	ts.alpha_clamp = clamp;
}

void 	GX_SetTevColorIn (uint8_t tevstage, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.color_a = a & 0xf;
	ts.color_b = b & 0xf;
	ts.color_c = c & 0xf;
	ts.color_d = d & 0xf;
}

void 	GX_SetTevColorOp (uint8_t tevstage, uint8_t tevop, uint8_t tevbias, uint8_t tevscale, uint8_t clamp, uint8_t tevregid)
{
	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.color_regid = tevregid;
	ts.color_op = tevop;
	ts.color_bias = tevbias;
	ts.color_scale = tevscale;
	ts.color_clamp = clamp;
}

void 	GX_SetTexCoordGen (uint8_t texcoord, uint8_t tgen_typ, uint8_t tgen_src, uint8_t mtxsrc)
{
	if (texcoord >= g_active_texgens.size())
		g_active_texgens.resize(texcoord + 1);

	TexGenProps& tg = g_active_texgens[texcoord];
	tg.tgen_typ = tgen_typ;
	tg.tgen_src = tgen_src;
	tg.mtxsrc = mtxsrc;
}

void 	GX_SetNumTexGens (uint8_t num)
{
	g_active_texgens.resize(num);
}

void 	GX_SetNumTevStages (uint8_t num)
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
