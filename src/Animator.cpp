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

#include <libwb/Animator.h>
#include <libwb/Funcs.h>
#include <libwb/Endian.h>
#include <libwb/Layout.h>

#include <cmath>

enum BinaryMagic : uint32_t {
	BINARY_MAGIC_ANIMATION = MAKE_FOURCC('R', 'L', 'A', 'N'),
	BINARY_MAGIC_PANE_ANIMATION_INFO = MAKE_FOURCC('p', 'a', 'i', '1')
};

namespace WiiBanner {
	FrameNumber Animator::LoadAnimators(std::istream& file, Layout& layout, uint8_t key_set) {
		const std::streamoff file_start = file.tellg();

		uint16_t frame_count;

		// read header
		FourCC header_magic;
		uint16_t endian; // always 0xFEFF
		uint16_t version; // always 0x0008

		file >> header_magic >> BE >> endian >> version;

		if (header_magic != BINARY_MAGIC_ANIMATION
			|| endian != 0xFEFF
			|| version != 0x008
			)
			return 0;	// bad header


		uint32_t file_size;
		uint16_t offset; // offset to the first section
		uint16_t section_count;

		file >> BE >> file_size >> offset >> section_count;

		// only a single pa*1 section is currently supported
		//if (section_count > 1)
		//	section_count = 1;

		// seek to header of first section
		file.seekg(file_start + offset, std::ios::beg);

		ReadSections(file, section_count, [&](FourCC magic, std::streamoff section_start)
		{
			if (magic == BINARY_MAGIC_PANE_ANIMATION_INFO)
			{
				uint8_t loop; // ?
				uint8_t pad;
				uint16_t file_count; // ?
				uint16_t animator_count;
				uint32_t entry_offset;

				file >> BE >> frame_count >> loop
					>> pad >> file_count >> animator_count;

				file >> BE >> entry_offset;

				const std::streamoff name_table_start = file.tellg();

				std::vector<uint32_t> name_offsets(file_count);
				for (uint32_t i = 0; i < file_count; i++) {
					file >> BE >> name_offsets[i];
				}

				for (uint32_t i = 0; i < file_count; i++) {
					file.seekg(name_table_start + static_cast<std::streamoff>(name_offsets[i]));
					std::string name;
					std::getline(file, name, '\0'); // null-terminated string

#ifdef WB_DEBUG
					std::cout << "File: " << name << std::endl;
#endif

					layout.AddPalette(name, key_set);
				}

				file.seekg(section_start + entry_offset);

				// read each animator
				ReadOffsetList<uint32_t>(file, animator_count, section_start, [&]
				{
					const std::streamoff origin = file.tellg();

					const std::string animator_name = ReadFixedLengthString<Animator::NAME_LENGTH>(file);

					uint8_t tag_count;
					uint8_t is_material;
					uint16_t apad;

					file >> BE >> tag_count >> is_material >> apad;

					Animator* const animator = is_material ?
						reinterpret_cast<Animator*>(layout.FindMaterial(animator_name)) :
						reinterpret_cast<Animator*>(layout.FindPane(animator_name));

					if (animator)
						animator->LoadKeyFrames(file, tag_count, origin, key_set);
				});
			} else {
#ifdef WB_DEBUG
				std::cout << "UNKNOWN SECTION: ";
				std::cout << magic << '\n';
#endif
			}
		});

		return frame_count;
	}

void Animator::LoadKeyFrames(std::istream& file, uint8_t tag_count, std::streamoff origin, uint8_t key_set) {
	ReadOffsetList<uint32_t>(file, tag_count, origin, [&]
	{
		const std::streamoff frame_origin = file.tellg();

		uint32_t animation_type;
		uint8_t entry_count;

		file >> BE >> animation_type >> entry_count;
		file.seekg(3, std::ios::cur);	// some padding

		ReadOffsetList<uint32_t>(file, entry_count, frame_origin, [&]
		{
			uint8_t index;
			uint8_t target;
			uint8_t data_type;
			uint8_t pad;
			uint16_t key_count;
			uint16_t pad1;
			uint32_t offset;	// TODO: handle this

			file >> BE >> index >> target >> data_type >> pad
				>> key_count >> pad1 >> offset;

			const KeyType frame_type(static_cast<AnimationType>(animation_type), index, target);

			switch (data_type)
			{
				// step key frame
			case 0x01:
				keys[key_set].step_keys[frame_type].Load(file, key_count);
				break;

				// hermite key frame
			case 0x02:
				keys[key_set].hermite_keys[frame_type].Load(file, key_count);
				break;

			default:
#ifdef WB_DEBUG
				std::cout << "UNKNOWN FRAME DATA TYPE!!\n";
#endif
				break;
			}
		});
	});
}

void Animator::SetFrame(FrameNumber frame_number, uint8_t key_set)
{
	for (auto& frame_handler : keys[key_set].hermite_keys)
	{
		const auto& frame_type = frame_handler.first;
		const auto frame_value = frame_handler.second.GetFrame(frame_number);

		ProcessHermiteKey(frame_type, frame_value);
	}

	for (auto& frame_handler : keys[key_set].step_keys)
	{
		const auto& frame_type = frame_handler.first;
		auto const frame_data = frame_handler.second.GetFrame(frame_number);

		ProcessStepKey(frame_type, frame_data);
	}
}

void StepKeyHandler::Load(std::istream& file, uint16_t count)
{
	while (count--)
	{
		FrameNumber frame;
		file >> BE >> frame;

		auto& data = keys[frame];
		file >> BE >> data.data1 >> data.data2;

		file.seekg(2, std::ios::cur);

		//std::cout << "\t\t\t" "frame: " << frame << ' ' << pair.first << " " << pair.second << '\n';
	}
}

void HermiteKeyHandler::Load(std::istream& file, uint16_t count)
{
	while (count--)
	{
		std::pair<FrameNumber, KeyData> pair;

		// read the frame number, value and slope
		file >> BE >> pair.first
			>> pair.second.value >> pair.second.slope;

		keys.insert(pair);

		//std::cout << "\t\t\t" "frame: " << frame << ' ' << keys[frame] << '\n';
	}
}

StepKeyHandler::KeyData StepKeyHandler::GetFrame(FrameNumber frame_number) const
{
	// assuming not empty, a safe assumption currently

	// find the current frame, or the one after it
	auto frame_it = keys.lower_bound(frame_number);

	// current frame is higher than any keyframe, use the last keyframe
	if (keys.end() == frame_it)
		--frame_it;

	// if this is after the current frame and not the first keyframe, use the previous one
	if (frame_number < frame_it->first && keys.begin() != frame_it)
		--frame_it;

	return frame_it->second;
}

float HermiteKeyHandler::GetFrame(FrameNumber frame_number) const {
	// assuming not empty, a safe assumption currently

	// find the current keyframe, or the one after it
	std::multimap<FrameNumber, KeyData>::const_iterator next = keys.lower_bound(frame_number);

	// current frame is higher than any keyframe, use the last keyframe
	if (keys.end() == next)
		--next;

	std::multimap<FrameNumber, KeyData>::const_iterator prev = next;

	// if this is after the current frame and not the first keyframe, use the previous one
	if (frame_number < prev->first && keys.begin() != prev)
		--prev;

	const float nf = next->first - prev->first;
	if (fabs(nf) < 0.01) {
		// same frame numbers, just return the first's value
		return prev->second.value;
	} else {
		// different frames, blend them together
		// this is a "Cubic Hermite spline" apparently

		frame_number = (frame_number < prev->first)
			               ? prev->first
			               : ((frame_number > next->first) ? next->first : frame_number);

		const float t = (frame_number - prev->first) / nf;

		// old curve-less code
		//return prev->second.value + (next->second.value - prev->second.value) * t;

		// curvy code from marcan, :p
		return
				prev->second.slope * nf * (t + powf(t, 3) - 2 * powf(t, 2)) +
				next->second.slope * nf * (powf(t, 3) - powf(t, 2)) +
				prev->second.value * (1 + (2 * powf(t, 3) - 3 * powf(t, 2))) +
				next->second.value * (-2 * powf(t, 3) + 3 * powf(t, 2));
	}
}

void Animator::ProcessHermiteKey(const KeyType& type, float value)
{
	std::cout << "unhandled key (" << GetName() << "): type: " << FourCC(type.type)
	<< " index: " << (int)type.index
	<< " target: " << (int)type.target
	<< " value: " << value
	<< '\n';
}

void Animator::ProcessStepKey(const KeyType& type, StepKeyHandler::KeyData data)
{
	std::cout << "unhandled key (" << GetName() << "): type: " << FourCC(type.type)
	<< " index: " << (int)type.index
	<< " target: " << (int)type.target
	<< " data:" << (int)data.data1 << " " << (int)data.data2
	<< '\n';
}

}
