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
#include "../include/libwb/Endian.h"

#include <cstring>

namespace wb {
	BEStreamManip BE;
	LEStreamManip LE;

	template <>
	void SwapData<1>(uint8_t* data)
	{
		// nothing
	}

	template <>
	void SwapData<2>(uint8_t* data)
	{
		std::reverse(data, data + 2);
	}

	template <>
	void SwapData<4>(uint8_t* data)
	{
		std::reverse(data, data + 4);
	}

	template <>
	void SwapData<8>(uint8_t* data)
	{
		std::reverse(data, data + 8);
	}

	std::ostream& operator<<(std::ostream& lhs, const FourCC& rhs)
	{
		uint8_t bytes[4];
		std::memcpy(bytes, &rhs.data, 4);
		std::reverse(bytes, bytes + 4);
		lhs.write(reinterpret_cast<const char*>(bytes), 4);
		return lhs;
	}
}
