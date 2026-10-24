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

#include <stack>

#include <GL/glew.h>

#include <libwb/Layout.h>
#include <libwb/Picture.h>
#include <libwb/Textbox.h>
#include <libwb/Window.h>
#include <libwb/Endian.h>

#include <cctype>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace WiiBanner
{

enum BinaryMagic : uint32_t
{
	BINARY_MAGIC_LAYOUT = MAKE_FOURCC('R', 'L', 'Y', 'T'),

	BINARY_MAGIC_PANE_PUSH = MAKE_FOURCC('p', 'a', 's', '1'),
	BINARY_MAGIC_PANE_POP = MAKE_FOURCC('p', 'a', 'e', '1'),
	BINARY_MAGIC_GROUP_PUSH = MAKE_FOURCC('g', 'r', 's', '1'),
	BINARY_MAGIC_GROUP_POP = MAKE_FOURCC('g', 'r', 'e', '1')
};

template <typename P>
Pane* LoadNewPane(std::istream& file)
{
	P* const pane = new P;
	pane->Load(file);
	return pane;
}

void Layout::Load(std::istream& file)
{
	const std::streamoff file_start = file.tellg();

	frame_current = frame_loop_start = frame_loop_end = 0.f;
	width = height = 0.f;
	centered = 0;

	// read header
	FourCC header_magic;
	uint16_t endian;
	uint16_t version;
	uint32_t filesize;
	uint16_t first_section_offset; // offset to first section
	uint16_t section_count;

	file >> header_magic >> BE >> endian >> version
		>> filesize >> first_section_offset >> section_count;

	if (header_magic != BINARY_MAGIC_LAYOUT
		|| endian != 0xFEFF
		|| version != 0x0008
		)
		return;	// bad header

	// temporary stacks
	Group* last_group = nullptr;
	std::stack<std::map<std::string, Group>*> group_stack;
	group_stack.push(&groups);

	Pane* last_pane = nullptr;
	std::stack<std::vector<Pane*>*> pane_stack;
	pane_stack.push(&panes);

	std::stack<Pane*> pane_owner_stack;

	auto const add_pane = [&](Pane* pane)
	{
		pane_stack.top()->push_back(last_pane = pane);
	};

	// seek to the first section
	file.seekg(file_start + first_section_offset, std::ios::beg);

	ReadSections(file, section_count, [&](FourCC magic, std::streamoff section_start)
	{
		if (magic == Layout::BINARY_MAGIC)
		{
			// read layout
			file >> BE >> centered;
			file.seekg(3, std::ios::cur);
			file >> BE >> width >> height;
		}
		else if (magic == TextureList::BINARY_MAGIC)
		{
			// load texture list
			uint16_t texture_count;
			uint16_t offset;

			file >> BE >> texture_count >> offset;

			ReadOffsetList<uint32_t>(file, texture_count, file.tellg(), [&]
			{
				auto* const texture = new Texture;
				texture->SetName(ReadNullTerminatedString(file));

				resources.textures.push_back(texture);

			}, 4);

			std::cout << "Loaded " << resources.textures.size() << " Textures\n";
		}
		else if (magic == FontList::BINARY_MAGIC)
		{
			// load font list
			uint16_t font_count;
			uint16_t offset;

			file >> BE >> font_count >> offset;

			ReadOffsetList<uint32_t>(file, font_count, file.tellg(), [&]
			{
				auto* const font = new Font;
				font->SetName(ReadNullTerminatedString(file));

				resources.fonts.push_back(font);

			}, 4);

			std::cout << "Loaded " << resources.fonts.size() << " Fonts\n";
		}
		else if (magic == MaterialList::BINARY_MAGIC)
		{
			// load materials
			uint16_t material_count;
			uint16_t offset;

			file >> BE >> material_count >> offset;

			ReadOffsetList<uint32_t>(file, material_count, section_start, [&]
			{
				auto* const mat = new Material;
				mat->Load(file);
				resources.materials.push_back(mat);
			});

			std::cout << "Loaded " << resources.materials.size() << " Materials\n";
		}
		else if (magic == Pane::BINARY_MAGIC)
		{
			add_pane(LoadNewPane<Pane>(file));
		}
		else if (magic == Bounding::BINARY_MAGIC)
		{
			add_pane(LoadNewPane<Bounding>(file));
		}
		else if (magic == Picture::BINARY_MAGIC)
		{
			add_pane(LoadNewPane<Picture>(file));
		}
		else if (magic == Window::BINARY_MAGIC)
		{
			add_pane(LoadNewPane<Window>(file));
		}
		else if (magic == Textbox::BINARY_MAGIC)
		{
			add_pane(LoadNewPane<Textbox>(file));
		}
		else if (magic == BINARY_MAGIC_PANE_PUSH)
		{
			if (last_pane)
			{
				pane_stack.push(&last_pane->panes);
				pane_owner_stack.push(last_pane);
			}
		}
		else if (magic == BINARY_MAGIC_PANE_POP)
		{
			if (pane_stack.size() > 1)
			{
				pane_stack.pop();

				last_pane = pane_owner_stack.top();
				pane_owner_stack.pop();
			}
		}
		else if (magic == Layout::Group::BINARY_MAGIC)
		{
			Group& group_ref = (*group_stack.top())[ReadFixedLengthString<Layout::Group::NAME_LENGTH>(file)];

			uint16_t sub_count;
			file >> BE >> sub_count;
			file.seekg(2, std::ios::cur);

			while (sub_count--)
			{
				group_ref.panes.push_back(ReadFixedLengthString<Pane::NAME_LENGTH>(file));
			}

			last_group = &group_ref;
		}
		else if (magic == BINARY_MAGIC_GROUP_PUSH)
		{
			if (last_group)
				group_stack.push(&last_group->groups);
		}
		else if (magic == BINARY_MAGIC_GROUP_POP)
		{
			if (group_stack.size() > 1)
				group_stack.pop();
		}
		else
		{
			std::cout << "UNKNOWN SECTION: ";
			std::cout << magic << '\n';
		}
	});
}

Layout::~Layout()
{
	for (Pane* pane : panes)
		delete pane;

	for (Material* material : resources.materials)
		delete material;

	for (Texture* texture : resources.textures)
		delete texture;

	for (Font* font : resources.fonts)
		delete font;
}

void Layout::Render(float zoom, uint8_t render_alpha, bool widescreen) const {
	glPushMatrix();

	glScalef(
		zoom / width,
		-zoom / height,
		1.f
	);

	if (centered) {
		glTranslatef(
			width * 0.5f,
			height * 0.5f,
			0.f
		);
	}

	for (Pane* pane : panes)
	{
		pane->Render(resources, render_alpha, widescreen);
	}

	glPopMatrix();
}

void Layout::DebugTrace(const char* names) const
{
	if (!names)
		return;

	std::set<std::string> wanted;
	{
		std::string current;
		for (const char* c = names;; ++c)
		{
			if (*c == ',' || *c == '\0')
			{
				if (!current.empty())
					wanted.insert(current);
				current.clear();
				if (*c == '\0')
					break;
			}
			else
			{
				current.push_back(*c);
			}
		}
	}

	// absolute position is approximate (ignores rotation and widescreen scale)
	std::function<void(const PaneList&, float, float, float, float)> walk =
		[&](const PaneList& list, float px, float py, float psx, float psy)
	{
		for (Pane* pane : list)
		{
			const float ax = px + psx * pane->GetTranslate().x;
			const float ay = py + psy * pane->GetTranslate().y;

			if (wanted.count(pane->GetName()))
			{
				std::printf("[trace] frame %d '%s' local=(%.1f,%.1f) "
					"scale=(%.2f,%.2f) abs=(%.1f,%.1f) v=%d z=%.1f "
					"rot=(%.1f,%.1f,%.1f) size=%.1fx%.1f flags=0x%02X "
					"origin_raw=%d\n",
					static_cast<int>(frame_current), pane->GetName().c_str(),
					pane->GetTranslate().x, pane->GetTranslate().y,
					pane->GetScale().x, pane->GetScale().y, ax, ay,
					pane->GetVisible() ? 1 : 0, pane->GetTranslate().z,
					pane->GetRotate().x, pane->GetRotate().y, pane->GetRotate().z,
					pane->GetWidth(), pane->GetHeight(),
					static_cast<unsigned>(pane->GetFlags()),
					static_cast<int>(pane->GetOriginRaw()));
			}

			walk(pane->panes, ax, ay, psx * pane->GetScale().x,
				psy * pane->GetScale().y);
		}
	};

	walk(panes, 0.f, 0.f, 1.f, 1.f);
}

void Layout::SetFrame(FrameNumber frame_number)
{
	frame_current = frame_number;

	const uint8_t key_set = (frame_current >= frame_loop_start);
	if (key_set)
		frame_number -= frame_loop_start;

	resources.cur_set = key_set;

	for (Pane* pane : panes)
		pane->SetFrame(frame_number, key_set);

	for (Material* material : resources.materials)
		material->SetFrame(frame_number, key_set);
}

void Layout::AdvanceFrame()
{
	++frame_current;

	if (frame_current >= frame_loop_end)
		frame_current = frame_loop_start;

	SetFrame(frame_current);
}

namespace
{

	struct LanguageGroup
	{
		std::string name;
		const Layout::Group* group;
	};

	void CollectLanguageGroups(
		const std::map<std::string, Layout::Group>& groups,
		bool under_root,
		std::vector<LanguageGroup>& out)
	{
		static const std::set<std::string> known_languages =
		{
			"JPN", "ENG", "GER", "FRA", "SPA", "ITA", "DUT", "KOR"
		};

		for (const auto& entry : groups)
		{
			// some hax, there are some odd "Rso0" "Rso1" groups that shouldn't
			// be hidden; under RootGroup only 3 character names are languages
			const bool is_language = known_languages.count(entry.first) ||
				(under_root && entry.first.length() == 3);

			if (is_language)
				out.push_back({ entry.first, &entry.second });

			CollectLanguageGroups(entry.second.groups,
				entry.first == "RootGroup", out);
		}
	}

	void DumpGroups(const std::map<std::string, Layout::Group>& groups, int depth)
	{
		for (const auto& entry : groups)
		{
			std::printf("[layout] %*sgroup '%s' (%zu panes)\n", depth * 2, "",
				entry.first.c_str(), entry.second.panes.size());
			for (const std::string& pane : entry.second.panes)
				std::printf("[layout] %*s  pane '%s'\n", depth * 2, "", pane.c_str());
			DumpGroups(entry.second.groups, depth + 1);
		}
	}

}

void Layout::SetLanguage(const std::string& language)
{
	/// some debugging possible here
	if (std::getenv("WB_LAYOUT_DEBUG"))
	{
		std::printf("[layout] language '%s', group tree:\n", language.c_str());
		DumpGroups(groups, 0);

		std::printf("[layout] pane tree (name, visible, hide):\n");
		std::function<void(const PaneList&, int)> dump_panes =
			[&](const PaneList& list, int depth)
		{
			for (Pane* pane : list)
			{
				std::printf("[layout] %*s'%s' v=%d h=%d t=(%.1f,%.1f) "
					"s=(%.2f,%.2f) size=%.1fx%.1f origin=(%d,%d)\n", depth * 2, "",
					pane->GetName().c_str(),
					pane->GetVisible() ? 1 : 0, pane->GetHide() ? 1 : 0,
					pane->GetTranslate().x, pane->GetTranslate().y,
					pane->GetScale().x, pane->GetScale().y,
					pane->GetWidth(), pane->GetHeight(),
					pane->GetOriginX(), pane->GetOriginY());
				dump_panes(pane->panes, depth + 1);
			}
		};
		dump_panes(panes, 0);
	}

	std::vector<LanguageGroup> language_groups;
	CollectLanguageGroups(groups, false, language_groups);

	bool found_language_group = false;
	for (const LanguageGroup& entry : language_groups)
	{
		if (entry.name == language)
			found_language_group = true;
	}

	// hide panes of non-matching languages
	for (const LanguageGroup& entry : language_groups)
	{
		if (entry.name == language)
			continue;

		for (const std::string& pane : entry.group->panes)
		{
			if (Pane* const found = FindPane(pane))
				found->SetHide(true);
		}
	}

	if (found_language_group)
	{
		for (const LanguageGroup& entry : language_groups)
		{
			if (entry.name != language)
				continue;

			for (const std::string& pane : entry.group->panes)
			{
				if (Pane* const found = FindPane(pane))
				{
					found->SetHide(false);
					found->SetVisible(true);
				}
			}
		}
		return;
	}

	{
		static const std::map<std::string, char> language_letters =
		{
			{ "JPN", 'J' }, { "ENG", 'E' }, { "GER", 'G' }, { "FRA", 'F' },
			{ "SPA", 'S' }, { "ITA", 'I' }, { "DUT", 'N' },
		};

		const auto selected = language_letters.find(language);
		if (selected != language_letters.end())
		{
			std::vector<std::pair<Pane*, char>> logo_panes;
			std::set<char> letters;

			std::function<void(const PaneList&)> collect =
				[&](const PaneList& list)
			{
				for (Pane* pane : list)
				{
					const std::string& name = pane->GetName();
					if (name.size() >= 9 && name.compare(0, 6, "N_logo") == 0 &&
						name[7] == '_' &&
						std::string("JEGFSIN").find(name[6]) != std::string::npos &&
						std::isdigit(static_cast<unsigned char>(name[8])))
					{
						logo_panes.emplace_back(pane, name[6]);
						letters.insert(name[6]);
					}

					collect(pane->panes);
				}
			};
			collect(panes);

			if (letters.size() >= 2)
			{
				for (auto& entry : logo_panes)
					entry.first->SetHide(entry.second != selected->second);
				return;
			}
		}
	}

	static const std::map<std::string, std::string> legacy_language_panes = {
		{"JPN", "font_j"},
		{"ENG", "font_e"},
		{"GER", "font_g"},
		{"FRA", "font_f"},
		{"SPA", "font_s"},
		{"ITA", "font_i"},
		{"DUT", "font_n"},
	};

	const auto selected_language = legacy_language_panes.find(language);
	if (selected_language == legacy_language_panes.end())
		return;

	unsigned int pane_count = 0;
	for (const auto& language_pane : legacy_language_panes)
	{
		const std::string& pane_name = language_pane.second;
		if (FindPane(pane_name))
			++pane_count;
	}
	if (pane_count < 2)
		return;

	for (const auto& language_pane : legacy_language_panes)
	{
		const std::string& pane_name = language_pane.second;
		if (Pane* const pane = FindPane(pane_name))
		{
			const bool selected = pane_name == selected_language->second;
			pane->SetHide(!selected);
			if (selected)
				pane->SetVisible(true);
		}
	}
}

Texture* Layout::FindTexture(const std::string& find_name) {
	for(auto & texture : resources.textures) {
		if (find_name == texture->GetName())
			return texture;
	}

	return nullptr;
}

void Layout::AddPalette(const std::string& name, uint8_t key_set) {
	std::cout << "AddPalette: " << name << "\n";

	if (resources.palettes.size() <= key_set)
		resources.palettes.resize(key_set + 1);

	resources.palettes.at(key_set).push_back(name);

	if (FindTexture(name))
	{
		std::cout << "already exists\n";
		return;
	}

	auto* texture = new Texture;
	texture->SetName(name);

	std::cout << "Creating texture "
		  << texture
		  << " name="
		  << texture->GetName()
		  << "\n";

	resources.textures.push_back(texture);

	std::cout << "added texture: " << texture->GetName() << "\n";
}

Pane* Layout::FindPane(const std::string& find_name)
{
	Pane* found = nullptr;

	for (Pane* pane : panes)
	{
		found = pane->FindPane(find_name);
		if (found)
			break;
	}

	return found;
}

Material* Layout::FindMaterial(const std::string& find_name)
{
	for (Material* material : resources.materials)
	{
		if (find_name == material->GetName())
			return material;
	}

	return nullptr;
}

}