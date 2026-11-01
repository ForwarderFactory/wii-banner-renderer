/*
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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

#include <libwb/Banner.h>
#include <libwb/GXDebug.h>
#include <libwb/Layout.h>
#include <libwb/Wad.h>

namespace
{

namespace gd = wb::gxdebug;

struct Options
{
	std::vector<std::string> inputs;
	std::string output_file;
	std::string font_archive;
	std::string language = "ENG";

	bool banner = true;
	bool icon = false;
	bool sound = true;
	bool dump = true;
	bool keys = true;
	bool render = true;
	bool widescreen = true;
	bool state_dump = true;

	uint32_t trace = gd::TRACE_ALL;

	std::string frames = "0";
	std::string render_frames;  // empty = first of frames
};

std::ostream* g_out = &std::cout;

void Sink(const std::string& line, void*)
{
	*g_out << line << '\n';
}

void Section(const std::string& title)
{
	*g_out << "\n==== " << title << " ====\n";
}

void FlushLog(const char* what)
{
	wb::Logger& logger = wb::Banner::GetLogger();
	if (!logger.size())
		return;

	*g_out << "-- library log (" << what << ") --\n";
	for (const wb::LogEntry& entry : logger)
	{
		std::string text = entry.text;
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
			text.pop_back();

		*g_out << "  " << entry.type << ": " << text << '\n';
	}
	logger.clear();
}

// "0", "0-30", "0-100/5", "0,10,20-25", "all", "start", "loop"
bool ParseFrames(const std::string& spec, const wb::Layout& layout, std::vector<float>& out, std::string& error)
{
	out.clear();

	const float loop_start = layout.GetLoopStart();
	const float loop_end = layout.GetLoopEnd();

	std::stringstream parts(spec);
	std::string part;
	while (std::getline(parts, part, ','))
	{
		if (part.empty())
			continue;

		float first = 0, last = 0, step = 1;

		if (part == "all")
		{
			first = 0;
			last = loop_end - 1;
		}
		else if (part == "start")
		{
			first = 0;
			last = loop_start - 1;
		}
		else if (part == "loop")
		{
			first = loop_start;
			last = loop_end - 1;
		}
		else
		{
			std::string range = part;

			const size_t slash = range.find('/');
			if (slash != std::string::npos)
			{
				step = static_cast<float>(std::atof(range.c_str() + slash + 1));
				range.resize(slash);
			}

			const size_t dash = range.find('-', 1);
			first = static_cast<float>(std::atof(range.c_str()));
			last = dash == std::string::npos ? first : static_cast<float>(std::atof(range.c_str() + dash + 1));

			if (!(step > 0))
			{
				error = "frame step must be positive: " + part;
				return false;
			}
		}

		for (float f = first; f <= last; f += step)
			out.push_back(f);
	}

	if (out.empty() && spec != "all" && spec != "start" && spec != "loop")
	{
		error = "no frames in '" + spec + "'";
		return false;
	}

	return true;
}

std::vector<std::pair<std::string, std::string>> Tokens(const std::string& state)
{
	std::vector<std::pair<std::string, std::string>> tokens;

	std::stringstream stream(state);
	std::string token;
	while (stream >> token)
	{
		const size_t eq = token.find('=');
		if (eq == std::string::npos)
			tokens.emplace_back(token, "");
		else
			tokens.emplace_back(token.substr(0, eq), token.substr(eq + 1));
	}

	return tokens;
}

std::string Diff(const std::string& before, const std::string& after)
{
	const auto a = Tokens(before);
	const auto b = Tokens(after);

	std::map<std::string, std::string> old_values(a.begin(), a.end());

	std::string out;
	for (const auto& token : b)
	{
		const auto found = old_values.find(token.first);
		if (found != old_values.end() && found->second == token.second)
			continue;

		out += " " + token.first + ": " + (found == old_values.end() ? "?" : found->second) + " -> " + token.second + ";";
	}

	return out;
}

struct Snapshot
{
	std::map<const wb::Pane*, std::string> panes;
	std::map<const wb::Material*, std::string> materials;
	std::map<const wb::Pane*, std::string> paths;
};

void Collect(const wb::PaneList& list, const std::string& prefix, Snapshot& snapshot)
{
	for (const wb::Pane* pane : list)
	{
		const std::string path = prefix + "/" + pane->GetName();
		snapshot.paths[pane] = path;
		snapshot.panes[pane] = pane->DebugState();
		Collect(pane->panes, path, snapshot);
	}
}

Snapshot Take(const wb::Layout& layout)
{
	Snapshot snapshot;
	Collect(layout.GetPanes(), "", snapshot);
	for (const wb::Material* material : layout.resources.materials)
		snapshot.materials[material] = material->DebugState();
	return snapshot;
}

void DescribeGroups(const std::map<std::string, wb::Layout::Group>& groups, const wb::Layout& layout, int indent)
{
	for (const auto& entry : groups)
	{
		*g_out << std::string(size_t(indent), ' ') << "group '" << entry.first << "' (" << entry.second.panes.size() << " pane(s))\n";

		for (const std::string& name : entry.second.panes)
		{
			const bool exists = const_cast<wb::Layout&>(layout).FindPane(name) != nullptr;
			*g_out << std::string(size_t(indent) + 2, ' ') << "pane '" << name << "'" << (exists ? "" : "  !! no pane with this name") << '\n';
		}

		DescribeGroups(entry.second.groups, layout, indent + 2);
	}
}

void DescribePanes(const wb::PaneList& list, const wb::Resources& resources, int depth, float ax, float ay, float sx, float sy)
{
	for (const wb::Pane* pane : list)
	{
		const float x = ax + sx * pane->GetTranslate().x;
		const float y = ay + sy * pane->GetTranslate().y;

		*g_out << std::string(size_t(depth) * 2, ' ') << "--- depth " << depth << ", absolute translate (ignoring rotation/origin) = (" << x << ", " << y << ")\n";
		pane->DebugDescribe(*g_out, resources, depth * 2);

		DescribePanes(pane->panes, resources, depth + 1, x, y, sx * pane->GetScale().x, sy * pane->GetScale().y);
	}
}

void DescribeLayout(const wb::Layout& layout, const std::string& name, const Options& options)
{
	Section("layout " + name + ": parsed data");

	*g_out << "size " << layout.GetWidth() << "x" << layout.GetHeight() << " (the banner/icon size is forced by the loader), centered=" << layout.GetCentered() << '\n';
	*g_out << "animation: start segment 0.." << layout.GetLoopStart() << ", loop segment " << layout.GetLoopStart() << ".." << layout.GetLoopEnd()
		<< " (" << layout.GetLoopEnd() << " frames total), current frame " << layout.GetFrame() << '\n';

	const wb::Resources& resources = layout.resources;

	*g_out << "\n-- textures (" << resources.textures.size() << ") --\n";
	for (size_t i = 0; i != resources.textures.size(); ++i)
	{
		*g_out << "  #" << i << " ";
		resources.textures[i]->DebugDescribe(*g_out, 0);
	}

	*g_out << "\n-- fonts (" << resources.fonts.size() << ") --\n";
	for (size_t i = 0; i != resources.fonts.size(); ++i)
	{
		*g_out << "  #" << i << " ";
		resources.fonts[i]->DebugDescribe(*g_out, 0);
	}

	*g_out << "\n-- materials (" << resources.materials.size() << ") --\n";
	for (size_t i = 0; i != resources.materials.size(); ++i)
	{
		*g_out << "  #" << i << '\n';
		resources.materials[i]->DebugDescribe(*g_out, resources, 2);
	}

	if (!resources.palettes.empty())
	{
		*g_out << "\n-- texture palettes (animated texture swaps) --\n";
		for (size_t set = 0; set != resources.palettes.size(); ++set)
			for (size_t i = 0; i != resources.palettes[set].size(); ++i)
				*g_out << "  key set " << set << " palette[" << i << "] = '" << resources.palettes[set][i] << "'\n";
	}

	*g_out << "\n-- groups --\n";
	if (layout.GetGroups().empty())
		*g_out << "  (none)\n";
	DescribeGroups(layout.GetGroups(), layout, 2);

	*g_out << "\n-- pane tree --\n";
	DescribePanes(layout.GetPanes(), resources, 0, 0.f, 0.f, 1.f, 1.f);
	(void)options;
}

void DryRender(wb::Layout& layout, float frame, const Options& options)
{
	*g_out << "\n-- dry render of frame " << frame << " --\n";

	gd::SetFlags(gd::HEADLESS | options.trace);
	layout.Render(1.0f, 0xff, options.widescreen);
	gd::SetFlags(gd::HEADLESS);
}

void StepFrames(wb::Layout& layout, const Options& options)
{
	std::vector<float> frames, render_frames;
	std::string error;

	if (!ParseFrames(options.frames, layout, frames, error))
	{
		*g_out << "!! " << error << '\n';
		return;
	}

	if (frames.empty())
	{
		*g_out << "(no frames selected)\n";
		return;
	}

	if (options.render_frames.empty())
		render_frames.push_back(frames.front());
	else if (!ParseFrames(options.render_frames, layout, render_frames, error))
	{
		*g_out << "!! " << error << '\n';
		return;
	}

	const std::set<float> render_set(render_frames.begin(), render_frames.end());

	Section("frames");

	Snapshot previous;
	bool first = true;

	for (const float frame : frames)
	{
		layout.SetFrame(frame);

		const unsigned key_set = frame >= layout.GetLoopStart() ? 1 : 0;
		*g_out << "\n--- frame " << frame << " (key set " << key_set << (key_set ? ": loop animation" : ": start animation") << ") ---\n";

		Snapshot now = Take(layout);

		if (first)
		{
			*g_out << "state of every pane and material:\n";
			for (const auto& pane : now.panes)
				*g_out << "  pane " << now.paths[pane.first] << " [" << pane.first->TypeName() << "]: " << pane.second << '\n';
			for (const auto& material : now.materials)
				*g_out << "  material '" << material.first->GetName() << "': " << material.second << '\n';
		}
		else
		{
			size_t changes = 0;

			for (const auto& pane : now.panes)
			{
				const auto before = previous.panes.find(pane.first);
				if (before != previous.panes.end() && before->second != pane.second)
				{
					*g_out << "  pane " << now.paths[pane.first] << ":" << Diff(before->second, pane.second) << '\n';
					++changes;
				}
			}

			for (const auto& material : now.materials)
			{
				const auto before = previous.materials.find(material.first);
				if (before != previous.materials.end() && before->second != material.second)
				{
					*g_out << "  material '" << material.first->GetName() << "':" << Diff(before->second, material.second) << '\n';
					++changes;
				}
			}

			if (!changes)
				*g_out << "  (nothing changed)\n";
		}

		previous = std::move(now);
		first = false;

		if (options.render && render_set.count(frame))
			DryRender(layout, frame, options);

		FlushLog("frame");
	}
}

int Usage(const char* program)
{
	std::cerr <<
		"wbd - Wii banner debugger\n"
		"\n"
		"usage: " << program << " [options] <file> [...]\n"
		"\n"
		"what to inspect:\n"
		"  --banner / --icon / --both   which layout (default: banner)\n"
		"  --language <XXX>             language group to select (default: ENG)\n"
		"  --font-archive <path>        shared font archive (same as wbr)\n"
		"  --no-sound                   do not load/describe sound.bin\n"
		"\n"
		"output:\n"
		"  -o <file>                    write to a file instead of stdout\n"
		"  --no-dump                    skip the static dump of the parsed layout\n"
		"  --no-keys                    leave animation keys out of the dump\n"
		"  --frames <spec>              frames to step through, printing what changed\n"
		"                               e.g. 0   0-30   0-300/10   0,15,60-65   start   loop   all  (default: 0)\n"
		"  --render-frames <spec>       frames to dry-render (default: the first of --frames)\n"
		"  --no-render                  never render\n"
		"  --no-widescreen              render as 4:3 (wbr renders widescreen)\n"
		"\n"
		"dry render trace (all on by default):\n"
		"  --no-gx --no-draw --no-glsl --no-textures   switch off one kind of trace\n"
		"  --no-state                   skip the final WrapGx state dump and statistics\n"
		"  --legacy-env                 also enable WB_LAYOUT_DEBUG, WB_TEXT_DEBUG and WB_DUMP_ALL\n"
		"\n"
		"  -h, --help                   this text\n";

	return 2;
}

bool Parse(int argc, char** argv, Options& options)
{
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];

		auto value = [&](const char* name) -> const char*
		{
			if (i + 1 >= argc)
			{
				std::cerr << "missing value for " << name << '\n';
				return nullptr;
			}
			return argv[++i];
		};

		if (arg == "-h" || arg == "--help")
			return false;
		else if (arg == "--banner") { options.banner = true; options.icon = false; }
		else if (arg == "--icon") { options.banner = false; options.icon = true; }
		else if (arg == "--both") { options.banner = options.icon = true; }
		else if (arg == "--no-sound") options.sound = false;
		else if (arg == "--no-dump") options.dump = false;
		else if (arg == "--no-keys") options.keys = false;
		else if (arg == "--no-render") options.render = false;
		else if (arg == "--no-widescreen") options.widescreen = false;
		else if (arg == "--no-state") options.state_dump = false;
		else if (arg == "--no-gx") options.trace &= ~uint32_t(gd::TRACE_GX);
		else if (arg == "--no-draw") options.trace &= ~uint32_t(gd::TRACE_DRAW);
		else if (arg == "--no-glsl") options.trace &= ~uint32_t(gd::TRACE_GLSL);
		else if (arg == "--no-textures") options.trace &= ~uint32_t(gd::TRACE_TEXTURE);
		else if (arg == "--language" || arg == "--font-archive" || arg == "--frames" || arg == "--render-frames" || arg == "-o")
		{
			const char* v = value(arg.c_str());
			if (!v)
				return false;

			if (arg == "--language") options.language = v;
			else if (arg == "--font-archive") options.font_archive = v;
			else if (arg == "--frames") options.frames = v;
			else if (arg == "--render-frames") options.render_frames = v;
			else options.output_file = v;
		}
		else if (!arg.empty() && arg[0] == '-')
		{
			std::cerr << "unknown option " << arg << '\n';
			return false;
		}
		else
			options.inputs.push_back(arg);
	}

	return !options.inputs.empty();
}

bool ResolveInput(const std::string& input, std::string& banner_path, std::filesystem::path& extraction_dir)
{
	if (!std::filesystem::exists(input))
	{
		*g_out << "!! " << input << " does not exist\n";
		return false;
	}

	*g_out << "input: " << input << " (" << std::filesystem::file_size(input) << " bytes)\n";

	if (std::filesystem::path(input).extension() != ".wad")
	{
		banner_path = input;
		return true;
	}

	std::ifstream in(input, std::ios::binary);
	if (!in)
	{
		*g_out << "!! cannot read " << input << '\n';
		return false;
	}

	extraction_dir = std::filesystem::temp_directory_path() / ("wbd-" + std::to_string(getpid()));

	try
	{
		wb::Wad::extract_wad(in, extraction_dir.string());
	}
	catch (const std::exception& e)
	{
		*g_out << "!! extracting the wad failed: " << e.what() << '\n';
		return false;
	}

	*g_out << "wad contents (extracted to " << extraction_dir.string() << "):\n";

	std::vector<std::filesystem::path> files;
	for (const auto& entry : std::filesystem::directory_iterator(extraction_dir))
		if (entry.is_regular_file())
			files.push_back(entry.path());
	std::sort(files.begin(), files.end());

	for (const auto& file : files)
	{
		const bool app = file.extension() == ".app";
		const bool valid = app && wb::Banner::is_valid(file.string());

		*g_out << "  " << file.filename().string() << "  " << std::filesystem::file_size(file) << " bytes"
			<< (app ? (valid ? "  <- valid banner" : "  (not a banner)") : "") << '\n';

		if (valid && banner_path.empty())
			banner_path = file.string();
	}

	if (banner_path.empty())
	{
		*g_out << "!! no valid banner .app found in the wad\n";
		return false;
	}

	return true;
}

int Inspect(const std::string& input, const Options& options)
{
	Section("input");

	std::string banner_path;
	std::filesystem::path extraction_dir;

	const bool resolved = ResolveInput(input, banner_path, extraction_dir);
	auto cleanup = [&]()
	{
		std::error_code ec;
		if (!extraction_dir.empty())
			std::filesystem::remove_all(extraction_dir, ec);
	};

	if (!resolved)
	{
		cleanup();
		return 1;
	}

	if (!wb::Banner::is_valid(banner_path))
	{
		*g_out << "!! " << banner_path << " is not a banner (no U8 archive at 0x600 or 0x640)\n";
		cleanup();
		return 1;
	}

	wb::Banner banner(banner_path, options.font_archive);

	Section("container");
	banner.DebugDescribeContainer(*g_out);
	FlushLog("opening");

	if (options.banner)
		banner.LoadBanner();
	if (options.icon)
		banner.LoadIcon();
	if (options.sound)
		banner.LoadSound();

	FlushLog("loading");

	if (options.sound)
	{
		Section("sound");
		if (wb::Sound* sound = banner.GetSound())
		{
			*g_out << "duration " << sound->GetDurationSeconds() << " s, loop: " << (sound->HasLoop() ? "yes" : "no");
			if (sound->HasLoop())
				*g_out << " (samples " << sound->GetLoopStart() << ".." << sound->GetLoopEnd() << ")";
			*g_out << '\n';
		}
		else
			*g_out << "no sound loaded (missing sound.bin, or its format is unsupported: see the log)\n";
	}

	const struct { const char* name; wb::Layout* layout; bool wanted; } layouts[] =
	{
		{ "Banner", banner.GetBanner(), options.banner },
		{ "Icon", banner.GetIcon(), options.icon },
	};

	int status = 0;

	for (const auto& item : layouts)
	{
		if (!item.wanted)
			continue;

		if (!item.layout)
		{
			Section(std::string("layout ") + item.name);
			*g_out << "!! the layout could not be loaded: the banner has no meta/" << (std::string(item.name) == "Icon" ? "icon" : "banner") << ".bin, or its brlyt is missing/invalid (see the log)\n";
			FlushLog("layout");
			status = 1;
			continue;
		}

		item.layout->SetLanguage(options.language);

		if (options.dump)
		{
			if (!options.keys)
			{
				std::ostringstream captured;
				std::ostream* previous = g_out;
				g_out = &captured;
				DescribeLayout(*item.layout, item.name, options);
				g_out = previous;

				std::stringstream lines(captured.str());
				std::string line;
				while (std::getline(lines, line))
					if (line.find("anim[set") == std::string::npos)
						*g_out << line << '\n';
			}
			else
				DescribeLayout(*item.layout, item.name, options);
		}

		StepFrames(*item.layout, options);

		if (options.state_dump)
		{
			Section(std::string("layout ") + item.name + ": state after render");
			gd::DumpWrapGxState(*g_out);
		}
	}

	if (options.state_dump)
	{
		Section("statistics");
		gd::PrintStats(*g_out);
	}

	FlushLog("end");
	cleanup();
	return status;
}

}

int main(int argc, char** argv)
{
	Options options;
	if (!Parse(argc, argv, options))
		return Usage(argv[0]);

	std::ofstream file;
	if (!options.output_file.empty())
	{
		file.open(options.output_file);
		if (!file)
		{
			std::cerr << "cannot write " << options.output_file << '\n';
			return 1;
		}
		g_out = &file;
	}

	gd::SetFlags(gd::HEADLESS);
	gd::SetSink(Sink, nullptr);
	gd::ResetStats();

	int status = 0;

	for (const std::string& input : options.inputs)
		status |= Inspect(input, options);

	return status;
}
