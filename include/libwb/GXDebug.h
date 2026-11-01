/*
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

#ifndef WB_GX_DEBUG_H_
#define WB_GX_DEBUG_H_

#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>

#if defined(__GNUC__) || defined(__clang__)
#define WB_PRINTF_LIKE(fmt_index, args_index) __attribute__((format(printf, fmt_index, args_index)))
#else
#define WB_PRINTF_LIKE(fmt_index, args_index)
#endif

namespace wb::gxdebug
{

enum Flags : uint32_t
{
	TRACE_GX      = 1u << 0,
	TRACE_DRAW    = 1u << 1,
	TRACE_GLSL    = 1u << 2,
	TRACE_TEXTURE = 1u << 3,
	TRACE_ALL     = TRACE_GX | TRACE_DRAW | TRACE_GLSL | TRACE_TEXTURE,
	HEADLESS      = 1u << 8,
};

inline uint32_t g_flags = 0;

inline void SetFlags(uint32_t flags) { g_flags = flags; }
inline uint32_t GetFlags() { return g_flags; }
inline bool Headless() { return (g_flags & HEADLESS) != 0; }
inline bool Tracing(uint32_t what) { return (g_flags & what) != 0; }

using Sink = void (*)(const std::string& line, void* user);
void SetSink(Sink sink, void* user);

void Log(const char* fmt, ...) WB_PRINTF_LIKE(1, 2);
void TraceGx(const char* function, const char* fmt, ...) WB_PRINTF_LIKE(2, 3);
void Print(std::ostream& os, const char* fmt, ...) WB_PRINTF_LIKE(2, 3);
std::string Format(const char* fmt, ...) WB_PRINTF_LIKE(1, 2);

struct Stats
{
	std::map<std::string, uint64_t> gx_calls;

	uint64_t shaders_generated = 0;
	uint64_t shaders_reused = 0;

	uint64_t textures_decoded = 0;
	uint64_t texture_decode_failures = 0;

	uint64_t panes_drawn = 0;
	uint64_t panes_skipped = 0;
	uint64_t quads = 0;
	uint64_t glyphs = 0;
	uint64_t missing_glyphs = 0;
};

Stats& GetStats();
void ResetStats();
void PrintStats(std::ostream& os);

void DumpWrapGxState(std::ostream& os);

namespace names
{
	std::string TevColorArg(unsigned v);
	std::string TevAlphaArg(unsigned v);
	std::string TevOp(unsigned v, bool alpha);
	std::string TevBias(unsigned v);
	std::string TevScale(unsigned v);
	std::string TevReg(unsigned v);
	std::string TevKSel(unsigned v);
	std::string TevOrderChannel(unsigned v);
	std::string TexMap(unsigned v);
	std::string TexCoord(unsigned v);
	std::string TexFormat(unsigned v);
	std::string TlutFormat(unsigned v);
	std::string Wrap(unsigned v);
	std::string TexFilter(unsigned v);
	std::string BlendType(unsigned v);
	std::string BlendFactor(unsigned v, bool source);
	std::string LogicOp(unsigned v);
	std::string Compare(unsigned v);
	std::string AlphaOp(unsigned v);
	std::string TexGenType(unsigned v);
	std::string TexGenSrc(unsigned v);
	std::string TexMtx(unsigned v);
	std::string ChannelSource(unsigned v);
	std::string IndFormat(unsigned v);
	std::string IndBias(unsigned v);
	std::string IndMtx(unsigned v);
	std::string IndWrap(unsigned v);
	std::string IndScale(unsigned v);
	std::string IndAlpha(unsigned v);

	std::string SwapTable(uint8_t packed);

	std::string Color(unsigned r, unsigned g, unsigned b, unsigned a);
}

std::string ToUtf8(const std::u16string& text);

}

#endif
