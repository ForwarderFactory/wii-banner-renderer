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

#ifndef WII_BNR_FUNCS_H_
#define WII_BNR_FUNCS_H_

#include "Endian.h"

template <typename C, typename F>
inline void ForEach(C& container, F func)
{
	std::for_each(container.begin(), container.end(), func);
}

template <typename T>
inline T Clamp(T value, T min, T max)
{
	return (value < min) ? min : (value < max) ? value : max;
}

template <typename T, typename B>
inline T RoundUp(T value, B base)
{
	return static_cast<T>((value + (base - 1)) & ~(base - 1));
}

template <typename T, typename B>
inline T RoundDown(T value, B base)
{
	return static_cast<T>(value & ~(base - 1));
}

template <typename C, typename F>
void ReadSections(std::istream& file, C count, F func)
{
	while (count--)
	{
		const std::streamoff start = file.tellg();

		FourCC magic;
		uint32_t size = 0;
		file >> magic >> BE >> size;

		func(magic, start);

		file.seekg(start + size, std::ios::beg);
	}
}

template <typename O, typename C, typename F>
void ReadOffsetList(std::istream& file, C count, std::streamoff origin, F func, std::streamoff pad = 0)
{
	std::streamoff next_offset = file.tellg();

	while (count--)
	{
		file.seekg(next_offset, std::ios::beg);

		O offset;
		file >> BE >> offset;
		file.seekg(pad, std::ios::cur);

		next_offset = file.tellg();

		file.seekg(origin + offset, std::ios::beg);
		func();
	}

	file.seekg(next_offset, std::ios::beg);
}

inline std::string ReadNullTerminatedString(std::istream& file)
{
	std::string str;
	std::getline(file, str, '\0');
	return str;
}

template <int L>
std::string ReadFixedLengthString(std::istream& file)
{
	char str[L + 1] = {};
	file.read(str, L);
	return str;
}

inline void WriteNullTerminatedString(std::ostream& file, const std::string& str)
{
	file << str << '\0';
}

template <int L>
inline void WriteFixedLengthString(std::ostream& file, const std::string& _str)
{
	char str[L] = {};
	std::copy(_str.begin(), (_str.length() > L) ? _str.begin() + L : _str.end(), str);
	file.write(str, L);
}

template <typename B, typename N>
inline void SetBit(B& flags, N bit, bool state)
{
	if (state)
		flags |= (1 << bit);
	else
		flags &= ~(1 << bit);
}

template <typename B, typename N>
inline bool GetBit(const B& flags, N bit)
{
	return !!(flags & (1 << bit));
}

// multiply 2 color components
// assumes uint8_ts, takes any type to avoid multiple conversions
template <typename C1, typename C2>
inline uint8_t MultiplyColors(C1 c1, C2 c2)
{
	return (uint16_t)c1 * c2 / 0xff;
}

#endif
