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

#include "Types.h"

#define 	GX_MAX_TEVREG   4
#define 	GX_TEVPREV   0
#define 	GX_TEVREG0   1
#define 	GX_TEVREG1   2
#define 	GX_TEVREG2   3

typedef float f32;

// watev
struct GXTexObj
{
	GXTexObj()
	{
		memset(val, 0, sizeof(val));
	};

	u32 	val [8];
};

struct GXTlutObj
{
	GXTlutObj()
	{
		memset(val, 0, sizeof(val));
	};

	u32 val[8];
};

struct GXColor
{
	u8 r, g, b, a;
};

struct GXColorS10
{
	s16 r, g, b, a;
};

typedef void GXFifoObj;

GXFifoObj * 	GX_Init (void *base, u32 size);

//void 	GX_SetViewport (f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ);

u32 	GX_GetTexBufferSize (u16 wd, u16 ht, u32 fmt, u8 mipmap, u8 maxlod);

void GX_SetIndTexMtx(u8 mtx_index, const float offset[2][3]);

void 	GX_InitTlutObj (GXTlutObj *obj, void *lut, u8 fmt, u16 entries);
void 	GX_LoadTlut (GXTlutObj *obj, u32 tlut_name);
void 	GX_InitTexObjTlut (GXTexObj *obj, u32 tlut_name);

void 	GX_InitTexObj (GXTexObj *obj, void *img_ptr, u16 wd, u16 ht, u8 fmt, u8 wrap_s, u8 wrap_t, u8 mipmap);
void 	GX_InitTexObjWrapMode (GXTexObj *obj, u8 wrap_s, u8 wrap_t);
void 	GX_InitTexObjFilterMode (GXTexObj *obj, u8 minfilt, u8 magfilt);

void 	GX_LoadTexObj (GXTexObj *obj, u8 mapid);

void 	GX_SetBlendMode (u8 type, u8 src_fact, u8 dst_fact, u8 op);
void 	GX_SetAlphaCompare (u8 comp0, u8 ref0, u8 aop, u8 comp1, u8 ref1);

void 	GX_SetTevOrder (u8 tevstage, u8 texcoord, u32 texmap, u8 color);
void 	GX_SetTevSwapMode (u8 tevstage, u8 ras_sel, u8 tex_sel);
void 	GX_SetTevSwapModeTable (u8 table, u8 r, u8 g, u8 b, u8 a);

void 	GX_SetTevIndirect (u8 tevstage, u8 indtexid, u8 format, u8 bias, u8 mtxid,
	u8 wrap_s, u8 wrap_t, u8 addprev, u8 utclod, u8 a,
	u8 ind_tex_map, u8 ind_tex_coord, u8 ind_scale_s, u8 ind_scale_t);

void 	GX_SetTevKAlphaSel (u8 tevstage, u8 sel);
void 	GX_SetTevKColorSel (u8 tevstage, u8 sel);

void 	GX_SetTevAlphaIn (u8 tevstage, u8 a, u8 b, u8 c, u8 d);
void 	GX_SetTevAlphaOp (u8 tevstage, u8 tevop, u8 tevbias, u8 tevscale, u8 clamp, u8 tevregid);

void 	GX_SetTevColorIn (u8 tevstage, u8 a, u8 b, u8 c, u8 d);
void 	GX_SetTevColorOp (u8 tevstage, u8 tevop, u8 tevbias, u8 tevscale, u8 clamp, u8 tevregid);

void 	GX_SetTevColorS10 (u8 tev_regid, GXColorS10 color);
void 	GX_SetTevKColor (u8 tev_regid, GXColor color);

void 	GX_SetNumTevStages (u8 num);

#endif
