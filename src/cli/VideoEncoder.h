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
#pragma once

#include <string>
#include <cstdint>
#include <stdexcept>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

class VideoEncoder {
public:
	enum class OutputFormat {
		MP4,
		WEBM
	};

	VideoEncoder(
		const std::string& output_path,
		int width,
		int height,
		int fps,
		const std::string& audio_path = "",
		OutputFormat format = OutputFormat::MP4
	);
	
	~VideoEncoder();

	void writeVideoFrame(const uint8_t* rgba_data);
	void finish();

private:
	AVFormatContext* format_ctx = nullptr;
	AVCodecContext* video_codec_ctx = nullptr;
	AVCodecContext* audio_codec_ctx = nullptr;
	AVCodecContext* audio_encoder_ctx = nullptr;
	
	AVStream* video_stream = nullptr;
	AVStream* audio_stream = nullptr;
	
	AVFrame* video_frame = nullptr;
	AVFrame* audio_frame = nullptr;
	AVPacket* packet = nullptr;

	SwrContext* swr_ctx = nullptr;
	AVFrame* resampled_frame = nullptr;
	AVAudioFifo* audio_fifo = nullptr;
	
	SwsContext* sws_ctx = nullptr;
	
	int frame_count = 0;
	int width = 0;
	int height = 0;
	int fps = 0;
	bool finished = false;
	
	AVFormatContext* audio_input_ctx = nullptr;
	int audio_stream_index = -1;
	int64_t audio_pts = 0;
	
	void initVideoEncoder(OutputFormat format);
	void initAudioEncoder(const std::string& audio_path);
	void writeAudioFramesFromFile();
	void encodeAndWriteVideoFrame() const;
	void encodeAndWriteAudioFrame() const;
	void cleanup();
};
