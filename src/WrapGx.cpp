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
#include "../include/libwb/GxDebug.h"

namespace gdn = wb::gxdebug::names;

#define GXTRACE(...) \
	do { \
		if (wb::gxdebug::Tracing(wb::gxdebug::TRACE_GX)) \
			wb::gxdebug::TraceGx(__func__, __VA_ARGS__); \
	} while (0)

#define GXU(x) static_cast<unsigned>(x)

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
	GXTRACE("base=%p size=%u", base, GXU(size));

	if (!wb::gxdebug::Headless())
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
		if (tex)
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

static void DebugDecodeTexture(const GLTexObj& t, uint8_t mapid)
{
	const auto& tlut = g_tlut_names[t.tlut_name];

	static std::set<std::pair<const void*, const void*>> seen;
	if (!seen.insert({ t.img_ptr, tlut.lut }).second)
		return;

	wb::gxdebug::Stats& stats = wb::gxdebug::GetStats();

	if (!t.img_ptr)
	{
		++stats.texture_decode_failures;
		wb::gxdebug::Log("[tex] !! map %u: no image data (the texture failed to load, or was never loaded)", GXU(mapid));
		return;
	}

	wb::TexturePalette palette;
	palette.data = static_cast<const uint8_t*>(tlut.lut);
	palette.entries = tlut.entries;
	palette.format = tlut.fmt;

	const uint32_t num_levels = t.mipmap ? (uint32_t(t.max_lod) + 1) : 1;

	wb::gxdebug::Log("[tex] map %u: img=%p %ux%u fmt=%s mipmap=%u (%u level%s) wrap=%s/%s filter=%s/%s%s",
		GXU(mapid), t.img_ptr, GXU(t.wd), GXU(t.ht), gdn::TexFormat(t.fmt).c_str(), GXU(t.mipmap),
		num_levels, num_levels == 1 ? "" : "s",
		gdn::Wrap(t.wrap_s).c_str(), gdn::Wrap(t.wrap_t).c_str(),
		gdn::TexFilter(t.minfilt).c_str(), gdn::TexFilter(t.magfilt).c_str(),
		tlut.lut ? wb::gxdebug::Format(" tlut(name=%u entries=%u fmt=%s)", t.tlut_name, GXU(tlut.entries),
			gdn::TlutFormat(tlut.fmt).c_str()).c_str() : "");

	uint32_t level_width = t.wd;
	uint32_t level_height = t.ht;
	const uint8_t* level_src = reinterpret_cast<const uint8_t*>(t.img_ptr);

	for (uint32_t level = 0; level < num_levels; ++level)
	{
		const uint32_t expanded_width  = wb::RoundUp(level_width, wb::GxBlockWidth(t.fmt));
		const uint32_t expanded_height = wb::RoundUp(level_height, wb::GxBlockHeight(t.fmt));

		wb::DecodedFormat decoded = wb::DecodedFormat::Invalid;

		if (expanded_width && expanded_height &&
			(size_t(expanded_width) * expanded_height * 4) <= sizeof(g_texture_decode_buffer))
		{
			decoded = wb::DecodeGxTexture(g_texture_decode_buffer, level_src,
				expanded_width, expanded_height, t.fmt, tlut.lut ? &palette : nullptr);
		}

		if (decoded == wb::DecodedFormat::Invalid)
		{
			++stats.texture_decode_failures;
			wb::gxdebug::Log("[tex]   !! level %u: %ux%u (stored %ux%u) failed to decode (unsupported format, missing palette or too large)",
				level, level_width, level_height, expanded_width, expanded_height);
			return;
		}

		++stats.textures_decoded;

		const uint32_t bpp = wb::BytesPerPixel(decoded);
		const char* const decoded_name =
			decoded == wb::DecodedFormat::Luminance8 ? "L8 (alpha = intensity)" :
			decoded == wb::DecodedFormat::LuminanceAlpha8 ? "LA8" : "RGBA8";

		if (level == 0)
		{
			uint32_t hash = 2166136261u;
			uint32_t alpha_min = 255, alpha_max = 0;
			uint64_t opaque = 0, clear = 0;

			for (uint32_t y = 0; y != level_height; ++y)
			{
				const uint8_t* row = g_texture_decode_buffer + size_t(y) * expanded_width * bpp;

				for (uint32_t x = 0; x != level_width; ++x)
				{
					const uint8_t* pixel = row + size_t(x) * bpp;

					for (uint32_t b = 0; b != bpp; ++b)
						hash = (hash ^ pixel[b]) * 16777619u;

					const uint32_t alpha =
						decoded == wb::DecodedFormat::Rgba8 ? pixel[3] :
						decoded == wb::DecodedFormat::LuminanceAlpha8 ? pixel[1] : pixel[0];

					alpha_min = std::min(alpha_min, alpha);
					alpha_max = std::max(alpha_max, alpha);
					opaque += alpha == 255;
					clear += alpha == 0;
				}
			}

			const double pixels = double(level_width) * level_height;

			wb::gxdebug::Log("[tex]   level 0: %ux%u (stored %ux%u) -> %s, fnv1a=%08x alpha=%u..%u opaque=%.1f%% transparent=%.1f%%",
				level_width, level_height, expanded_width, expanded_height, decoded_name, hash,
				alpha_min, alpha_max, pixels ? 100.0 * double(opaque) / pixels : 0.0,
				pixels ? 100.0 * double(clear) / pixels : 0.0);
		}
		else
		{
			wb::gxdebug::Log("[tex]   level %u: %ux%u (stored %ux%u) -> %s", level, level_width, level_height,
				expanded_width, expanded_height, decoded_name);
		}

		level_src += wb::GxTextureSize(expanded_width, expanded_height, t.fmt);
		level_width  = std::max<uint32_t>(1, level_width  >> 1);
		level_height = std::max<uint32_t>(1, level_height >> 1);
	}
}

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

	GXTRACE("wd=%u ht=%u fmt=%s mipmap=%u maxlod=%u -> %u bytes", GXU(wd), GXU(ht), gdn::TexFormat(fmt).c_str(),
		GXU(mipmap), GXU(maxlod), GXU(total));

	return total;
}

void 	GX_InitTexObjTlut (GXTexObj *obj, uint32_t tlut_name)
{
	GXTRACE("obj=%p tlut_name=%u", (void*)obj, GXU(tlut_name));
	GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	txobj.tlut_name = tlut_name;
}

void 	GX_InitTlutObj (GXTlutObj *obj, void *lut, uint8_t fmt, uint16_t entries)
{
	GXTRACE("obj=%p lut=%p fmt=%s entries=%u", (void*)obj, lut, gdn::TlutFormat(fmt).c_str(), GXU(entries));
	TlutObj& tlutobj = *reinterpret_cast<TlutObj*>(obj);

	tlutobj.lut = lut;
	tlutobj.fmt = fmt;
	tlutobj.entries = entries;
}

void 	GX_LoadTlut (GXTlutObj *obj, uint32_t tlut_name)
{
	GXTRACE("obj=%p tlut_name=%u (entries=%u fmt=%s)", (void*)obj, GXU(tlut_name),
		GXU(reinterpret_cast<TlutObj*>(obj)->entries), gdn::TlutFormat(reinterpret_cast<TlutObj*>(obj)->fmt).c_str());
	TlutObj& tlutobj = *reinterpret_cast<TlutObj*>(obj);

	g_tlut_names[tlut_name] = tlutobj;
}

void 	GX_InitTexObj (GXTexObj *obj, void *img_ptr, uint16_t wd, uint16_t ht, uint8_t fmt, uint8_t wrap_s, uint8_t wrap_t, uint8_t mipmap)
{
	GXTRACE("obj=%p img=%p %ux%u fmt=%s wrap=%s/%s mipmap=%u", (void*)obj, img_ptr, GXU(wd), GXU(ht),
		gdn::TexFormat(fmt).c_str(), gdn::Wrap(wrap_s).c_str(), gdn::Wrap(wrap_t).c_str(), GXU(mipmap));
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
	GXTRACE("obj=%p wrap=%s/%s", (void*)obj, gdn::Wrap(wrap_s).c_str(), gdn::Wrap(wrap_t).c_str());
	GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	txobj.wrap_s = wrap_s;
	txobj.wrap_t = wrap_t;
}

void 	GX_InitTexObjFilterMode (GXTexObj *obj, uint8_t minfilt, uint8_t magfilt)
{
	GXTRACE("obj=%p min=%s mag=%s", (void*)obj, gdn::TexFilter(minfilt).c_str(), gdn::TexFilter(magfilt).c_str());
	GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	txobj.minfilt = minfilt;
	txobj.magfilt = magfilt;
}

void 	GX_InitTexObjLOD (GXTexObj *obj, uint8_t minfilt, uint8_t magfilt, f32 minlod, f32 maxlod, f32 lodbias, uint8_t biasclamp, uint8_t edgelod, uint8_t maxaniso)
{
	GXTRACE("obj=%p min=%s mag=%s minlod=%g maxlod=%g lodbias=%g biasclamp=%u edgelod=%u maxaniso=%u", (void*)obj,
		gdn::TexFilter(minfilt).c_str(), gdn::TexFilter(magfilt).c_str(), double(minlod), double(maxlod), double(lodbias),
		GXU(biasclamp), GXU(edgelod), GXU(maxaniso));
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
	GXTRACE("type=%s src=%s dst=%s op=%s", gdn::BlendType(type).c_str(), gdn::BlendFactor(src_fact, true).c_str(),
		gdn::BlendFactor(dst_fact, false).c_str(), gdn::LogicOp(op).c_str());

	if (wb::gxdebug::Headless())
		return;

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
	GXTRACE("comp0=%s ref0=%u aop=%s comp1=%s ref1=%u", gdn::Compare(comp0).c_str(), GXU(ref0),
		gdn::AlphaOp(aop).c_str(), gdn::Compare(comp1).c_str(), GXU(ref1));

	if (wb::gxdebug::Headless())
		return;

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

struct WGXDrawState
{
	f32 pos_mtx[10][16];
	f32 tex_mtx[10][16];

	uint32_t current_pos_mtx = GX_PNMTX0;

	bool tex_mtx_dirty = true;

	f32 projection[16];
	GXColor clear_color{ 0, 0, 0, 255 };

	bool pos = false;
	bool clr0 = false;
	bool tex[8] = {};

	unsigned tex_units[8] = {};
	unsigned tex_unit_count = 0;

	bool vertex_pending = false;
	bool vertex_3d = false;
	f32 x = 0, y = 0, z = 0;
	bool have_color = false;
	uint8_t color[4] = {};
	f32 texcoords[8][2] = {};
	unsigned texcoord_count = 0;

	WGXDrawState()
	{
		for (auto& m : pos_mtx)
			Identity(m);
		for (auto& m : tex_mtx)
			Identity(m);
		Identity(projection);
	}

	static void Identity(f32* m)
	{
		memset(m, 0, 16 * sizeof(f32));
		m[0] = m[5] = m[10] = m[15] = 1.f;
	}
};

static WGXDrawState g_draw;

static void DescribeShaderConfig(std::ostream& os, const TevStages& stages, const TexGens& texgens,
	const IndStages& ind_stages)
{
	using wb::gxdebug::Print;

	for (size_t i = 0; i != texgens.size(); ++i)
		Print(os, "  texgen[%zu]: type=%s src=%s mtx=%s\n", i, gdn::TexGenType(texgens[i].tgen_typ).c_str(),
			gdn::TexGenSrc(texgens[i].tgen_src).c_str(), gdn::TexMtx(texgens[i].mtxsrc).c_str());

	for (size_t i = 0; i != ind_stages.size(); ++i)
		Print(os, "  indstage[%zu]: coord=%s map=%s scale=%s/%s\n", i, gdn::TexCoord(ind_stages[i].tex_coord).c_str(),
			gdn::TexMap(ind_stages[i].tex_map).c_str(), gdn::IndScale(ind_stages[i].scale_s).c_str(),
			gdn::IndScale(ind_stages[i].scale_t).c_str());

	for (size_t i = 0; i != stages.size(); ++i)
	{
		const TevStageProps& ts = stages[i];

		Print(os, "  stage %zu: coord=%s map=%s swap(ras=%s tex=%s)\n", i, gdn::TexCoord(ts.texcoord).c_str(),
			gdn::TexMap(ts.texmap).c_str(), gdn::SwapTable(ts.ras_swap).c_str(), gdn::SwapTable(ts.tex_swap).c_str());

		Print(os, "    color: a=%s b=%s c=%s d=%s op=%s bias=%s scale=%s clamp=%u -> %s\n",
			gdn::TevColorArg(ts.color_a).c_str(), gdn::TevColorArg(ts.color_b).c_str(),
			gdn::TevColorArg(ts.color_c).c_str(), gdn::TevColorArg(ts.color_d).c_str(),
			gdn::TevOp(ts.color_op, false).c_str(), gdn::TevBias(ts.color_bias).c_str(),
			gdn::TevScale(ts.color_scale).c_str(), GXU(ts.color_clamp), gdn::TevReg(ts.color_regid).c_str());

		Print(os, "    alpha: a=%s b=%s c=%s d=%s op=%s bias=%s scale=%s clamp=%u -> %s\n",
			gdn::TevAlphaArg(ts.alpha_a).c_str(), gdn::TevAlphaArg(ts.alpha_b).c_str(),
			gdn::TevAlphaArg(ts.alpha_c).c_str(), gdn::TevAlphaArg(ts.alpha_d).c_str(),
			gdn::TevOp(ts.alpha_op, true).c_str(), gdn::TevBias(ts.alpha_bias).c_str(),
			gdn::TevScale(ts.alpha_scale).c_str(), GXU(ts.alpha_clamp), gdn::TevReg(ts.alpha_regid).c_str());

		Print(os, "    konst: color=%s alpha=%s\n", gdn::TevKSel(ts.kcolor_sel).c_str(), gdn::TevKSel(ts.kalpha_sel).c_str());

		if (ts.ind_mtxid != GX_ITM_OFF)
			Print(os, "    indirect: indtex=%u fmt=%s bias=%s mtx=%s wrap=%s/%s addprev=%u alpha=%s\n", GXU(ts.ind_texid),
				gdn::IndFormat(ts.ind_format).c_str(), gdn::IndBias(ts.ind_bias).c_str(), gdn::IndMtx(ts.ind_mtxid).c_str(),
				gdn::IndWrap(ts.ind_wrap_s).c_str(), gdn::IndWrap(ts.ind_wrap_t).c_str(), GXU(ts.ind_addprev),
				gdn::IndAlpha(ts.ind_alpha).c_str());
	}
}

static IndStages CurrentIndStages()
{
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

	return ind_stages;
}

static int g_debug_shader_id = 0;

static std::string PrettyGlsl(const std::string& source)
{
	std::string out;
	int depth = 0;
	int parens = 0;

	auto newline = [&]()
	{
		out.push_back('\n');
		out.append(size_t(depth) * 2, ' ');
	};

	for (const char c : source)
	{
		if (c == '(')
			++parens;
		else if (c == ')')
			--parens;

		if (c == '{')
		{
			out.push_back(c);
			++depth;
			newline();
		}
		else if (c == '}')
		{
			while (!out.empty() && out.back() == ' ')
				out.pop_back();
			if (!out.empty() && out.back() == '\n')
				out.pop_back();

			--depth;
			newline();
			out.push_back(c);
			newline();
		}
		else if (c == ';' && parens == 0)
		{
			out.push_back(c);
			newline();
		}
		else if (c == '\n')
		{
			newline();
		}
		else
		{
			out.push_back(c);
		}
	}

	while (!out.empty() && (out.back() == ' ' || out.back() == '\n'))
		out.pop_back();

	return out;
}

static void DebugReportShader(const std::string& vertex, const std::string& fragment)
{
	if (!wb::gxdebug::Tracing(wb::gxdebug::TRACE_GLSL))
		return;

	wb::gxdebug::Log("[glsl] shader #%d vertex shader:\n%s", g_debug_shader_id, PrettyGlsl(vertex).c_str());
	wb::gxdebug::Log("[glsl] shader #%d fragment shader:\n%s", g_debug_shader_id, PrettyGlsl(fragment).c_str());
}

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

	if (!wb::gxdebug::Headless())
	{
		vertex_shader = glCreateShader(GL_VERTEX_SHADER);

		const auto& vert_src_str = vert_ss.str();
		const GLchar* vert_src = vert_src_str.c_str();
		glShaderSource(vertex_shader, 1, &vert_src, nullptr);
	}

	}	// done generating vertex shader

	if (!wb::gxdebug::Headless())
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

	if (!wb::gxdebug::Headless())
	{
		fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);

		const auto& frag_src_str = frag_ss.str();
		const GLchar* frag_src = frag_src_str.c_str();
		glShaderSource(fragment_shader, 1, &frag_src, nullptr);
	}

	}

	if (wb::gxdebug::Headless())
	{
		DebugReportShader(vert_ss.str(), frag_ss.str());
		return;
	}

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
	GXTRACE("obj=%p map=%u", (void*)obj, GXU(mapid));

	const GLTexObj& txobj = *reinterpret_cast<GLTexObj*>(obj);

	if (mapid < 8)
	{
		g_texmap_dims[mapid][0] = static_cast<float>(txobj.wd);
		g_texmap_dims[mapid][1] = static_cast<float>(txobj.ht);
	}

	if (wb::gxdebug::Headless())
	{
		if (wb::gxdebug::Tracing(wb::gxdebug::TRACE_TEXTURE))
			DebugDecodeTexture(txobj, mapid);

		return;
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
	GXTRACE("stage=%u coord=%s map=%s chan=%s", GXU(tevstage), gdn::TexCoord(texcoord).c_str(),
		gdn::TexMap(texmap).c_str(), gdn::TevOrderChannel(color).c_str());

	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.texmap = texmap;
	ts.texcoord = texcoord;

	//glActiveTexture(GL_TEXTURE0 + g_texmap_start_index + tevstage);
}

void 	GX_SetTevSwapMode (uint8_t tevstage, uint8_t ras_sel, uint8_t tex_sel)
{
	GXTRACE("stage=%u ras=table%u(%s) tex=table%u(%s)", GXU(tevstage), GXU(ras_sel),
		gdn::SwapTable(g_tev_swap_tables[ras_sel & 3]).c_str(), GXU(tex_sel), gdn::SwapTable(g_tev_swap_tables[tex_sel & 3]).c_str());

	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.ras_swap = g_tev_swap_tables[ras_sel & 3];
	ts.tex_swap = g_tev_swap_tables[tex_sel & 3];
}

void 	GX_SetTevSwapModeTable (uint8_t table, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	GXTRACE("table=%u r=%u g=%u b=%u a=%u", GXU(table), GXU(r), GXU(g), GXU(b), GXU(a));

	g_tev_swap_tables[table & 3] = (r & 3) | ((g & 3) << 2)
		| ((b & 3) << 4) | ((a & 3) << 6);
}

void 	GX_SetTevIndirect (uint8_t tevstage, uint8_t indtexid, uint8_t format, uint8_t bias, uint8_t mtxid,
	uint8_t wrap_s, uint8_t wrap_t, uint8_t addprev, uint8_t utclod, uint8_t a)
{
	GXTRACE("stage=%u indtex=%u fmt=%s bias=%s mtx=%s wrap=%s/%s addprev=%u utclod=%u alpha=%s", GXU(tevstage),
		GXU(indtexid), gdn::IndFormat(format).c_str(), gdn::IndBias(bias).c_str(), gdn::IndMtx(mtxid).c_str(),
		gdn::IndWrap(wrap_s).c_str(), gdn::IndWrap(wrap_t).c_str(), GXU(addprev), GXU(utclod), gdn::IndAlpha(a).c_str());

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
	GXTRACE("ind_stage=%u coord=%s map=%s", GXU(ind_stage), gdn::TexCoord(tex_coord).c_str(), gdn::TexMap(tex_map).c_str());

	if (ind_stage < 4)
	{
		g_ind_tex_order[ind_stage].tex_coord = tex_coord;
		g_ind_tex_order[ind_stage].tex_map = tex_map;
	}
}

void 	GX_SetIndTexCoordScale (uint8_t ind_stage, uint8_t scale_s, uint8_t scale_t)
{
	GXTRACE("ind_stage=%u scale_s=%s scale_t=%s", GXU(ind_stage), gdn::IndScale(scale_s).c_str(), gdn::IndScale(scale_t).c_str());

	if (ind_stage < 4)
	{
		g_ind_tex_coord_scale[ind_stage].scale_s = scale_s;
		g_ind_tex_coord_scale[ind_stage].scale_t = scale_t;
	}
}

void GX_SetIndTexMatrix(uint8_t mtx_ind, Mtx23 offset_mtx, int8_t scale_exp)
{
	GXTRACE("mtx=%s [[%g %g %g] [%g %g %g]] scale_exp=%d", gdn::IndMtx(mtx_ind).c_str(),
		double(offset_mtx[0][0]), double(offset_mtx[0][1]), double(offset_mtx[0][2]),
		double(offset_mtx[1][0]), double(offset_mtx[1][1]), double(offset_mtx[1][2]), int(scale_exp));

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
	GXTRACE("num=%u", GXU(num_stages));

	g_num_ind_stages = num_stages;
}

void 	GX_SetTevColorS10 (uint8_t tev_regid, GXColorS10 color)
{
	GXTRACE("reg=%s color=(%d,%d,%d,%d)", gdn::TevReg(tev_regid).c_str(), int(color.r), int(color.g), int(color.b), int(color.a));

	for (unsigned int i = 0; i != 4; ++i)
		g_color_registers[tev_regid - 1][i] = (float)(&color.r)[i] / 255;
}

void 	GX_SetTevKColor (uint8_t tev_regid, GXColor color)
{
	GXTRACE("reg=K%u color=%s", GXU(tev_regid), gdn::Color(color.r, color.g, color.b, color.a).c_str());

	for (unsigned int i = 0; i != 4; ++i)
		g_kcolor_registers[tev_regid][i] = (float)(&color.r)[i] / 255;
}

void 	GX_SetTevKAlphaSel (uint8_t tevstage, uint8_t sel)
{
	GXTRACE("stage=%u sel=%s", GXU(tevstage), gdn::TevKSel(sel).c_str());

	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.kalpha_sel = sel & 0x1f;
}

void 	GX_SetTevKColorSel (uint8_t tevstage, uint8_t sel)
{
	GXTRACE("stage=%u sel=%s", GXU(tevstage), gdn::TevKSel(sel).c_str());

	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.kcolor_sel = sel & 0x1f;
}

void 	GX_SetTevAlphaIn (uint8_t tevstage, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	GXTRACE("stage=%u a=%s b=%s c=%s d=%s", GXU(tevstage), gdn::TevAlphaArg(a).c_str(), gdn::TevAlphaArg(b).c_str(),
		gdn::TevAlphaArg(c).c_str(), gdn::TevAlphaArg(d).c_str());

	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.alpha_a = a & 0x7;
	ts.alpha_b = b & 0x7;
	ts.alpha_c = c & 0x7;
	ts.alpha_d = d & 0x7;
}

void 	GX_SetTevAlphaOp (uint8_t tevstage, uint8_t tevop, uint8_t tevbias, uint8_t tevscale, uint8_t clamp, uint8_t tevregid)
{
	GXTRACE("stage=%u op=%s bias=%s scale=%s clamp=%u out=%s", GXU(tevstage), gdn::TevOp(tevop, true).c_str(),
		gdn::TevBias(tevbias).c_str(), gdn::TevScale(tevscale).c_str(), GXU(clamp), gdn::TevReg(tevregid).c_str());

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
	GXTRACE("stage=%u a=%s b=%s c=%s d=%s", GXU(tevstage), gdn::TevColorArg(a).c_str(), gdn::TevColorArg(b).c_str(),
		gdn::TevColorArg(c).c_str(), gdn::TevColorArg(d).c_str());

	ActiveStage(tevstage);

	TevStageProps& ts = g_active_stages[tevstage];
	ts.color_a = a & 0xf;
	ts.color_b = b & 0xf;
	ts.color_c = c & 0xf;
	ts.color_d = d & 0xf;
}

void 	GX_SetTevColorOp (uint8_t tevstage, uint8_t tevop, uint8_t tevbias, uint8_t tevscale, uint8_t clamp, uint8_t tevregid)
{
	GXTRACE("stage=%u op=%s bias=%s scale=%s clamp=%u out=%s", GXU(tevstage), gdn::TevOp(tevop, false).c_str(),
		gdn::TevBias(tevbias).c_str(), gdn::TevScale(tevscale).c_str(), GXU(clamp), gdn::TevReg(tevregid).c_str());

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
	GXTRACE("coord=%s type=%s src=%s mtx=%s", gdn::TexCoord(texcoord).c_str(), gdn::TexGenType(tgen_typ).c_str(),
		gdn::TexGenSrc(tgen_src).c_str(), gdn::TexMtx(mtxsrc).c_str());

	if (texcoord >= g_active_texgens.size())
		g_active_texgens.resize(texcoord + 1);

	TexGenProps& tg = g_active_texgens[texcoord];
	tg.tgen_typ = tgen_typ;
	tg.tgen_src = tgen_src;
	tg.mtxsrc = mtxsrc;

	g_draw.tex_mtx_dirty = true;
}

void 	GX_SetNumTexGens (uint8_t num)
{
	GXTRACE("num=%u", GXU(num));

	g_active_texgens.resize(num);

	g_draw.tex_mtx_dirty = true;
}

void 	GX_SetNumTevStages (uint8_t num)
{
	GXTRACE("num=%u", GXU(num));

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

	if (wb::gxdebug::Headless())
	{
		static std::map<ShaderKey, int> shader_ids;

		auto found = shader_ids.find(key);
		const bool is_new = found == shader_ids.end();

		if (is_new)
			found = shader_ids.emplace(key, int(shader_ids.size()) + 1).first;

		g_debug_shader_id = found->second;

		wb::gxdebug::Stats& stats = wb::gxdebug::GetStats();
		++(is_new ? stats.shaders_generated : stats.shaders_reused);

		if (wb::gxdebug::Tracing(wb::gxdebug::TRACE_GX))
		{
			wb::gxdebug::Log("[gx] => shader #%d (%s), %zu tev stage(s), %zu texgen(s), %zu indirect stage(s)",
				g_debug_shader_id, is_new ? "new" : "reused", g_active_stages.size(),
				g_active_texgens.size(), ind_stages.size());

			if (is_new)
			{
				std::ostringstream description;
				DescribeShaderConfig(description, g_active_stages, g_active_texgens, ind_stages);
				wb::gxdebug::Log("%s", description.str().c_str());
			}
		}

		if (is_new)
		{
			CompiledTevStages scratch;
			scratch.Compile(g_active_stages, g_active_texgens, ind_stages);
		}

		return;
	}

	CompiledTevStages& comptevs = g_compiled_tev_stages[key];

	// compile program if needed
	if (!comptevs.program)
		comptevs.Compile(g_active_stages, g_active_texgens, ind_stages);

	// enable the program
	comptevs.Enable();
}

static std::string MtxText(const Mtx m, int rows)
{
	std::string text = "[";
	for (int r = 0; r != rows; ++r)
	{
		text += r ? " [" : "[";
		for (int c = 0; c != 4; ++c)
			text += wb::gxdebug::Format(c ? " %g" : "%g", double(m[r][c]));
		text += "]";
	}
	return text + "]";
}

static void ExpandMtx(const Mtx m, int rows, f32* out)
{
	for (int c = 0; c != 4; ++c)
	{
		for (int r = 0; r != 3; ++r)
			out[c * 4 + r] = r < rows ? m[r][c] : (r == c ? 1.f : 0.f);

		out[c * 4 + 3] = c == 3 ? 1.f : 0.f;
	}
}

void 	guMtxIdentity (Mtx mt)
{
	for (int r = 0; r != 3; ++r)
		for (int c = 0; c != 4; ++c)
			mt[r][c] = r == c ? 1.f : 0.f;
}

void 	guMtxConcat (const Mtx a, const Mtx b, Mtx ab)
{
	Mtx result;

	for (int r = 0; r != 3; ++r)
		for (int c = 0; c != 4; ++c)
			result[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c] + (c == 3 ? a[r][3] : 0.f);

	memcpy(ab, result, sizeof(Mtx));
}

void 	guMtxTrans (Mtx mt, f32 xT, f32 yT, f32 zT)
{
	guMtxIdentity(mt);
	mt[0][3] = xT;
	mt[1][3] = yT;
	mt[2][3] = zT;
}

void 	guMtxScale (Mtx mt, f32 xS, f32 yS, f32 zS)
{
	guMtxIdentity(mt);
	mt[0][0] = xS;
	mt[1][1] = yS;
	mt[2][2] = zS;
}

void 	guMtxRotRad (Mtx mt, char axis, f32 rad)
{
	const f32 s = std::sin(rad);
	const f32 c = std::cos(rad);

	guMtxIdentity(mt);

	switch (axis)
	{
	case 'x': case 'X':
		mt[1][1] = c;  mt[1][2] = -s;
		mt[2][1] = s;  mt[2][2] = c;
		break;
	case 'y': case 'Y':
		mt[0][0] = c;  mt[0][2] = s;
		mt[2][0] = -s; mt[2][2] = c;
		break;
	case 'z': case 'Z':
		mt[0][0] = c;  mt[0][1] = -s;
		mt[1][0] = s;  mt[1][1] = c;
		break;
	}
}

void 	guMtxRotDeg (Mtx mt, char axis, f32 deg)
{
	guMtxRotRad(mt, axis, deg * 3.14159265358979323846f / 180.f);
}

void 	guOrtho (Mtx44 mt, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f)
{
	memset(mt, 0, sizeof(Mtx44));

	mt[0][0] = 2.f / (r - l);
	mt[0][3] = -(r + l) / (r - l);
	mt[1][1] = 2.f / (t - b);
	mt[1][3] = -(t + b) / (t - b);
	mt[2][2] = -1.f / (f - n);
	mt[2][3] = -f / (f - n);
	mt[3][3] = 1.f;
}

void 	GX_SetViewport (f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ)
{
	GXTRACE("x=%g y=%g w=%g h=%g near=%g far=%g", double(xOrig), double(yOrig), double(wd), double(ht),
		double(nearZ), double(farZ));

	if (wb::gxdebug::Headless())
		return;

	glViewport(static_cast<GLint>(xOrig), static_cast<GLint>(yOrig), static_cast<GLsizei>(wd), static_cast<GLsizei>(ht));
	glDepthRange(nearZ, farZ);
}

void 	GX_LoadProjectionMtx (Mtx44 mt, uint8_t type)
{
	GXTRACE("type=%s [[%g %g %g %g] [%g %g %g %g] [%g %g %g %g] [%g %g %g %g]]",
		type == GX_ORTHOGRAPHIC ? "ORTHOGRAPHIC" : "PERSPECTIVE",
		double(mt[0][0]), double(mt[0][1]), double(mt[0][2]), double(mt[0][3]),
		double(mt[1][0]), double(mt[1][1]), double(mt[1][2]), double(mt[1][3]),
		double(mt[2][0]), double(mt[2][1]), double(mt[2][2]), double(mt[2][3]),
		double(mt[3][0]), double(mt[3][1]), double(mt[3][2]), double(mt[3][3]));

	// column-major, and GX clips depth to -w..0 where GL clips to -w..w: z' = 2z + w
	for (int c = 0; c != 4; ++c)
	{
		g_draw.projection[c * 4 + 0] = mt[0][c];
		g_draw.projection[c * 4 + 1] = mt[1][c];
		g_draw.projection[c * 4 + 2] = 2.f * mt[2][c] + mt[3][c];
		g_draw.projection[c * 4 + 3] = mt[3][c];
	}

	if (wb::gxdebug::Headless())
		return;

	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(g_draw.projection);
	glMatrixMode(GL_MODELVIEW);
}

void 	GX_LoadPosMtxImm (Mtx mt, uint32_t pnidx)
{
	GXTRACE("slot=%u %s", GXU(pnidx), MtxText(mt, 3).c_str());

	if (pnidx % 3 || pnidx / 3 >= 10)
		return;

	ExpandMtx(mt, 3, g_draw.pos_mtx[pnidx / 3]);

	if (pnidx == g_draw.current_pos_mtx && !wb::gxdebug::Headless())
	{
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf(g_draw.pos_mtx[pnidx / 3]);
	}
}

void 	GX_SetCurrentMtx (uint32_t mtx)
{
	GXTRACE("slot=%u", GXU(mtx));

	if (mtx % 3 || mtx / 3 >= 10)
		return;

	g_draw.current_pos_mtx = mtx;

	if (!wb::gxdebug::Headless())
	{
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf(g_draw.pos_mtx[mtx / 3]);
	}
}

void 	GX_LoadTexMtxImm (Mtx mt, uint32_t texidx, uint8_t type)
{
	GXTRACE("%s type=%s %s", gdn::TexMtx(texidx).c_str(), type == GX_MTX2x4 ? "MTX2x4" : "MTX3x4",
		MtxText(mt, type == GX_MTX2x4 ? 2 : 3).c_str());

	if (texidx < GX_TEXMTX0 || texidx > GX_TEXMTX9 || (texidx - GX_TEXMTX0) % 3)
		return;

	ExpandMtx(mt, type == GX_MTX2x4 ? 2 : 3, g_draw.tex_mtx[(texidx - GX_TEXMTX0) / 3]);
	g_draw.tex_mtx_dirty = true;
}

void 	GX_SetZMode (uint8_t enable, uint8_t func, uint8_t update_enable)
{
	GXTRACE("enable=%u func=%s update=%u", GXU(enable), gdn::Compare(func).c_str(), GXU(update_enable));

	if (wb::gxdebug::Headless())
		return;

	if (enable)
		glEnable(GL_DEPTH_TEST);
	else
		glDisable(GL_DEPTH_TEST);

	glDepthFunc(GL_NEVER + func);
	glDepthMask(update_enable ? GL_TRUE : GL_FALSE);
}

void 	GX_SetChanMatColor (int32_t channel, GXColor color)
{
	GXTRACE("channel=%d color=%s", int(channel), gdn::Color(color.r, color.g, color.b, color.a).c_str());

	if (wb::gxdebug::Headless())
		return;

	// the "current colour": used for vertices that do not carry their own
	glColor4ubv(&color.r);
}

void 	GX_SetCopyClear (GXColor clear_clr, uint32_t clear_z)
{
	GXTRACE("color=%s z=%u", gdn::Color(clear_clr.r, clear_clr.g, clear_clr.b, clear_clr.a).c_str(), GXU(clear_z));

	g_draw.clear_color = clear_clr;
}

void 	GX_ClearVtxDesc (void)
{
	GXTRACE("");

	g_draw.pos = false;
	g_draw.clr0 = false;
	memset(g_draw.tex, 0, sizeof(g_draw.tex));
}

void 	GX_SetVtxDesc (uint8_t attr, uint8_t type)
{
	const std::string attr_name =
		attr == GX_VA_POS ? "POS" :
		attr == GX_VA_NRM ? "NRM" :
		attr == GX_VA_CLR0 ? "CLR0" :
		attr == GX_VA_CLR1 ? "CLR1" :
		attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7 ? "TEX" + std::to_string(attr - GX_VA_TEX0) :
		"?(" + std::to_string(attr) + ")";

	GXTRACE("attr=%s type=%s", attr_name.c_str(), type == GX_DIRECT ? "DIRECT" : type == GX_NONE ? "NONE" : "INDEX");

	const bool enabled = type != GX_NONE;

	if (attr == GX_VA_POS)
		g_draw.pos = enabled;
	else if (attr == GX_VA_CLR0)
		g_draw.clr0 = enabled;
	else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7)
		g_draw.tex[attr - GX_VA_TEX0] = enabled;
}

void 	GX_SetVtxAttrFmt (uint8_t vtxfmt, uint32_t vtxattr, uint32_t comptype, uint32_t compsize, uint32_t frac)
{
	GXTRACE("fmt=%u attr=%u comptype=%u compsize=%u frac=%u", GXU(vtxfmt), GXU(vtxattr), GXU(comptype), GXU(compsize), GXU(frac));
}

// hands the texture matrices to the texture units: unit n uses what texgen n asks for
static void ApplyTexMatrices()
{
	if (!g_draw.tex_mtx_dirty)
		return;

	g_draw.tex_mtx_dirty = false;

	f32 identity[16];
	WGXDrawState::Identity(identity);

	glMatrixMode(GL_TEXTURE);

	for (unsigned unit = 0; unit != 8; ++unit)
	{
		const f32* matrix = identity;

		if (unit < g_active_texgens.size())
		{
			const TexGenProps& tg = g_active_texgens[unit];

			if (tg.tgen_typ == GX_TG_MTX2x4 && tg.mtxsrc >= GX_TEXMTX0 && tg.mtxsrc <= GX_TEXMTX9 &&
				(tg.mtxsrc - GX_TEXMTX0) % 3 == 0)
			{
				matrix = g_draw.tex_mtx[(tg.mtxsrc - GX_TEXMTX0) / 3];
			}
		}

		glActiveTexture(GL_TEXTURE0 + unit);
		glLoadMatrixf(matrix);
	}

	glMatrixMode(GL_MODELVIEW);
}

static void FlushVertex()
{
	if (!g_draw.vertex_pending)
		return;

	g_draw.vertex_pending = false;

	if (g_draw.have_color)
		glColor4ub(g_draw.color[0], g_draw.color[1], g_draw.color[2], g_draw.color[3]);

	for (unsigned i = 0; i != g_draw.texcoord_count && i != g_draw.tex_unit_count; ++i)
		glMultiTexCoord2fv(GL_TEXTURE0 + g_draw.tex_units[i], g_draw.texcoords[i]);

	if (g_draw.vertex_3d)
		glVertex3f(g_draw.x, g_draw.y, g_draw.z);
	else
		glVertex2f(g_draw.x, g_draw.y);
}

void 	GX_Begin (uint8_t primitve, uint8_t vtxfmt, uint16_t vtxcnt)
{
	if (wb::gxdebug::Tracing(wb::gxdebug::TRACE_GX))
	{
		std::string attributes = g_draw.pos ? "POS" : "";
		if (g_draw.clr0)
			attributes += " CLR0";
		for (unsigned i = 0; i != 8; ++i)
			if (g_draw.tex[i])
				attributes += wb::gxdebug::Format(" TEX%u", i);

		wb::gxdebug::TraceGx("GX_Begin", "primitive=0x%02x fmt=%u vertices=%u attributes: %s", GXU(primitve), GXU(vtxfmt),
			GXU(vtxcnt), attributes.c_str());
	}

	g_draw.vertex_pending = false;
	g_draw.tex_unit_count = 0;
	for (unsigned i = 0; i != 8; ++i)
		if (g_draw.tex[i])
			g_draw.tex_units[g_draw.tex_unit_count++] = i;

	if (wb::gxdebug::Headless())
		return;

	ApplyTexMatrices();

	GLenum mode = GL_QUADS;
	switch (primitve)
	{
	case GX_POINTS: mode = GL_POINTS; break;
	case GX_LINES: mode = GL_LINES; break;
	case GX_LINESTRIP: mode = GL_LINE_STRIP; break;
	case GX_TRIANGLES: mode = GL_TRIANGLES; break;
	case GX_TRIANGLESTRIP: mode = GL_TRIANGLE_STRIP; break;
	case GX_TRIANGLEFAN: mode = GL_TRIANGLE_FAN; break;
	default: break;
	}

	glBegin(mode);
}

void 	GX_End (void)
{
	GXTRACE("");

	if (wb::gxdebug::Headless())
		return;

	FlushVertex();
	glEnd();
}

// per vertex data is not traced individually: it is a lot of calls for little information

void 	GX_Position2f32 (f32 x, f32 y)
{
	if (wb::gxdebug::Headless())
		return;

	FlushVertex();

	g_draw.vertex_pending = true;
	g_draw.vertex_3d = false;
	g_draw.x = x;
	g_draw.y = y;
	g_draw.z = 0.f;
	g_draw.have_color = false;
	g_draw.texcoord_count = 0;
}

void 	GX_Position3f32 (f32 x, f32 y, f32 z)
{
	if (wb::gxdebug::Headless())
		return;

	FlushVertex();

	g_draw.vertex_pending = true;
	g_draw.vertex_3d = true;
	g_draw.x = x;
	g_draw.y = y;
	g_draw.z = z;
	g_draw.have_color = false;
	g_draw.texcoord_count = 0;
}

void 	GX_Color4u8 (uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	if (wb::gxdebug::Headless())
		return;

	g_draw.have_color = true;
	g_draw.color[0] = r;
	g_draw.color[1] = g;
	g_draw.color[2] = b;
	g_draw.color[3] = a;
}

void 	GX_TexCoord2f32 (f32 s, f32 t)
{
	if (wb::gxdebug::Headless() || g_draw.texcoord_count == 8)
		return;

	g_draw.texcoords[g_draw.texcoord_count][0] = s;
	g_draw.texcoords[g_draw.texcoord_count][1] = t;
	++g_draw.texcoord_count;
}

// ---- host side ----------------------------------------------------------------------------

void 	WGX_InitFrameState (int width, int height)
{
	if (wb::gxdebug::Tracing(wb::gxdebug::TRACE_GX))
		wb::gxdebug::Log("[gx] WGX_InitFrameState(%d, %d)", width, height);

	GX_SetViewport(0.f, 0.f, static_cast<f32>(width), static_cast<f32>(height), 0.f, 1.f);

	Mtx44 projection;
	guOrtho(projection, 1.f, -1.f, -1.f, 1.f, -1000.f, 1000.f);
	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);

	Mtx identity;
	guMtxIdentity(identity);
	GX_LoadPosMtxImm(identity, GX_PNMTX0);

	GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);

	if (!wb::gxdebug::Headless())
		glEnable(GL_STENCIL_TEST);
}

void 	WGX_BeginFrame (void)
{
	if (wb::gxdebug::Tracing(wb::gxdebug::TRACE_GX))
		wb::gxdebug::Log("[gx] WGX_BeginFrame()");

	Mtx44 projection;
	guOrtho(projection, 1.f, -1.f, -1.f, 1.f, -1000.f, 1000.f);

	Mtx identity;
	guMtxIdentity(identity);

	if (wb::gxdebug::Headless())
		return;

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	g_draw.tex_mtx_dirty = true;

	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_LoadPosMtxImm(identity, GX_PNMTX0);

	glStencilFunc(GL_ALWAYS, 0, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

	glClearColor(g_draw.clear_color.r / 255.f, g_draw.clear_color.g / 255.f, g_draw.clear_color.b / 255.f,
		g_draw.clear_color.a / 255.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void 	WGX_ReadPixels (int x, int y, int width, int height, void *rgba)
{
	if (wb::gxdebug::Headless())
		return;

	glFinish();

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static GLuint g_offscreen_framebuffer = 0;
static GLuint g_offscreen_depth = 0;
static GLuint g_offscreen_color = 0;

bool 	WGX_CreateOffscreenTarget (int width, int height)
{
	if (wb::gxdebug::Headless())
		return true;

	glGenFramebuffers(1, &g_offscreen_framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, g_offscreen_framebuffer);

	glGenTextures(1, &g_offscreen_color);
	glBindTexture(GL_TEXTURE_2D, g_offscreen_color);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_offscreen_color, 0);

	glGenRenderbuffers(1, &g_offscreen_depth);
	glBindRenderbuffer(GL_RENDERBUFFER, g_offscreen_depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_offscreen_depth);

	return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

void 	WGX_DestroyOffscreenTarget (void)
{
	if (wb::gxdebug::Headless())
		return;

	if (g_offscreen_depth)
		glDeleteRenderbuffers(1, &g_offscreen_depth);

	if (g_offscreen_color)
		glDeleteTextures(1, &g_offscreen_color);

	if (g_offscreen_framebuffer)
		glDeleteFramebuffers(1, &g_offscreen_framebuffer);

	g_offscreen_depth = 0;
	g_offscreen_color = 0;
	g_offscreen_framebuffer = 0;
}

namespace wb::gxdebug
{

void DumpWrapGxState(std::ostream& os)
{
	Print(os, "WrapGx state:\n");

	if (sizeof(GLTexObj) > sizeof(GXTexObj))
		Print(os, "  !! sizeof(GLTexObj)=%zu but sizeof(GXTexObj)=%zu: GLTexObj is cast onto a GXTexObj, so its LOD fields\n"
			"  !! (min_lod, lod_bias, bias_clamp, edge_lod) are read and written outside of the object\n",
			sizeof(GLTexObj), sizeof(GXTexObj));
	else
		Print(os, "  sizeof(GLTexObj)=%zu sizeof(GXTexObj)=%zu\n", sizeof(GLTexObj), sizeof(GXTexObj));

	Print(os, "  active TEV configuration (%zu stage(s)):\n", g_active_stages.size());
	DescribeShaderConfig(os, g_active_stages, g_active_texgens, CurrentIndStages());

	for (unsigned i = 0; i != 3; ++i)
		Print(os, "  color register REG%u: (%d,%d,%d,%d)\n", i,
			int(g_color_registers[i][0] * 255.f + (g_color_registers[i][0] < 0 ? -0.5f : 0.5f)),
			int(g_color_registers[i][1] * 255.f + (g_color_registers[i][1] < 0 ? -0.5f : 0.5f)),
			int(g_color_registers[i][2] * 255.f + (g_color_registers[i][2] < 0 ? -0.5f : 0.5f)),
			int(g_color_registers[i][3] * 255.f + (g_color_registers[i][3] < 0 ? -0.5f : 0.5f)));

	for (unsigned i = 0; i != 4; ++i)
		Print(os, "  konst color K%u: %s\n", i, names::Color(unsigned(g_kcolor_registers[i][0] * 255.f + 0.5f),
			unsigned(g_kcolor_registers[i][1] * 255.f + 0.5f), unsigned(g_kcolor_registers[i][2] * 255.f + 0.5f),
			unsigned(g_kcolor_registers[i][3] * 255.f + 0.5f)).c_str());

	for (unsigned i = 0; i != 4; ++i)
		Print(os, "  swap table %u: %s\n", i, names::SwapTable(g_tev_swap_tables[i]).c_str());

	for (unsigned i = 0; i != 12; ++i)
	{
		const IndTexMtx& m = g_ind_tex_mtx[i];
		if (m.set)
			Print(os, "  indirect matrix %s: [[%g %g %g] [%g %g %g]] scale_exp=%d\n", names::IndMtx(i + GX_ITM_0).c_str(),
				double(m.m[0][0]), double(m.m[0][1]), double(m.m[0][2]), double(m.m[1][0]), double(m.m[1][1]),
				double(m.m[1][2]), int(m.scale_exp));
	}

	for (unsigned i = 0; i != 8; ++i)
		if (g_texmap_dims[i][0] || g_texmap_dims[i][1])
			Print(os, "  texture map %u: %gx%g\n", i, double(g_texmap_dims[i][0]), double(g_texmap_dims[i][1]));

	for (const auto& tlut : g_tlut_names)
		Print(os, "  tlut %u: lut=%p entries=%u fmt=%s\n", GXU(tlut.first), tlut.second.lut, GXU(tlut.second.entries),
			names::TlutFormat(tlut.second.fmt).c_str());

	Print(os, "  %zu texture(s) in the GL cache, %zu shader(s) compiled\n", g_texture_cache.size(), g_compiled_tev_stages.size());
}

}
