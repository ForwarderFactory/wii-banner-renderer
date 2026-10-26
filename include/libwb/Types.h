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

#ifndef WII_BNR_TYPES_H_
#define WII_BNR_TYPES_H_

#include <string>
#include <iostream>
#include <algorithm>
#include <type_traits>
#include <cstdint>

namespace wb {
	template <typename T>
	struct Vec2
	{
		Vec2() = default;

		Vec2(T _x, T _y) : x(_x), y(_y) {}

		bool operator!=(const Vec2& rhs) const
		{
			return x != rhs.x || y != rhs.y;
		}

		bool operator==(const Vec2& rhs) const
		{
			return !(*this != rhs);
		}

		T x, y;
	};

	typedef Vec2<float> Vec2f;
	typedef Vec2<int> Vec2i;

	template <typename T>
	struct Vec3
	{
		T x, y, z;
	};

	typedef Vec3<float> Vec3f;
	typedef Vec3<int> Vec3i;

	class FourCC
	{
		friend std::istream& operator>>(std::istream& lhs, FourCC& rhs);
		friend std::ostream& operator<<(std::ostream& lhs, const FourCC& rhs);
	public:
		FourCC() {}
		FourCC(uint32_t _data) { data = _data; }

		bool operator==(uint32_t rhs) const
		{
			return data == rhs;
		}

		bool operator!=(uint32_t rhs) const
		{
			return !(*this == rhs);
		}

	private:
		uint32_t data;
	};

#define MAKE_FOURCC(a, b, c, d) ((a) * (1 << 24) + (b) * (1 << 16) + (c) * (1 << 8) + (d) * (1 << 0))

	class Named
	{
	public:
		const std::string& GetName() const { return name; }
		void SetName(const std::string& _name) { name = _name; }

	protected:
		std::string name;
	};
}

#endif
