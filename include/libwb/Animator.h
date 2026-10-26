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

#ifndef WII_BNR_ANIMATOR_H_
#define WII_BNR_ANIMATOR_H_

#include <map>
#include <tuple>
#include <cstring>

#include <libwb/Types.h>

namespace wb
{
	class Layout;

	typedef float FrameNumber;

enum AnimationType : uint32_t
{
	ANIMATION_TYPE_PANE = MAKE_FOURCC('R', 'L', 'P', 'A'),
	ANIMATION_TYPE_TEXTURE_SRT = MAKE_FOURCC('R', 'L', 'T', 'S'),
	ANIMATION_TYPE_VISIBILITY = MAKE_FOURCC('R', 'L', 'V', 'I'),
	ANIMATION_TYPE_VERTEX_COLOR = MAKE_FOURCC('R', 'L', 'V', 'C'),
	ANIMATION_TYPE_MATERIAL_COLOR = MAKE_FOURCC('R', 'L', 'M', 'C'),
	ANIMATION_TYPE_TEXTURE_PALETTE = MAKE_FOURCC('R', 'L', 'T', 'P'),
	ANIMATION_TYPE_IND_MATERIAL = MAKE_FOURCC('R', 'L', 'I', 'M'),
};

struct KeyType
{
	KeyType(AnimationType _type, uint8_t _index, uint8_t _target)
		: type(_type)
		, index(_index)
		, target(_target)
	{}

	bool operator<(const KeyType& rhs) const
	{
		return std::tie(type, index, target) < std::tie(rhs.type, rhs.index, rhs.target);
	}

	const AnimationType type;
	const uint8_t index, target;
};

class StepKeyHandler
{
public:
	void Load(std::istream& file, uint16_t count);

	struct KeyData
	{
		uint8_t data1, data2;
	};

	[[nodiscard]] KeyData GetFrame(FrameNumber frame_number) const;

private:
	std::map<FrameNumber, KeyData> keys;
};

class HermiteKeyHandler
{
public:
	void Load(std::istream& file, uint16_t count);

	struct KeyData
	{
		float value, slope;
	};

	[[nodiscard]] float GetFrame(FrameNumber frame_number) const;

	void DebugPrint() const;

private:
	std::multimap<FrameNumber, KeyData> keys;
};

inline uint8_t ClampToByte(float value)
{
	if (!(value > 0.f))
		return 0;
	if (value >= 255.f)
		return 255;
	return static_cast<uint8_t>(value);
}

class Animator : public Named
{
public:
	enum
	{
		NAME_LENGTH = 20
	};

	static FrameNumber LoadAnimators(std::istream& file, Layout& layout, uint8_t key_set);
	void LoadKeyFrames(std::istream& file, uint8_t tag_count, std::streamoff origin, uint8_t key_set);

	virtual void SetFrame(FrameNumber frame, uint8_t key_set);

	void DebugPrintKeys() const;

protected:
	Animator() = default;

	virtual void ProcessHermiteKey(const KeyType& type, float value);
	virtual void ProcessStepKey(const KeyType& type, StepKeyHandler::KeyData data);

private:
	struct
	{
		std::map<KeyType, StepKeyHandler> step_keys;
		std::map<KeyType, HermiteKeyHandler> hermite_keys;
	} keys[2];
};

}

#endif