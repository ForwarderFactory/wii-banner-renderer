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

#ifndef WII_BNR_WRAP_GX_
#define WII_BNR_WRAP_GX_

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

typedef float f32;

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

//void 	GX_SetViewport (f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ);

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
