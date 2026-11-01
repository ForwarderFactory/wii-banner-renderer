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

#ifndef WII_BNR_WRAP_GX_
#define WII_BNR_WRAP_GX_

/* should we really wrap this in namespace wb?
 * doing so would immediately make this incompatible with the original GX implementation
 * im choosing not to do so as of now, we'll see how that ends up.
 */

#include <cstdint>
#include <cstring>

#define 	GX_MAX_TEVREG   4
#define 	GX_TEVPREV   0
#define 	GX_TEVREG0   1
#define 	GX_TEVREG1   2
#define 	GX_TEVREG2   3

#define		GX_TG_MTX3x4	0
#define		GX_TG_MTX2x4	1
#define		GX_TG_BUMP0		2
#define		GX_TG_BUMP7		9
#define		GX_TG_SRTG		10

#define		GX_TG_POS		0
#define		GX_TG_NRM		1
#define		GX_TG_BINRM		2
#define		GX_TG_TANGENT	3
#define		GX_TG_TEX0		4
#define		GX_TG_TEX1		5
#define		GX_TG_TEX2		6
#define		GX_TG_TEX3		7
#define		GX_TG_TEX4		8
#define		GX_TG_TEX5		9
#define		GX_TG_TEX6		10
#define		GX_TG_TEX7		11
#define		GX_TG_COLOR0	21
#define		GX_TG_COLOR1	22

#define		GX_TEXMTX0		30
#define		GX_TEXMTX1		33
#define		GX_TEXMTX2		36
#define		GX_TEXMTX3		39
#define		GX_TEXMTX4		42
#define		GX_TEXMTX5		45
#define		GX_TEXMTX6		48
#define		GX_TEXMTX7		51
#define		GX_TEXMTX8		54
#define		GX_TEXMTX9		57
#define		GX_IDENTITY		60

#define		GX_TEXCOORD0	0

// channel color/alpha source (GX_SetChanCtrl "matsrc")
#define		GX_SRC_REG		0	// use the material's constant register color
#define		GX_SRC_VTX		1	// use the per-vertex color

// ---- drawing: matrices, vertex data and frame state ------------------------------------
// These follow libogc's names, values and call order (gu.h / gx.h), so code written against
// this header also compiles against libogc on a Wii. OpenGL is only used inside WrapGx.cpp.

#define		GX_TRUE	1
#define		GX_FALSE	0

#define		GX_PNMTX0	0	// position matrix slot 0, the only one wb draws with
#define		GX_MTX3x4	0
#define		GX_MTX2x4	1
#define		GX_PERSPECTIVE	0
#define		GX_ORTHOGRAPHIC	1

#define		GX_NEVER	0
#define		GX_LESS	1
#define		GX_EQUAL	2
#define		GX_LEQUAL	3
#define		GX_GREATER	4
#define		GX_NEQUAL	5
#define		GX_GEQUAL	6
#define		GX_ALWAYS	7

#define		GX_POINTS	0xb8
#define		GX_LINES	0xa8
#define		GX_LINESTRIP	0xb0
#define		GX_TRIANGLES	0x90
#define		GX_TRIANGLESTRIP	0x98
#define		GX_TRIANGLEFAN	0xa0
#define		GX_QUADS	0x80

#define		GX_VTXFMT0	0

#define		GX_VA_POS	9
#define		GX_VA_NRM	10
#define		GX_VA_CLR0	11
#define		GX_VA_CLR1	12
#define		GX_VA_TEX0	13
#define		GX_VA_TEX7	20

#define		GX_NONE	0
#define		GX_DIRECT	1

#define		GX_POS_XY	0
#define		GX_POS_XYZ	1
#define		GX_CLR_RGB	0
#define		GX_CLR_RGBA	1
#define		GX_TEX_S	0
#define		GX_TEX_ST	1

#define		GX_F32	4
#define		GX_RGBA8	5

#define		GX_COLOR0A0	4

typedef float f32;

typedef f32 Mtx[3][4];
typedef f32 Mtx44[4][4];

// watev
struct GXTexObj
{
	GXTexObj()
	{
		memset(val, 0, sizeof(val));
	};

	uint32_t 	val [8];
};

struct GXTlutObj
{
	GXTlutObj()
	{
		memset(val, 0, sizeof(val));
	};

	uint32_t val[8];
};

struct GXColor
{
	uint8_t r, g, b, a;
};

struct GXColorS10
{
	int16_t r, g, b, a;
};

typedef void GXFifoObj;

GXFifoObj * 	GX_Init (void *base, uint32_t size);

void	guMtxIdentity (Mtx mt);
void	guMtxConcat (const Mtx a, const Mtx b, Mtx ab);
void	guMtxTrans (Mtx mt, f32 xT, f32 yT, f32 zT);
void	guMtxScale (Mtx mt, f32 xS, f32 yS, f32 zS);
void	guMtxRotRad (Mtx mt, char axis, f32 rad);
void	guMtxRotDeg (Mtx mt, char axis, f32 deg);
void	guOrtho (Mtx44 mt, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f);

void	GX_SetViewport (f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ);
void	GX_LoadProjectionMtx (Mtx44 mt, uint8_t type);
void	GX_LoadPosMtxImm (Mtx mt, uint32_t pnidx);
void	GX_SetCurrentMtx (uint32_t mtx);
void	GX_LoadTexMtxImm (Mtx mt, uint32_t texidx, uint8_t type);

void	GX_SetZMode (uint8_t enable, uint8_t func, uint8_t update_enable);
void	GX_SetChanMatColor (int32_t channel, GXColor color);
void	GX_SetCopyClear (GXColor clear_clr, uint32_t clear_z);

void	GX_ClearVtxDesc (void);
void	GX_SetVtxDesc (uint8_t attr, uint8_t type);
void	GX_SetVtxAttrFmt (uint8_t vtxfmt, uint32_t vtxattr, uint32_t comptype, uint32_t compsize, uint32_t frac);
void	GX_Begin (uint8_t primitve, uint8_t vtxfmt, uint16_t vtxcnt);
void	GX_End (void);
void	GX_Position2f32 (f32 x, f32 y);
void	GX_Position3f32 (f32 x, f32 y, f32 z);
void	GX_Color4u8 (uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void	GX_TexCoord2f32 (f32 s, f32 t);

void	WGX_InitFrameState (int width, int height);

void	WGX_BeginFrame (void);

void	WGX_ReadPixels (int x, int y, int width, int height, void *rgba);

bool	WGX_CreateOffscreenTarget (int width, int height);
void	WGX_DestroyOffscreenTarget (void);

uint32_t 	GX_GetTexBufferSize (uint16_t wd, uint16_t ht, uint32_t fmt, uint8_t mipmap, uint8_t maxlod);

void 	GX_InitTlutObj (GXTlutObj *obj, void *lut, uint8_t fmt, uint16_t entries);
void 	GX_LoadTlut (GXTlutObj *obj, uint32_t tlut_name);
void 	GX_InitTexObjTlut (GXTexObj *obj, uint32_t tlut_name);

void 	GX_InitTexObj (GXTexObj *obj, void *img_ptr, uint16_t wd, uint16_t ht, uint8_t fmt, uint8_t wrap_s, uint8_t wrap_t, uint8_t mipmap);
void 	GX_InitTexObjWrapMode (GXTexObj *obj, uint8_t wrap_s, uint8_t wrap_t);
void 	GX_InitTexObjFilterMode (GXTexObj *obj, uint8_t minfilt, uint8_t magfilt);
void 	GX_InitTexObjLOD (GXTexObj *obj, uint8_t minfilt, uint8_t magfilt, f32 minlod, f32 maxlod, f32 lodbias, uint8_t biasclamp, uint8_t edgelod, uint8_t maxaniso);

void 	GX_LoadTexObj (GXTexObj *obj, uint8_t mapid);

void 	GX_SetBlendMode (uint8_t type, uint8_t src_fact, uint8_t dst_fact, uint8_t op);
void 	GX_SetAlphaCompare (uint8_t comp0, uint8_t ref0, uint8_t aop, uint8_t comp1, uint8_t ref1);

void 	GX_SetTevOrder (uint8_t tevstage, uint8_t texcoord, uint32_t texmap, uint8_t color);
void 	GX_SetTevSwapMode (uint8_t tevstage, uint8_t ras_sel, uint8_t tex_sel);
void 	GX_SetTevSwapModeTable (uint8_t table, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

void 	GX_SetTevIndirect (uint8_t tevstage, uint8_t indtexid, uint8_t format, uint8_t bias, uint8_t mtxid,
	uint8_t wrap_s, uint8_t wrap_t, uint8_t addprev, uint8_t utclod, uint8_t a);

void 	GX_SetTevKAlphaSel (uint8_t tevstage, uint8_t sel);
void 	GX_SetTevKColorSel (uint8_t tevstage, uint8_t sel);

void 	GX_SetTevAlphaIn (uint8_t tevstage, uint8_t a, uint8_t b, uint8_t c, uint8_t d);
void 	GX_SetTevAlphaOp (uint8_t tevstage, uint8_t tevop, uint8_t tevbias, uint8_t tevscale, uint8_t clamp, uint8_t tevregid);

void 	GX_SetTevColorIn (uint8_t tevstage, uint8_t a, uint8_t b, uint8_t c, uint8_t d);
void 	GX_SetTevColorOp (uint8_t tevstage, uint8_t tevop, uint8_t tevbias, uint8_t tevscale, uint8_t clamp, uint8_t tevregid);

void 	GX_SetTevColorS10 (uint8_t tev_regid, GXColorS10 color);
void 	GX_SetTevKColor (uint8_t tev_regid, GXColor color);

void 	GX_SetNumTevStages (uint8_t num);

void 	GX_SetTexCoordGen (uint8_t texcoord, uint8_t tgen_typ, uint8_t tgen_src, uint8_t mtxsrc);
void 	GX_SetNumTexGens (uint8_t num);

typedef f32 Mtx23[2][3];

#define 	GX_ITM_OFF		0
#define 	GX_ITM_0		1
#define 	GX_ITM_1		2
#define 	GX_ITM_2		3
#define 	GX_ITM_S0		5
#define 	GX_ITM_S1		6
#define 	GX_ITM_S2		7
#define 	GX_ITM_T0		9
#define 	GX_ITM_T1		10
#define 	GX_ITM_T2		11

#define 	GX_ITF_8		0
#define 	GX_ITF_5		1
#define 	GX_ITF_4		2
#define 	GX_ITF_3		3

#define 	GX_ITB_NONE		0
#define 	GX_ITB_S		1
#define 	GX_ITB_T		2
#define 	GX_ITB_ST		3
#define 	GX_ITB_U		4
#define 	GX_ITB_SU		5
#define 	GX_ITB_TU		6
#define 	GX_ITB_STU		7

#define 	GX_ITBA_OFF		0
#define 	GX_ITBA_S		1
#define 	GX_ITBA_T		2
#define 	GX_ITBA_U		3

#define 	GX_ITW_OFF		0
#define 	GX_ITW_256		1
#define 	GX_ITW_128		2
#define 	GX_ITW_64		3
#define 	GX_ITW_32		4
#define 	GX_ITW_16		5
#define 	GX_ITW_0		6

#define 	GX_ITS_1		0
#define 	GX_ITS_2		1
#define 	GX_ITS_4		2
#define 	GX_ITS_8		3
#define 	GX_ITS_16		4
#define 	GX_ITS_32		5
#define 	GX_ITS_64		6
#define 	GX_ITS_128		7
#define 	GX_ITS_256		8

void 	GX_SetIndTexOrder (uint8_t ind_stage, uint8_t tex_coord, uint8_t tex_map);
void 	GX_SetIndTexCoordScale (uint8_t ind_stage, uint8_t scale_s, uint8_t scale_t);
void 	GX_SetIndTexMatrix (uint8_t mtx_ind, Mtx23 offset_mtx, int8_t scale_exp);
void 	GX_SetNumIndStages (uint8_t num_stages);

#endif
