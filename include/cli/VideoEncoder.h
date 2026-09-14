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
