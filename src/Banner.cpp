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

#include <fstream>
#include <cstdlib>
#include <vector>

#include <GL/glew.h>

// hax
#define WIN32_LEAN_AND_MEAN
#define _WINUSER_
// from dolphin
#include "FileHandlerARC.h"

#include "Banner.h"
#include "LZ77.h"
#include "Sound.h"
#include "Endian.h"
#include "Types.h"

namespace WiiBanner
{

enum BinaryMagic : u32
{
	BINARY_MAGIC_U8_ARCHIVE = MAKE_FOURCC('U', 0xAA, '8', '-'),

	BINARY_MAGIC_ANIMATION = MAKE_FOURCC('R', 'L', 'A', 'N'),
	BINARY_MAGIC_PANE_ANIMATION_INFO = MAKE_FOURCC('p', 'a', 'i', '1')
};

	bool Banner::is_valid(const std::string& filename) {
		std::ifstream bnr_file(filename, std::ios::binary | std::ios::in);
		// opening.bnr  archives have 0x600 byte headers
		// 00000000.app archives have 0x640 byte headers
		auto header_bytes = 0x600;

		bnr_file.seekg(header_bytes, std::ios::cur);

		// lets see if this is an opening.bnr
		FourCC magic;
		bnr_file >> magic;
		if (magic != BINARY_MAGIC_U8_ARCHIVE) {
			// lets see if it's a 00000000.app
			bnr_file.seekg(60, std::ios::cur);
			bnr_file >> magic;

			if (magic != BINARY_MAGIC_U8_ARCHIVE)
				return false;

			header_bytes = 0x640;
		}

		return true;
	}

Banner::Banner(const std::string& _filename, const std::string& _font_archive)
	: layout_banner(nullptr)
	, layout_icon(nullptr)
	, filename(_filename)
	, font_archive(_font_archive)
{
	std::ifstream bnr_file(filename, std::ios::binary | std::ios::in);

	// opening.bnr  archives have 0x600 byte headers
	// 00000000.app archives have 0x640 byte headers
	header_bytes = 0x600;

	bnr_file.seekg(header_bytes, std::ios::cur);

	// lets see if this is an opening.bnr
	FourCC magic;
	bnr_file >> magic;
	if (magic != BINARY_MAGIC_U8_ARCHIVE)
	{
		// lets see if it's a 00000000.app
		bnr_file.seekg(60, std::ios::cur);
		bnr_file >> magic;

		if (magic != BINARY_MAGIC_U8_ARCHIVE)
			return;	// not a 00000000.app either

		header_bytes = 0x640;
	}

	header_bytes += 32;	// the inner-files have bigger headers

	bnr_file.seekg(-4, std::ios::cur);
	DiscIO::CARCFile opening_arc(bnr_file);

	offset_banner = opening_arc.GetFileOffset("meta/" "banner" ".bin");
	offset_icon = opening_arc.GetFileOffset("meta/" "icon" ".bin");
	offset_sound = opening_arc.GetFileOffset("meta/" "sound" ".bin");
}

void Banner::LoadBanner()
{
	if (offset_banner && !layout_banner)
		layout_banner = LoadLayout("Banner", offset_banner, Vec2f(608.f, 456.f));
}

/* defined in header
void Banner::LoadIcon()
{
	if (offset_icon && !layout_icon)
		layout_icon = LoadLayout("Icon", offset_icon, Vec2f(128.f, 96.f));
}
*/

void Banner::LoadSound()
{
    std::cout << "offset_sound = " << offset_sound << "\n";

    if (offset_sound && !sound)
    {
        std::ifstream bnr_file(filename, std::ios::binary | std::ios::in);

        if (!bnr_file)
        {
            std::cerr << "Failed to open banner file\n";
            return;
        }

        bnr_file.seekg(header_bytes + offset_sound, std::ios::beg);

        std::cout << "Loading sound at offset "
                  << header_bytes + offset_sound << "\n";

        auto* const s = new Sound;

        if (s->Load(bnr_file))
        {
            std::cout << "Sound loaded\n";
            sound = s;
        }
        else
        {
            delete s;
            std::cerr << "s->Load() failed\n";
        }
    }
    else
    {
        std::cout << "No sound offset or already loaded\n";
    }
}

Layout* Banner::LoadLayout(const std::string& lyt_name, std::streamoff offset, Vec2f size)
{
	std::ifstream bnr_file(filename, std::ios::binary | std::ios::in);

	bnr_file.seekg(header_bytes + offset, std::ios::beg);

	// LZ77 decompress .bin file
	LZ77Decompressor decomp(bnr_file);
	std::istream& file = decomp.GetStream();

	DiscIO::CARCFile bin_arc(file);
	const auto brlyt_offset = bin_arc.GetFileOffset("arc/blyt/" + lyt_name + ".brlyt");

	if (0 == brlyt_offset)
		return nullptr;

	file.seekg(brlyt_offset, std::ios::beg);
	auto* const layout = new Layout;
	layout->Load(file);

	// override size
	layout->SetWidth(size.x);
	layout->SetHeight(size.y);

	// load animations
	FrameNumber length_start = 0, length_loop = 0;

	auto brlan_start_offset = bin_arc.GetFileOffset("arc/anim/" + lyt_name + "_Start.brlan");

	// alt. start file
	if (!brlan_start_offset) {
		brlan_start_offset =
				bin_arc.GetFileOffset("arc/anim/" + lyt_name + "_In.brlan");
	}

	if (brlan_start_offset) {
		file.seekg(brlan_start_offset, std::ios::beg);
		length_start = Animator::LoadAnimators(file, *layout, 0);
	}

	auto brlan_loop_offset = bin_arc.GetFileOffset("arc/anim/" + lyt_name + ".brlan");
	if (!brlan_loop_offset) {
		brlan_loop_offset = bin_arc.GetFileOffset("arc/anim/" + lyt_name + "_Loop.brlan");
	}
	if (!brlan_loop_offset) {
		brlan_loop_offset = bin_arc.GetFileOffset("arc/anim/" + lyt_name + "_Rso0.brlan");
	}

	if (brlan_loop_offset) {
		file.seekg(brlan_loop_offset, std::ios::beg);
		length_loop = Animator::LoadAnimators(file, *layout, 1);
	}

	// load textures
	for (Texture* texture : layout->resources.textures) {
		auto const texture_offset = bin_arc.GetFileOffset("arc/timg/" + texture->GetName());
		std::cout << "Loading texture: "
		  << texture->GetName()
		  << " offset="
		  << texture_offset
		  << "\n";
		if (texture_offset)
		{
			file.seekg(texture_offset, std::ios::beg);
			texture->Load(file);
		}
	}

	std::cout << "textures:\n";
	for (auto* tex : layout->resources.textures)
		std::cout << "  " << tex->GetName() << "\n";

	// Load banner-local fonts first, then fall back to the Wii shared font
	// archive for the two system fonts used by channel banners.
	for (Font* font : layout->resources.fonts)
	{
		const auto embedded_font_offset =
			bin_arc.GetFileOffset("arc/font/" + font->GetName());
		if (embedded_font_offset)
		{
			file.clear();
			file.seekg(embedded_font_offset, std::ios::beg);
			if (font->Load(file))
			{
				std::cout << "Loaded font: " << font->GetName()
					<< " (banner)\n";
				continue;
			}
		}

		std::string archive_name = font->GetName();
		if (archive_name == "RevoIpl_RodinNTLGPro_DB_32_I4.brfnt")
			archive_name = "wbf1.brfna";
		else if (archive_name == "RevoIpl_UtrilloProGrecoStd_M_32_I4.brfnt")
			archive_name = "wbf2.brfna";

		std::vector<std::string> archive_candidates;
		if (!font_archive.empty())
		{
			archive_candidates.push_back(font_archive);
		}
		else if (const char* environment_archive = std::getenv("WII_FONT_ARCHIVE"))
		{
			archive_candidates.emplace_back(environment_archive);
		}
		else
		{
			archive_candidates.emplace_back("00000003.app");
			archive_candidates.emplace_back("00000011.app");
		}

		for (const std::string& archive_path : archive_candidates)
		{
			std::ifstream font_file(archive_path, std::ios::binary | std::ios::in);
			if (!font_file)
				continue;

			DiscIO::CARCFile font_arc(font_file);
			const auto font_offset = font_arc.GetFileOffset(archive_name);
			if (!font_offset)
				continue;

			font_file.clear();
			font_file.seekg(font_offset, std::ios::beg);
			if (font->Load(font_file))
			{
				std::cout << "Loaded font: " << font->GetName()
					<< " (" << archive_path << ")\n";
				break;
			}
		}

		if (!font->IsLoaded())
			std::cerr << "Unable to load font: " << font->GetName() << '\n';
	}

	layout->SetLoopStart(length_start);
	layout->SetLoopEnd(length_start + length_loop);
	// update everything for frame 0
	layout->SetFrame(0);

	return layout;
}

Banner::~Banner()
{
	UnloadBanner();
	UnloadIcon();
	//UnloadSound();
}

}
