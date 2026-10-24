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

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace WiiBanner
{

class Sound
{
	std::vector<uint8_t> rawData{};
        std::vector<int16_t> samples{};
        uint16_t channels = 0;
        uint32_t sampleRate = 0;
		uint32_t sampleCount = 0;

	uint32_t loop_start = 0;
	uint32_t loop_end = 0;
	uint32_t wav_data_size = 0;
	uint32_t wav_byte_rate = 0;
	bool has_loop = false;

	static void WritePCMAsWAV(const std::string& path,
	                          const std::vector<int16_t>& samples,
	                          uint16_t channels,
	                          uint32_t sampleRate);
	bool ParseWAV();

public:
	Sound() = default;
	~Sound() = default;

	bool Load(std::istream& file);

	void WriteWAV(const std::string& path);
	void WriteWAVLooped(const std::string& path, double seconds);
	[[nodiscard]] double GetDurationSeconds() const;
};

}
