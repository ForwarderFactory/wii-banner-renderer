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

#include "../include/libwb/Banner.h"
#include "../include/libwb/LZ77.h"
#include "../include/libwb/Sound.h"
#include "../include/libwb/Endian.h"
#include "../include/libwb/Types.h"
#include <libwb/U8.h>

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

std::string FirstPresent(const u8archive::Archive& arc,
                         const std::vector<std::string>& paths)
{
   for (size_t i = 0; i != paths.size(); ++i)
      if (arc.FindFile(paths[i]) >= 0)
         return paths[i];

   return std::string();
}

Layout* Banner::LoadLayout(const std::string& lyt_name, std::streamoff offset, Vec2f size)
{
   std::ifstream bnr_file(filename.c_str(), std::ios::binary | std::ios::in);
   if (!bnr_file)
      return nullptr;

   u8archive::Compression codec = u8archive::COMPRESSION_NONE;
   u8archive::Archive bin_arc;
   if (!bin_arc.OpenStream(bnr_file, header_bytes + offset, 0, &codec))
   {
      std::cerr << "Unable to open banner archive at offset " << offset << '\n';
      return nullptr;
   }

   std::cout << lyt_name << ".bin: " << u8archive::CompressionName(codec)
             << ", " << bin_arc.Entries().size() << " entries\n";

   std::vector<uint8_t> brlyt;
   if (!bin_arc.ReadFile("arc/blyt/" + lyt_name + ".brlyt", brlyt))
      return nullptr;

   auto* const layout = new Layout;
   {
      u8archive::MemoryStream in(brlyt);
      layout->Load(in);
   }

   layout->SetWidth(size.x);
   layout->SetHeight(size.y);

   FrameNumber length_start = 0, length_loop = 0;

   std::vector<std::string> start_names;
   start_names.push_back("arc/anim/" + lyt_name + "_Start.brlan");
   start_names.push_back("arc/anim/" + lyt_name + "_In.brlan");

   const std::string start_path = FirstPresent(bin_arc, start_names);
   if (!start_path.empty())
   {
      std::vector<uint8_t> brlan;
      if (bin_arc.ReadFile(start_path, brlan))
      {
         u8archive::MemoryStream in(brlan);
         length_start = Animator::LoadAnimators(in, *layout, 0);
      }
   }

   std::vector<std::string> loop_names;
   loop_names.push_back("arc/anim/" + lyt_name + ".brlan");
   loop_names.push_back("arc/anim/" + lyt_name + "_Loop.brlan");
   loop_names.push_back("arc/anim/" + lyt_name + "_Rso0.brlan");

   const std::string loop_path = FirstPresent(bin_arc, loop_names);
   if (!loop_path.empty()) {
      std::vector<uint8_t> brlan;
      if (bin_arc.ReadFile(loop_path, brlan)) {
         u8archive::MemoryStream in(brlan);
         length_loop = Animator::LoadAnimators(in, *layout, 1);
      }
   }

   for (Texture* texture : layout->resources.textures) {
      std::vector<uint8_t> tpl;
      if (!bin_arc.ReadFile("arc/timg/" + texture->GetName(), tpl))
      {
         std::cerr << "Missing texture: " << texture->GetName() << '\n';
         continue;
      }

      u8archive::MemoryStream in(tpl);
      texture->Load(in);
      std::cout << "Loaded texture: " << texture->GetName()
                << " (" << tpl.size() << " bytes)\n";
   }

   for (Font* font : layout->resources.fonts) {
      std::vector<uint8_t> brfnt;
      if (bin_arc.ReadFile("arc/font/" + font->GetName(), brfnt))
      {
         u8archive::MemoryStream in(brfnt);
         if (font->Load(in))
         {
            std::cout << "Loaded font: " << font->GetName() << " (banner)\n";
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
         archive_candidates.push_back(environment_archive);
      }
      else
      {
         archive_candidates.push_back("00000003.app");
         archive_candidates.push_back("00000011.app");
      }

      for (const std::string& archive_path : archive_candidates) {
         u8archive::Archive font_arc;
         if (!font_arc.OpenFile(archive_path))
            continue;

         std::vector<uint8_t> font_data;
         if (!font_arc.ReadFile(archive_name, font_data))
            continue;

         u8archive::MemoryStream in(font_data);
         if (font->Load(in))
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
   layout->SetFrame(0);

   return layout;
}

Banner::~Banner()
{
	UnloadBanner();
	UnloadIcon();
	UnloadSound();
}

}
