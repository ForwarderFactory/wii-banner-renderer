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

// from dolphin
#include "CommonFuncs.h"

#include "../include/libwb/Endian.h"

namespace wb {
	BEStreamManip BE;
	LEStreamManip LE;

	template <>
	void SwapData<1>(u8* data)
	{
		// nothing
	}

	template <>
	void SwapData<2>(u8* data)
	{
		*reinterpret_cast<u16*>(data) = Common::swap16(data);
	}

	template <>
	void SwapData<4>(u8* data)
	{
		*reinterpret_cast<u32*>(data) = Common::swap32(data);
	}

	template <>
	void SwapData<8>(u8* data)
	{
		*reinterpret_cast<u64*>(data) = Common::swap64(data);
	}

	std::ostream& operator<<(std::ostream& lhs, const FourCC& rhs)
	{
		const u32 data = Common::swap32(rhs.data);
		lhs.write(reinterpret_cast<const char*>(&data), 4);
		return lhs;
	}
}