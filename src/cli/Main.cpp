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
#include "VideoEncoder.h"
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#include <windows.h>
#include <cstdlib>
#include <io.h>
#include <fcntl.h>
#endif

#include <iostream>
#include <filesystem>
#include <cmath>
#ifdef __WIN32__
#include <process.h>
#else
#include <unistd.h>
#endif
#include <algorithm>
#include <array>
#include <sstream>

#include "../../include/libwb/Banner.h"
#include "../../include/libwb/Renderer.h"
#include "../../include/libwb/Wad.h"

constexpr int VIDEO_WIDTH = 1920;
constexpr int VIDEO_HEIGHT = 1080;

struct Settings {
	int fps = 60; // fps to render at
	int minimum_length = 10; // min seconds
	int maximum_length = -1; // max seconds
	bool save_frames = false; // save frames? (wastes your time, useful for debugging)
	int frames_to_save = -1; // -1 = save all frames iterated through
	bool no_audio = false; // no audio, for debugging
	bool no_crop = false; // no crop, for debugging
	bool icon = false; // icon, self explanatory
	double resolution_multiplier = 1; // resolution multiplier
	bool webm = false; // output to webm
	bool sound_only = false; // sound only
	std::filesystem::path font_archive; // Wii shared font content archive

	void print_settings() const {
		std::cout << "FPS: " << fps << "\n";
		std::cout << "Icon: " << icon << "\n";
		std::cout << "Minimum length: " << (minimum_length == -1 ? "No limit" : std::to_string(minimum_length)) << " sec\n";
		std::cout << "Maximum length: " << (maximum_length == -1 ? "No limit" : std::to_string(maximum_length)) << " sec\n";
		std::cout << "Save frames: " << save_frames << "\n";
		std::cout << "Frames to save (if enabled): " << (frames_to_save == -1 ? "all" : std::to_string(frames_to_save)) << "\n";
		std::cout << "No audio: " << no_audio << "\n";
		std::cout << "No crop: " << no_crop << "\n";
		std::cout << "Resolution multiplier: " << resolution_multiplier << "\n";
		std::cout << "Sound only: " << sound_only << "\n";
		std::cout << "Font archive: "
			<< (font_archive.empty() ? "auto" : font_archive.string()) << "\n";
	}
};

struct Render {
	std::filesystem::path input;
	std::filesystem::path output;
};

int get_proc() {
#ifdef __WIN32__
	return _getpid();
#else
	return getpid();
#endif
}

int process(const Render& input_opening, Settings settings = {}) {
	std::filesystem::path opening = input_opening.input;

	std::cout << "Processing: " << opening << "\n";

	std::filesystem::path extraction_dir =
		std::filesystem::temp_directory_path() /
		("wbr-" + std::to_string(get_proc()));

	if (std::filesystem::path(opening).extension() == ".wad") {
		std::ifstream in(opening, std::ios::binary);
		if (!in) {
			std::cerr << "Input file does not exist or cannot be read: " << input_opening.input << "\n";
			return EXIT_FAILURE;
		}

		Wad::extract_wad(in, extraction_dir.string());

		for (const auto& entry : std::filesystem::directory_iterator(extraction_dir))
		{
			if (!entry.is_regular_file())
				continue;

			if (entry.path().extension() != ".app")
				continue;

			const std::string path = entry.path().string();

			if (WiiBanner::Banner::is_valid(path)) {
				opening = path;
				break;
			}
		}

		if (opening.empty())
		{
			std::cerr << "No valid .app file found, probably invalid .wad\n";
			return EXIT_FAILURE;
		}
	}

	std::string base_filename = input_opening.output.string();
	if (input_opening.output.empty()) {
		base_filename = input_opening.input.filename().string();

		auto ext = base_filename.find_last_of('.');
		if (ext != std::string::npos) {
			base_filename = base_filename.substr(0, ext);
		}

		if (settings.webm)
			base_filename += ".webm";
		else
			base_filename += ".mp4";
	}

	std::filesystem::path tmp{base_filename};
	if (!std::filesystem::is_directory(tmp.parent_path()) && tmp.parent_path().empty() == false) {
		std::filesystem::create_directories(tmp.parent_path());

		if (!std::filesystem::is_directory(tmp.parent_path())) {
			std::cerr << "Failed to create directory: " << tmp.parent_path().string() << "\n";
			return EXIT_FAILURE;
		}
	}

	if (settings.sound_only) {
		WiiBanner::Banner banner(opening.string(), settings.font_archive.string());
		banner.LoadSound();
		if (banner.GetSound()) {
			banner.GetSound()->WriteWAV(base_filename + ".wav");
			std::cerr << "Extracted audio\n";
		} else {
			std::cerr << "Failed to extract audio, exiting...\n";
			return EXIT_FAILURE;
		}

		return EXIT_SUCCESS;
	}

  Renderer renderer(static_cast<int>(VIDEO_WIDTH * settings.resolution_multiplier), static_cast<int>(VIDEO_HEIGHT * settings.resolution_multiplier));

  WiiBanner::Banner banner(opening.string(), settings.font_archive.string());

	if (settings.icon) {
		banner.LoadIcon();
		settings.no_audio = true; // im torn about this one
	} else {
		banner.LoadBanner();
	}

	if (!settings.no_audio)
		banner.LoadSound();

	WiiBanner::Layout* layout = settings.icon ? banner.GetIcon() : banner.GetBanner();
	if (!layout) {
		throw std::runtime_error{"layout == nullptr"};
	}

	layout->SetLanguage("ENG");

	if (!settings.no_audio) {
		if (!banner.GetSound()) {
			throw std::runtime_error{"GetSound() failed"};
		}
	}
	double runtime{};

	if (!settings.no_audio) {
		banner.GetSound()->WriteWAV(base_filename + ".wav");
		runtime = banner.GetSound()->GetDurationSeconds();
	}

	if (settings.maximum_length >= 0) {
		runtime = std::min(runtime, static_cast<double>(settings.maximum_length));
	}
	if (settings.minimum_length >= 0) {
		runtime = std::max(runtime, static_cast<double>(settings.minimum_length));
	}

	if (!settings.no_audio) {
		banner.GetSound()->WriteWAVLooped(
			base_filename + ".wav",
			runtime
		);
	}

	std::array<Point, 4> points = {{
		{1060 * settings.resolution_multiplier, 20 * settings.resolution_multiplier},
		{1060 * settings.resolution_multiplier, 403 * settings.resolution_multiplier},
		{1833 * settings.resolution_multiplier, 20 * settings.resolution_multiplier},
		{1833 * settings.resolution_multiplier, 403 * settings.resolution_multiplier}
	}};

	std::array<Point, 4> points_icon = {{
		{1060 * settings.resolution_multiplier, 0 * settings.resolution_multiplier},
		{1060 * settings.resolution_multiplier, 520 * settings.resolution_multiplier},
		{1833 * settings.resolution_multiplier, 0 * settings.resolution_multiplier},
		{1833 * settings.resolution_multiplier, 520 * settings.resolution_multiplier}
	}};

	Rect crop;

	if (settings.no_crop) {
		crop = {
			static_cast<int>(VIDEO_WIDTH * settings.resolution_multiplier),
			static_cast<int>(VIDEO_HEIGHT * settings.resolution_multiplier),
			0,
			0
		};
	}
	else
	{
		crop = GetCrop(settings.icon ? points_icon : points);
	}

	VideoEncoder::OutputFormat output_format = settings.webm ?
	VideoEncoder::OutputFormat::WEBM :
	VideoEncoder::OutputFormat::MP4;

	auto audio_file = base_filename + ".wav";

	VideoEncoder encoder(base_filename, crop.width, crop.height, settings.fps, audio_file, output_format);

    for (int i = 0; i < settings.fps * runtime; i++) {
    	renderer.BeginFrame();
    	layout->Render(1.0f, 0xff, true);
		renderer.EndFrame();

		if (settings.save_frames && settings.frames_to_save && i <= settings.frames_to_save) {
			char filename[64];
			sprintf(filename, "output-%04d.png", i);

			renderer.SavePNG(filename, static_cast<int>(VIDEO_WIDTH * settings.resolution_multiplier), static_cast<int>(VIDEO_HEIGHT * settings.resolution_multiplier));
  		}

    	encoder.writeVideoFrame(renderer.GetFrameData(crop).data());

    	layout->AdvanceFrame();
    }

	encoder.finish();

    banner.UnloadBanner();

	// clean temp files and directories TODO: dont write wav to disk at all
	if (std::filesystem::is_directory(extraction_dir)) {
		std::filesystem::remove_all(extraction_dir);
	}
	if (!settings.no_audio && std::filesystem::is_regular_file(base_filename + ".wav")) {
		std::filesystem::remove(base_filename + ".wav");
	}

	std::cout << "Processed " << input_opening.input << "\n";

    return 0;
}

static std::string trim(const std::string& s) {
	const char* whitespace = " \t\n\r\f\v";

	size_t start = s.find_first_not_of(whitespace);
	if (start == std::string::npos)
		return "";

	size_t end = s.find_last_not_of(whitespace);
	return s.substr(start, end - start + 1);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "usage: wii-banner-renderer <00000000.app/opening.bnr/*.wad> <optional arguments>\n";
    	std::cout << "-fps/--fps:                         Frames to render per second. Useful if you want to speed up or slow down the render.\n";
    	std::cout << "-w/--prompt:                        Ask for input files instead of checking parameters. Useful for building in IDEs.\n";
    	std::cout << "-m/--mute:                          Do not retrieve the audio data. Output video will be silent.\n";
    	std::cout << "-nc/--no-crop:                      Do not crop to the Wii's visible area. Only recommended for debugging.\n";
    	std::cout << "-i/--icon:                          Output the channel's icon, instead of banner.\n";
    	std::cout << "-s/--save <int>:                    Save frames as images. Optional integer following it will be the limit, otherwise all frames will be saved.\n";
    	std::cout << "-so/--sound-only:                   Save audio only, don't process banner/icon.\n";
    	std::cout << "-min/--minimum-length <int>:        Minimum length of the output video. Default is 10 seconds, 0 is the length of the audio track.\n";
    	std::cout << "-max/--maximum-length <int>:        Maximum length of the output video. Default is no limit.\n";
		std::cout << "-res/--resolution-multiplier <int>: Resolution multiplier, 1 is default (1920x1080). Example: pass 1.33 for 1440p or 2 for 4k.\n";
		std::cout << "-font/--font-archive <path>:        Wii shared font archive containing wbf1.brfna and wbf2.brfna.\n";
    	std::cout << "-webm/--webm:                       Output in .webm format (VP9)\n";
    	std::cout << "\n";
    	std::cout << "Files are output in the working directory and bear the name of the input file (with a different file extension) by default.\n";
    	std::cout << "You can pass in -o <path> after the input file to specify an output path.\n";
    	std::cout << "\n";
    	std::cout << "Wii Banner Renderer by Forwarder Factory, continuation of wii-banner-player by Wii Banner Player Team.\n";
    	std::cout << "https://github.com/ForwarderFactory/wii-banner-renderer\n";
    	std::cout << "https://code.google.com/archive/p/wii-banner-player/\n";
    	std::cout << "We thank the original authors for all of their hard work.\n";
        return EXIT_FAILURE;
    }

	Settings settings{};
	std::vector<Render> openings;

	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];

		if (arg == "-w" || arg == "--prompt") {
			std::string opening;
			std::cout << "input file(s) (comma split): " << std::flush;
			std::getline(std::cin, opening);

			std::stringstream ss(opening);
			std::string item;

			while (std::getline(ss, item, ',')) {
				item = trim(item);
				if (!item.empty())
					openings.emplace_back(Render{.input = item});
			}
		} else if (arg == "-m" || arg == "--mute") {
			// mute
			settings.no_audio = true;
		} else if (arg == "-nc" || arg == "--no-crop") {
			// no crop
			settings.no_crop = true;
		} else if (arg == "-so" || arg == "--sound-only") {
			settings.sound_only = true;
		} else if (arg == "-i" || arg == "--icon") {
			settings.icon = true;
		} else if (arg == "-webm" || arg == "--webm") {
			settings.webm = true;
		} else if (arg == "-font" || arg == "--font-archive") {
			if (i + 1 >= argc) {
				std::cerr << arg << " requires a path\n";
				return EXIT_FAILURE;
			}

			settings.font_archive = argv[++i];
			if (!std::filesystem::is_regular_file(settings.font_archive)) {
				std::cerr << "Font archive does not exist or cannot be read: "
					<< settings.font_archive << "\n";
				return EXIT_FAILURE;
			}
		} else if (arg == "-s" || arg == "--save") {
			settings.save_frames = true;

			if (i + 1 < argc) {
				std::string sec_arg = argv[i + 1];

				try {
					size_t pos = 0;
					int value = std::stoi(sec_arg, &pos);

					if (pos == sec_arg.size()) {
						settings.frames_to_save = value;
						++i;
					}
				} catch (...) {
					continue;
				}
			}
		} else if (arg == "-min" || arg == "--minimum-length") {
			if (i + 1 < argc) {
				std::string sec_arg = argv[i + 1];

				try {
					size_t pos = 0;
					int value = std::stoi(sec_arg, &pos);

					if (pos == sec_arg.size()) { // ensure entire string is integers
						settings.minimum_length = value;
					} else {
						std::cerr << "invalid integer passed\n";
						return EXIT_FAILURE;
					}
				} catch (std::exception&) {
					std::cerr << "-min flag requires an integer\n";
				}
			} else {
				std::cerr << "-min flag requires an integer\n";
				return EXIT_FAILURE;
			}
		} else if (arg == "-max" || arg == "--maximum-length") {
			if (i + 1 < argc) {
				std::string sec_arg = argv[i + 1];

				try {
					size_t pos = 0;
					int value = std::stoi(sec_arg, &pos);

					if (pos == sec_arg.size()) { // ensure entire string is integers
						settings.maximum_length = value;
					} else {
						std::cerr << "invalid integer passed\n";
						return EXIT_FAILURE;
					}
				} catch (std::exception&) {
					std::cerr << "-max flag requires an integer\n";
				}
			} else {
				std::cerr << "-max flag requires an integer\n";
				return EXIT_FAILURE;
			}
		} else if (arg == "-fps" || arg == "--fps") {
			if (i + 1 < argc) {
				std::string sec_arg = argv[i + 1];

				try {
					size_t pos = 0;
					int value = std::stoi(sec_arg, &pos);

					if (pos == sec_arg.size()) { // ensure entire string is integers
						settings.fps = value;
					} else {
						std::cerr << "invalid integer passed\n";
						return EXIT_FAILURE;
					}
				} catch (std::exception&) {
					std::cerr << "-fps flag requires an integer\n";
				}
			} else {
				std::cerr << "-fps flag requires an integer\n";
				return EXIT_FAILURE;
			}
		} else if (arg == "-res" || arg == "--resolution-multiplier") {
			if (i + 1 < argc) {
				std::string sec_arg = argv[i + 1];

				try {
					size_t pos = 0;
					double value = std::stod(sec_arg, &pos);

					if (pos == sec_arg.size()) { // ensure entire string is doubles
						settings.resolution_multiplier = value;
					} else {
						std::cerr << "invalid double passed\n";
						return EXIT_FAILURE;
					}
				} catch (std::exception&) {
					std::cerr << "-res flag requires a numeral value\n";
				}
			} else {
				std::cerr << "-res flag requires an integer\n";
				return EXIT_FAILURE;
			}
		} else if (arg == "-o") {
			if (i + 1 < argc) {
				if (openings.empty()) {
					std::cerr << "Error: -o used before any input file\n";
					return 1;
				}

				openings.back().output = argv[++i];
			} else {
				std::cerr << "Error: -o requires a path\n";
				return 1;
			}
		} else if (std::filesystem::is_regular_file(arg)) {
			openings.push_back({arg, {}});
		} else {
			std::cerr << "Warning: ignoring unknown argument: " << arg << "\n";
		}
	}

	if (openings.empty()) {
		std::cerr << "No files specified.\n";
		return EXIT_FAILURE;
	}

	std::cout << "Settings:\n";
	settings.print_settings();

	int ret_val = EXIT_SUCCESS;
	for (const auto& it : openings) {
		auto ret = process(it, settings);

		if (ret != EXIT_SUCCESS) {
			ret_val = EXIT_FAILURE;
		}
	}

	return ret_val;
}
