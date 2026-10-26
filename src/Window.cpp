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

#include <GL/glew.h>

#include "../include/libwb/Window.h"
#include "../include/libwb/Endian.h"

namespace wb
{

void Window::Load(std::istream& file)
{
	const std::streamoff section_start = file.tellg() - (std::streamoff)8;	// 8 being size of section header :/

	Pane::Load(file);

	file >> BE >> inflation.l >> inflation.r >> inflation.t >> inflation.b;

	uint8_t frame_count;

	file >> BE >> frame_count;
	file.seekg(3, std::ios::cur);

	uint32_t content_offset, frame_table_offset;

	file >> BE >> content_offset >> frame_table_offset;

	// read content
	file.seekg(section_start + content_offset, std::ios::beg);
	Quad::Load(file);

	// read frames
	file.seekg(section_start + frame_table_offset, std::ios::beg);
	ReadOffsetList<uint32_t>(file, frame_count, file.tellg(), [&]
	{
		Frame frame;
		file >> BE >> frame.material_index >> frame.texture_flip;

		frames.push_back(std::move(frame));
	});
}

void Window::Draw(const Resources& resources, uint8_t render_alpha) const
{
	// TODO: handle "inflation"
	// TODO: handle "frames" and "texture_flip"

	Quad::Draw(resources, render_alpha);
}

}
