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

#ifndef WII_BNR_TEXTURE_H_
#define WII_BNR_TEXTURE_H_

#include <vector>

#include <libwb/WrapGx.h>
#include <libwb/Types.h>

namespace wb
{

class Texture : public Named
{
public:
	Texture() : img_ptr(nullptr), tlut_ptr(nullptr), tlut_name(0) {}
	~Texture();

	void Load(std::istream& file);
	//[[nodiscard]] const std::string &getName() const { return name; }
	//void setName(const std::string& _name) { name = _name; }
	void Apply(uint8_t &tlutName, uint8_t map_id, uint8_t wrap_s, uint8_t wrap_t) const;

	GXTexObj texobj;
private:
	//std::string name;
	char* img_ptr;
	char* tlut_ptr;
	uint32_t tlut_name;
	uint32_t tlut_format{};
	uint16_t tlut_count{};
};

class TextureList : public std::vector<Texture*>
{
public:
	static constexpr uint32_t BINARY_MAGIC = MAKE_FOURCC('t', 'x', 'l', '1');
};

}

#endif
