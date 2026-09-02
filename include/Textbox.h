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

#ifndef WII_BNR_TEXTBOX_H_
#define WII_BNR_TEXTBOX_H_

#include <string>

#include "Pane.h"

namespace WiiBanner
{

class Textbox : public Pane
{
public:
	typedef Pane Base;

	static const u32 BINARY_MAGIC = MAKE_FOURCC('t', 'x', 't', '1');

	void Load(std::istream& file);

private:
	void Draw(const Resources& resources, u8 render_alpha) const override;
	void ProcessHermiteKey(const KeyType& type, float value) override;

	u16 material_index, font_index;

	u8 text_position, text_alignment;

	GXColor colors[2];	// top and bottom apparently

	float font_width, font_height;
	float space_char, space_line;

	std::u16string text;
};

}

#endif
