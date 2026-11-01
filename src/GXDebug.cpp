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

#include "../include/libwb/GXDebug.h"

#include <cstdarg>
#include <cstdio>
#include <iostream>

namespace wb::gxdebug
{

namespace
{
	Sink g_sink = nullptr;
	void* g_sink_user = nullptr;

	Stats g_stats;

	std::string VFormat(const char* fmt, va_list args)
	{
		va_list copy;
		va_copy(copy, args);
		const int length = std::vsnprintf(nullptr, 0, fmt, copy);
		va_end(copy);

		if (length < 0)
			return std::string();

		std::string result(static_cast<size_t>(length), '\0');
		std::vsnprintf(&result[0], static_cast<size_t>(length) + 1, fmt, args);
		return result;
	}

	void Emit(std::string line)
	{
		while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
			line.pop_back();

		if (g_sink)
			g_sink(line, g_sink_user);
		else
			std::cout << line << '\n';
	}

	std::string Unknown(unsigned v)
	{
		return "?(" + std::to_string(v) + ")";
	}

	template <size_t N>
	std::string Pick(const char* const (&table)[N], unsigned v)
	{
		return v < N ? std::string(table[v]) : Unknown(v);
	}
}

void SetSink(Sink sink, void* user)
{
	g_sink = sink;
	g_sink_user = user;
}

void Log(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	std::string line = VFormat(fmt, args);
	va_end(args);

	Emit(std::move(line));
}

void TraceGx(const char* function, const char* fmt, ...)
{
	++g_stats.gx_calls[function];

	va_list args;
	va_start(args, fmt);
	std::string text = VFormat(fmt, args);
	va_end(args);

	Emit(std::string("[gx] ") + function + "(" + text + ")");
}

void Print(std::ostream& os, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	const std::string text = VFormat(fmt, args);
	va_end(args);

	os << text;
}

std::string Format(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	std::string text = VFormat(fmt, args);
	va_end(args);

	return text;
}

Stats& GetStats()
{
	return g_stats;
}

void ResetStats()
{
	g_stats = Stats();
}

void PrintStats(std::ostream& os)
{
	Print(os, "renderer: %llu panes drawn, %llu panes skipped (hidden/invisible), %llu quads, %llu glyphs (%llu missing)\n",
		(unsigned long long)g_stats.panes_drawn, (unsigned long long)g_stats.panes_skipped,
		(unsigned long long)g_stats.quads, (unsigned long long)g_stats.glyphs,
		(unsigned long long)g_stats.missing_glyphs);

	Print(os, "shaders: %llu generated, %llu reused\n",
		(unsigned long long)g_stats.shaders_generated, (unsigned long long)g_stats.shaders_reused);

	Print(os, "textures: %llu decoded, %llu failed to decode\n",
		(unsigned long long)g_stats.textures_decoded, (unsigned long long)g_stats.texture_decode_failures);

	if (g_stats.gx_calls.empty())
		return;

	uint64_t total = 0;
	for (const auto& call : g_stats.gx_calls)
		total += call.second;

	Print(os, "GX calls (%llu total):\n", (unsigned long long)total);
	for (const auto& call : g_stats.gx_calls)
		Print(os, "  %-28s %llu\n", call.first.c_str(), (unsigned long long)call.second);
}

std::string ToUtf8(const std::u16string& text)
{
	std::string out;
	out.reserve(text.size());

	for (size_t i = 0; i != text.size(); ++i)
	{
		uint32_t code = text[i];

		if (code >= 0xD800 && code <= 0xDBFF && i + 1 != text.size() &&
			text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
		{
			code = 0x10000 + ((code - 0xD800) << 10) + (uint32_t(text[i + 1]) - 0xDC00);
			++i;
		}

		if (code < 0x80)
		{
			out.push_back(static_cast<char>(code));
		}
		else if (code < 0x800)
		{
			out.push_back(static_cast<char>(0xC0 | (code >> 6)));
			out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		}
		else if (code < 0x10000)
		{
			out.push_back(static_cast<char>(0xE0 | (code >> 12)));
			out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		}
		else
		{
			out.push_back(static_cast<char>(0xF0 | (code >> 18)));
			out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		}
	}

	return out;
}

namespace names
{

std::string TevColorArg(unsigned v)
{
	static const char* const table[] =
	{
		"CPREV", "APREV", "C0", "A0", "C1", "A1", "C2", "A2",
		"TEXC", "TEXA", "RASC", "RASA", "ONE", "HALF", "KONST", "ZERO",
	};
	return Pick(table, v);
}

std::string TevAlphaArg(unsigned v)
{
	static const char* const table[] =
	{
		"APREV", "A0", "A1", "A2", "TEXA", "RASA", "KONST", "ZERO",
	};
	return Pick(table, v);
}

std::string TevOp(unsigned v, bool alpha)
{
	switch (v)
	{
	case 0:  return "ADD";
	case 1:  return "SUB";
	case 8:  return "COMP_R8_GT";
	case 9:  return "COMP_R8_EQ";
	case 10: return "COMP_GR16_GT";
	case 11: return "COMP_GR16_EQ";
	case 12: return "COMP_BGR24_GT";
	case 13: return "COMP_BGR24_EQ";
	case 14: return alpha ? "COMP_A8_GT" : "COMP_RGB8_GT";
	case 15: return alpha ? "COMP_A8_EQ" : "COMP_RGB8_EQ";
	default: return Unknown(v);
	}
}

std::string TevBias(unsigned v)
{
	static const char* const table[] = { "ZERO", "ADDHALF", "SUBHALF" };
	return Pick(table, v);
}

std::string TevScale(unsigned v)
{
	static const char* const table[] = { "x1", "x2", "x4", "/2" };
	return Pick(table, v);
}

std::string TevReg(unsigned v)
{
	static const char* const table[] = { "PREV", "REG0", "REG1", "REG2" };
	return Pick(table, v);
}

std::string TevKSel(unsigned v)
{
	static const char* const fractions[] = { "ONE", "7/8", "3/4", "5/8", "1/2", "3/8", "1/4", "1/8" };

	if (v < 8)
		return fractions[v];

	if (v >= 0x0C && v <= 0x0F)
		return "K" + std::to_string(v - 0x0C);

	if (v >= 0x10 && v <= 0x1F)
	{
		static const char channels[] = { 'R', 'G', 'B', 'A' };
		return "K" + std::to_string((v - 0x10) % 4) + "_" + channels[(v - 0x10) / 4];
	}

	return Unknown(v);
}

std::string TevOrderChannel(unsigned v)
{
	static const char* const table[] =
	{
		"COLOR0", "COLOR1", "ALPHA0", "ALPHA1", "COLOR0A0", "COLOR1A1",
		"COLORZERO", "ALPHA_BUMP", "ALPHA_BUMPN",
	};

	if (v == 0xFF)
		return "NULL";

	return Pick(table, v);
}

std::string TexMap(unsigned v)
{
	if (v == 0xFF)
		return "NULL";

	if (v < 8)
		return "MAP" + std::to_string(v);

	if (v >= 0x100 && v < 0x108)
		return "MAP" + std::to_string(v - 0x100) + "|DISABLE";

	return Unknown(v);
}

std::string TexCoord(unsigned v)
{
	if (v == 0xFF)
		return "NULL";

	if (v < 8)
		return "COORD" + std::to_string(v);

	return Unknown(v);
}

std::string TexFormat(unsigned v)
{
	switch (v)
	{
	case 0:  return "I4";
	case 1:  return "I8";
	case 2:  return "IA4";
	case 3:  return "IA8";
	case 4:  return "RGB565";
	case 5:  return "RGB5A3";
	case 6:  return "RGBA8";
	case 8:  return "C4";
	case 9:  return "C8";
	case 10: return "C14X2";
	case 14: return "CMPR";
	default: return Unknown(v);
	}
}

std::string TlutFormat(unsigned v)
{
	static const char* const table[] = { "IA8", "RGB565", "RGB5A3" };
	return Pick(table, v);
}

std::string Wrap(unsigned v)
{
	static const char* const table[] = { "CLAMP", "REPEAT", "MIRROR" };
	return Pick(table, v);
}

std::string TexFilter(unsigned v)
{
	static const char* const table[] =
	{
		"NEAR", "LINEAR", "NEAR_MIP_NEAR", "LIN_MIP_NEAR", "NEAR_MIP_LIN", "LIN_MIP_LIN",
	};
	return Pick(table, v);
}

std::string BlendType(unsigned v)
{
	static const char* const table[] = { "NONE", "BLEND", "LOGIC", "SUBTRACT" };
	return Pick(table, v);
}

std::string BlendFactor(unsigned v, bool source)
{
	// GX uses the same numbers for different things on each side:
	// 2 and 3 are SRCCLR/INVSRCCLR for the destination factor, but
	// DSTCLR/INVDSTCLR for the source factor
	static const char* const src[] =
	{
		"ZERO", "ONE", "DSTCLR", "INVDSTCLR", "SRCALPHA", "INVSRCALPHA", "DSTALPHA", "INVDSTALPHA",
	};
	static const char* const dst[] =
	{
		"ZERO", "ONE", "SRCCLR", "INVSRCCLR", "SRCALPHA", "INVSRCALPHA", "DSTALPHA", "INVDSTALPHA",
	};

	return source ? Pick(src, v) : Pick(dst, v);
}

std::string LogicOp(unsigned v)
{
	static const char* const table[] =
	{
		"CLEAR", "AND", "REVAND", "COPY", "INVAND", "NOOP", "XOR", "OR",
		"NOR", "EQUIV", "INV", "REVOR", "INVCOPY", "INVOR", "NAND", "SET",
	};
	return Pick(table, v);
}

std::string Compare(unsigned v)
{
	static const char* const table[] =
	{
		"NEVER", "LESS", "EQUAL", "LEQUAL", "GREATER", "NEQUAL", "GEQUAL", "ALWAYS",
	};
	return Pick(table, v);
}

std::string AlphaOp(unsigned v)
{
	static const char* const table[] = { "AND", "OR", "XOR", "XNOR" };
	return Pick(table, v);
}

std::string TexGenType(unsigned v)
{
	if (v == 0)
		return "MTX3x4";
	if (v == 1)
		return "MTX2x4";
	if (v >= 2 && v <= 9)
		return "BUMP" + std::to_string(v - 2);
	if (v == 10)
		return "SRTG";

	return Unknown(v);
}

std::string TexGenSrc(unsigned v)
{
	static const char* const table[] =
	{
		"POS", "NRM", "BINRM", "TANGENT",
		"TEX0", "TEX1", "TEX2", "TEX3", "TEX4", "TEX5", "TEX6", "TEX7",
	};

	if (v == 21)
		return "COLOR0";
	if (v == 22)
		return "COLOR1";

	return Pick(table, v);
}

std::string TexMtx(unsigned v)
{
	if (v == 60)
		return "IDENTITY";

	if (v >= 30 && v <= 57 && (v - 30) % 3 == 0)
		return "TEXMTX" + std::to_string((v - 30) / 3);

	return Unknown(v);
}

std::string ChannelSource(unsigned v)
{
	return v == 0 ? "REG" : v == 1 ? "VTX" : Unknown(v);
}

std::string IndFormat(unsigned v)
{
	static const char* const table[] = { "ITF_8", "ITF_5", "ITF_4", "ITF_3" };
	return Pick(table, v);
}

std::string IndBias(unsigned v)
{
	static const char* const table[] =
	{
		"NONE", "S", "T", "ST", "U", "SU", "TU", "STU",
	};
	return Pick(table, v);
}

std::string IndMtx(unsigned v)
{
	switch (v)
	{
	case 0:  return "OFF";
	case 1:  return "ITM_0";
	case 2:  return "ITM_1";
	case 3:  return "ITM_2";
	case 5:  return "ITM_S0";
	case 6:  return "ITM_S1";
	case 7:  return "ITM_S2";
	case 9:  return "ITM_T0";
	case 10: return "ITM_T1";
	case 11: return "ITM_T2";
	default: return Unknown(v);
	}
}

std::string IndWrap(unsigned v)
{
	static const char* const table[] = { "OFF", "256", "128", "64", "32", "16", "0" };
	return Pick(table, v);
}

std::string IndScale(unsigned v)
{
	static const char* const table[] = { "1", "2", "4", "8", "16", "32", "64", "128", "256" };
	return Pick(table, v);
}

std::string IndAlpha(unsigned v)
{
	static const char* const table[] = { "OFF", "S", "T", "U" };
	return Pick(table, v);
}

std::string SwapTable(uint8_t packed)
{
	static const char channels[] = { 'r', 'g', 'b', 'a' };

	std::string out;
	for (unsigned i = 0; i != 4; ++i)
		out.push_back(channels[(packed >> (i * 2)) & 3]);

	return out;
}

std::string Color(unsigned r, unsigned g, unsigned b, unsigned a)
{
	return Format("#%02X%02X%02X%02X", r & 0xFF, g & 0xFF, b & 0xFF, a & 0xFF);
}

}

}
