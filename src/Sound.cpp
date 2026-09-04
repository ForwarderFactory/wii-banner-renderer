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

#include <iostream>
#include <cstring>
#include <fstream>

// from dolphin
#include "CommonTypes.h"

#include "Sound.h"
#include "LZ77.h"
#include "Endian.h"
#include "Types.h"

namespace WiiBanner
{

enum BinaryMagic : u32
{
	BINARY_MAGIC_WAV = MAKE_FOURCC('R', 'I', 'F', 'F'),
	BINARY_MAGIC_AIFF = MAKE_FOURCC('F', 'O', 'R', 'M'),
	BINARY_MAGIC_BNS = MAKE_FOURCC('B', 'N', 'S', ' '),

	BINARY_MAGIC_BNS_INFO = MAKE_FOURCC('I', 'N', 'F', 'O'),
	BINARY_MAGIC_BNS_DATA = MAKE_FOURCC('D', 'A', 'T', 'A')
};

enum SoundFormat
{
	FORMAT_BNS,
	FORMAT_WAV,
	FORMAT_AIFF
};
SoundFormat format;

struct BNS
{
	struct BNSHeader
	{
		FourCC magic;
		u32 endian{};
		u32 size{};
		u32 unk{};
		u32 info_off{};
		u32 info_len{};
		u32 data_off{};
		u32 data_len{};
	};

	struct BNSInfo
	{
		FourCC magic;
		u32 size{};
		u8 codec{};
		u8 loop{};
		u8 channel_count{};
		u8 unk{};
		u16 sample_rate{};
		u16 unk2{};
		u32 loop_start{}; // do we care?
		u32 sample_count{};
		// TODO SO MANY unknowns
		u32 right_start{};
		s16 coefs[2][16]{};
	};

	struct BNSData
	{
		FourCC magic;
		u32 size{};
	};

	struct DSPRegs
	{
		s16 coefs[16];
		u8 pred_scale;
		s16 yn1;
		s16 yn2;

		void ClearHistory() { yn1 = yn2 = 0; }
	};

	std::streamoff start{};
	BNSHeader hdr;
	BNSInfo info;
	BNSData data;
	u8 *adpcm;
	DSPRegs dsp_regs{};

	BNS() : adpcm(nullptr) {}
	~BNS() { delete[] adpcm; }

	void Open(std::istream& in)
	{
		start = in.tellg();

		in >> hdr.magic >> BE >> hdr.endian >> hdr.size >> hdr.unk
			>> hdr.info_off >> hdr.info_len >> hdr.data_off >> hdr.data_len;

		in.seekg(start + hdr.info_off, std::istream::beg);
		in >> info.magic >> BE >> info.size >> info.codec >> info.loop
			>> info.channel_count >> info.unk >> info.sample_rate
			>> info.unk2 >> info.loop_start >> info.sample_count;

		in.seekg(6 * sizeof(u32), std::istream::cur);

		if (info.channel_count == 1)
		{
			ReadBEArray(in, info.coefs[0], 16);
		}
		else if (info.channel_count == 2)
		{
			// L, R
			in.seekg(1 * sizeof(u32), std::istream::cur);
			in >> BE >> info.right_start;
			in.seekg(2 * sizeof(u32), std::istream::cur);
			ReadBEArray(in, info.coefs[0], 16);
			in.seekg(4 * sizeof(u32), std::istream::cur);
			ReadBEArray(in, info.coefs[1], 16);
		}
		else
		{
			std::cout << static_cast<int>(info.channel_count) << " channels unsupported!\n";
		}

		in.seekg(start + hdr.data_off, std::istream::beg);
		in >> data.magic >> BE >> data.size;
		// chop off the magic + size words
		hdr.data_len = data.size -= 8;
		adpcm = new u8[data.size];
		ReadBEArray(in, adpcm, data.size);

		if ((hdr.info_len != info.size)
			|| (hdr.data_len != data.size)
			|| (info.magic != BINARY_MAGIC_BNS_INFO)
			|| (data.magic != BINARY_MAGIC_BNS_DATA))
			std::cout << "sound.bin appears invalid\n";
	}

	[[nodiscard]] u8  GetChannelsCount() const { return info.channel_count; }
	[[nodiscard]] u32 GetSamplesCount() const { return info.sample_count; }
	[[nodiscard]] u16 GetSampleRate() const { return info.sample_rate; }
	[[nodiscard]] u8 GetLoop() const {
		return info.loop;
	}
	[[nodiscard]] u32 GetLoopStart() const {
		return info.loop_start;
	}

	u32 DecodeChannelToPCM(s16 *pcm, u32 pcm_start_pos,
		u32 adpcm_start_pos, u32 adpcm_end_pos)
	{
		u32 pcm_pos = pcm_start_pos;
		u32 adpcm_pos = adpcm_start_pos;

		while (adpcm_pos != adpcm_end_pos)
		{
			if ((adpcm_pos & 15) == 0)
			{
				dsp_regs.pred_scale = adpcm[(adpcm_pos & ~15) / 2];
				adpcm_pos += 2;
			}

			int scale = 1 << (dsp_regs.pred_scale & 0xf);
			int coef_idx = (dsp_regs.pred_scale >> 4) & 7;

			s32 coef1 = dsp_regs.coefs[coef_idx * 2];
			s32 coef2 = dsp_regs.coefs[coef_idx * 2 + 1];

			for (int i = 0; i < 14; i++, adpcm_pos++)
			{

				// unpack a nybble
				s16 nybble = (adpcm_pos & 1) ?
					adpcm[adpcm_pos / 2] & 0xf : adpcm[adpcm_pos / 2] >> 4;

				// sign extension
				if (nybble >= 8)
					nybble -= 16;

				// calc the sample
				int sample = (scale * nybble) +
					((0x400 + coef1 * dsp_regs.yn1 + coef2 * dsp_regs.yn2) >> 11);

				// clamp
				if (sample > 0x7FFF)
					sample = 0x7FFF;
				else if (sample < -0x7FFF)
					sample = -0x7FFF;

				// history
				dsp_regs.yn2 = dsp_regs.yn1;
				dsp_regs.yn1 = sample;

				if (pcm_pos >= info.sample_count * info.channel_count)
					return pcm_pos;

				if (pcm)
					pcm[pcm_pos] = sample;

				pcm_pos++;

				if (info.channel_count == 2)
					++pcm_pos;
			}
		}

		return pcm_pos;
	}
static u32 DecodeSampleCountForChannel(u32 adpcm_start_pos, u32 adpcm_end_pos)
{
    u32 count = 0;
    u32 adpcm_pos = adpcm_start_pos;

    while (adpcm_pos != adpcm_end_pos * 2)
    {
        if ((adpcm_pos & 15) == 0)
        {
            adpcm_pos += 2; // skip predictor/scale
        }

        for (int i = 0; i < 14; i++, adpcm_pos++)
        {
            count++;
        }
    }

    return count;
}

[[nodiscard]] u32 GetDecodedSampleCount() const
{
    if (info.channel_count == 1)
    {
        return DecodeSampleCountForChannel(0, data.size);
    }

    u32 left = DecodeSampleCountForChannel(0, info.right_start);

    return left * 2;
}

u32 DecodeToPCM(s16* pcm)
{
    dsp_regs.ClearHistory();
    memcpy(dsp_regs.coefs, info.coefs[0], 16 * 2);

    u32 end = DecodeChannelToPCM(
        pcm,
        0,
        0,
        (info.channel_count == 2) ? info.right_start * 2 : data.size * 2
    );

    if (info.channel_count == 2)
    {
        dsp_regs.ClearHistory();
        memcpy(dsp_regs.coefs, info.coefs[1], 16 * 2);

        end = DecodeChannelToPCM(
            pcm,
            1,
            info.right_start * 2,
            data.size * 2
        );
    }



    return end;
}
};

	// get metadata and shit so we can loop wavs
	bool Sound::ParseWAV() {
		const uint8_t* data = rawData.data();
		size_t size = rawData.size();

		if (size < 12) return false;

		if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0)
			return false;

		size_t offset = 12;

		const uint8_t* dataPtr = nullptr;
		uint32_t dataSize = 0;
		uint16_t bitsPerSample = 0;

		while (offset + 8 <= size)
		{
			const char* chunkId = reinterpret_cast<const char*>(data + offset);
			uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(data + offset + 4);
			offset += 8;

			if (memcmp(chunkId, "fmt ", 4) == 0)
			{
				if (chunkSize < 16) return false;

				uint16_t audioFormat = *reinterpret_cast<const uint16_t*>(data + offset + 0);
				channels             = *reinterpret_cast<const uint16_t*>(data + offset + 2);
				sampleRate           = *reinterpret_cast<const uint32_t*>(data + offset + 4);
				bitsPerSample        = *reinterpret_cast<const uint16_t*>(data + offset + 14);

				if (audioFormat != 1) {
					std::cerr << "Only PCM WAV supported\n";
					return false;
				}
			}
			else if (memcmp(chunkId, "data", 4) == 0)
			{
				dataPtr  = data + offset;
				dataSize = chunkSize;
			}
			else if (memcmp(chunkId, "smpl", 4) == 0)
			{
				if (chunkSize < 36) {
					offset += chunkSize;
					continue;
				}

				uint32_t numLoops = *reinterpret_cast<const uint32_t*>(data + offset + 28);

				if (numLoops > 0) {
					const uint8_t* loopPtr = data + offset + 36;

					uint32_t start = *reinterpret_cast<const uint32_t*>(loopPtr + 8);
					uint32_t end   = *reinterpret_cast<const uint32_t*>(loopPtr + 12);

					loop_start = start;
					sampleCount = end;
					has_loop = true;
				}
			}

			offset += chunkSize;
			if (chunkSize % 2 == 1) offset++;
		}

		if (!dataPtr || dataSize == 0 || sampleRate == 0 || channels == 0)
			return false;

		if (bitsPerSample != 16) {
			std::cerr << "Only 16-bit WAV supported\n";
			return false;
		}

		size_t totalSamples = dataSize / sizeof(int16_t);

		samples.resize(totalSamples);
		memcpy(samples.data(), dataPtr, dataSize);

		sampleCount = totalSamples / channels;

		return true;
	}

bool Sound::Load(std::istream& file)
{
    FourCC magic;
    u32 file_len = 0;

    LZ77Decompressor decomp(file);
    std::istream& in = decomp.GetStream();

    const std::streamoff start = in.tellg();
    in >> magic;

    if (magic == BINARY_MAGIC_WAV) {
        std::cout << "WAV detected\n";

        in >> LE >> file_len;
        in.seekg(start, std::istream::beg);

        rawData.resize(file_len);
        in.read(reinterpret_cast<char *>(rawData.data()), file_len);

        format = FORMAT_WAV;

    	return ParseWAV();
    }
    if (magic == BINARY_MAGIC_AIFF)
    {
	    std::cout << "AIFF detected\n";

	    in >> BE >> file_len;
	    in.seekg(start, std::istream::beg);

	    rawData.resize(file_len);
	    in.read(reinterpret_cast<char *>(rawData.data()), file_len);

	    format = FORMAT_AIFF;
	    return true;
    }
    if (magic == BINARY_MAGIC_BNS) {
	    std::cout << "BNS detected\n";

	    format = FORMAT_BNS;

	    in.seekg(start, std::istream::beg);

	    BNS bns_file;

	    std::cout << "Opening BNS...\n";
	    bns_file.Open(in);
	    std::cout << "BNS opened\n";

	    sampleCount = bns_file.GetSamplesCount();
	    channels    = bns_file.GetChannelsCount();
	    sampleRate  = bns_file.GetSampleRate();
	    loop_start  = bns_file.GetLoopStart();
	    loop_end    = sampleCount;
    	has_loop   = bns_file.GetLoop();

    	std::cout << "Expected PCM samples: "
			<< bns_file.GetDecodedSampleCount()
			<< "\n";

    	std::cout << "BNS header samples: "
				  << sampleCount * channels
				  << "\n";

	    std::cout
			    << "samples=" << sampleCount
			    << " channels=" << channels
			    << " rate=" << sampleRate
			    << "\n";

	    //if (sampleCount == 0 || channels == 0)
	    //{
		//    std::cout << "Invalid BNS\n";
		//    return false;
	    //}

	    std::cout << "Resizing PCM buffer\n";
	    samples.resize(sampleCount * channels);

	    std::cout << "Decoding PCM...\n";
	    u32 written = bns_file.DecodeToPCM(samples.data());

	    std::cout << "Allocated samples: " << samples.size() << "\n";
	    std::cout << "Written samples: " << written << "\n";

	    samples.resize(written);

    	std::cout << "Decoded samples: " << samples.size() << "\n";
    	std::cout << "Channels: " << channels << "\n";
    	std::cout << "Remainder: " << (samples.size() % channels) << "\n";

	    std::cout << "Decode complete\n";

	    this->channels = channels;

	    std::cout << "channels assigned\n";

	    this->sampleRate = sampleRate;

	    std::cout << "rate assigned\n";

	    std::cout << "Returning true from Sound::Load\n";

	    return true;
    }

    std::cout << "Unknown format\n";
    return false;
}

void Sound::WritePCMAsWAV(const std::string& path,
                   const std::vector<int16_t>& samples,
                   uint16_t channels,
                   uint32_t sampleRate)
{
    std::ofstream out(path, std::ios::binary);

    uint32_t dataSize   = samples.size() * sizeof(int16_t);
    uint32_t byteRate   = sampleRate * channels * sizeof(int16_t);
    uint16_t blockAlign = channels * sizeof(int16_t);
    uint16_t bitsPerSample = 16;

    // RIFF header
    out.write("RIFF", 4);
    uint32_t chunkSize = 36 + dataSize;
    out.write(reinterpret_cast<char*>(&chunkSize), 4);
    out.write("WAVE", 4);

    // fmt chunk
    out.write("fmt ", 4);
    uint32_t subchunk1Size = 16;
    uint16_t audioFormat = 1; // PCM
    out.write(reinterpret_cast<char*>(&subchunk1Size), 4);
    out.write(reinterpret_cast<char*>(&audioFormat), 2);
    out.write(reinterpret_cast<char*>(&channels), 2);
    out.write(reinterpret_cast<char*>(&sampleRate), 4);
    out.write(reinterpret_cast<char*>(&byteRate), 4);
    out.write(reinterpret_cast<char*>(&blockAlign), 2);
    out.write(reinterpret_cast<char*>(&bitsPerSample), 2);

    // data chunk
    out.write("data", 4);
    out.write(reinterpret_cast<char*>(&dataSize), 4);
    out.write(reinterpret_cast<const char*>(samples.data()), dataSize);
}


void Sound::WriteWAV(const std::string& path) {
    //std::cout << "called" << std::flush;
    if (format == FORMAT_WAV) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<char *>(rawData.data()), rawData.size());
        return;
    }

    if (format == FORMAT_AIFF)
    {
        std::cout << "AIFF not converted yet\n";
        return;
    }

    WritePCMAsWAV(path, samples, channels, sampleRate);
}

void Sound::WriteWAVLooped(const std::string& path, double seconds)
{
    if (sampleRate == 0 || channels == 0)
        throw std::runtime_error("Invalid audio format");

    if (samples.empty())
        throw std::runtime_error("No PCM samples loaded");

    if (sampleCount == 0)
        throw std::runtime_error("sampleCount is zero");

    const auto target_frames = static_cast<size_t>(seconds * sampleRate);
    const size_t target_samples = target_frames * channels;

	// i guess homebrew channel is retarded or something because its the only test subject that has this fucking issue
	// if anyone has a better solution to this pls fix my shit
	if (samples.size() % channels != 0)
		samples.resize((samples.size() / channels) * channels);

	const size_t total_frames = samples.size() / channels;
    const size_t total_samples = total_frames * channels;

    if (samples.size() < total_samples)
        throw std::runtime_error("samples buffer smaller than expected");

    size_t loop_start_frame = std::min(static_cast<size_t>(loop_start), total_frames);
    size_t loop_end_frame   = total_frames;

    if (has_loop && loop_start_frame >= loop_end_frame)
        has_loop = false;

    std::vector<int16_t> output;
    output.reserve(target_samples);

    size_t intro_frames = std::min(loop_start_frame, target_frames);
    size_t intro_samples = intro_frames * channels;

    output.insert(
        output.end(),
        samples.begin(),
        samples.begin() + intro_samples
    );

    if (has_loop) {
        const size_t loop_frames = loop_end_frame - loop_start_frame;
        //const size_t loop_samples = loop_frames * channels;

        const size_t loop_start_sample = loop_start_frame * channels;

        while (output.size() < target_samples) {
            size_t remaining_samples = target_samples - output.size();
            size_t remaining_frames  = remaining_samples / channels;

            if (remaining_frames == 0)
                break;

            size_t copy_frames = std::min(remaining_frames, loop_frames);
            size_t copy_samples = copy_frames * channels;

            const size_t src_start = loop_start_sample;
            const size_t src_end   = src_start + copy_samples;

            if (src_end > samples.size())
                throw std::runtime_error("out of bounds");

            output.insert(
                output.end(),
                samples.begin() + src_start,
                samples.begin() + src_end
            );
        }
    }
    else {
    	const size_t copy_samples =
			std::min(target_samples, samples.size());

    	output.insert(
			output.end(),
			samples.begin(),
			samples.begin() + copy_samples
		);

    	output.resize(target_samples, 0);
    }

    if (output.size() > target_samples)
        output.resize(target_samples);

    if (output.size() % channels != 0)
        throw std::runtime_error("output not aligned");

    WritePCMAsWAV(path, output, channels, sampleRate);
}

double Sound::GetDurationSeconds() const {
	if (sampleRate == 0) throw std::runtime_error("sample rate is zero");
	if (channels == 0) throw std::runtime_error("channels is zero");
    return static_cast<double>(samples.size()) / sampleRate / channels;
}
}
