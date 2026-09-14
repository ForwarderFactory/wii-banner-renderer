#include "VideoEncoder.h"
#include <iostream>
#include <libavutil/log.h>

VideoEncoder::VideoEncoder(
	const std::string& output_path,
	int width,
	int height,
	int fps,
	const std::string& audio_path,
	OutputFormat format
)
	: width(width), height(height), fps(fps) {

	try {
		avformat_alloc_output_context2(&format_ctx, nullptr, nullptr, output_path.c_str());
		if (!format_ctx) {
			throw std::runtime_error("Could not create output context");
		}

		av_log_set_level(AV_LOG_QUIET);

		initVideoEncoder(format);

		if (!audio_path.empty()) {
			initAudioEncoder(audio_path);
		}

		if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
			if (avio_open(&format_ctx->pb, output_path.c_str(), AVIO_FLAG_WRITE) < 0) {
				throw std::runtime_error("Could not open output file: " + output_path);
			}
		}

		if (avformat_write_header(format_ctx, nullptr) < 0) {
			throw std::runtime_error("Error writing output file header");
		}

		std::cout << "VideoEncoder initialized: " << output_path << " (" << width << "x" << height << " @ " << fps << "fps)\n";
	} catch (const std::exception&) {
		cleanup();
		throw;
	}
}

void VideoEncoder::initVideoEncoder(OutputFormat format) {
	const AVCodec* codec = nullptr;
	const char* codec_name = nullptr;

	if (format == OutputFormat::WEBM) {
		codec_name = "libvpx-vp9";
	} else {
		codec_name = "libx264";
	}

	codec = avcodec_find_encoder_by_name(codec_name);
	if (!codec) {
		throw std::runtime_error(std::string("Codec not found: ") + codec_name);
	}

	video_stream = avformat_new_stream(format_ctx, codec);
	if (!video_stream) {
		throw std::runtime_error("Could not create video stream");
	}

	video_codec_ctx = avcodec_alloc_context3(codec);
	if (!video_codec_ctx) {
		throw std::runtime_error("Could not allocate video codec context");
	}

	video_codec_ctx->codec_id = codec->id;
	video_codec_ctx->width = width;
	video_codec_ctx->height = height;
	video_codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
	video_codec_ctx->time_base = {1, fps};
	video_codec_ctx->framerate = {fps, 1};

	if (format == OutputFormat::WEBM) {
		av_opt_set(video_codec_ctx->priv_data, "crf", "30", 0);
		video_codec_ctx->gop_size = fps;
	} else {
		av_opt_set(video_codec_ctx->priv_data, "crf", "23", 0);
		video_codec_ctx->profile = AV_PROFILE_H264_BASELINE;
		video_codec_ctx->level = 42;
		video_codec_ctx->gop_size = fps;
	}

	if (avcodec_open2(video_codec_ctx, codec, nullptr) < 0) {
		throw std::runtime_error("Could not open video codec");
	}

	if (avcodec_parameters_from_context(video_stream->codecpar, video_codec_ctx) < 0) {
		throw std::runtime_error("Could not copy video codec parameters");
	}

	video_frame = av_frame_alloc();
	if (!video_frame) {
		throw std::runtime_error("Could not allocate video frame");
	}

	video_frame->format = video_codec_ctx->pix_fmt;
	video_frame->width = width;
	video_frame->height = height;

	if (av_frame_get_buffer(video_frame, 0) < 0) {
		throw std::runtime_error("Could not allocate frame buffer");
	}

	av_samples_set_silence(video_frame->data, 0, video_frame->nb_samples, video_frame->ch_layout.nb_channels, static_cast<enum AVSampleFormat>(video_frame->format));

	sws_ctx = sws_getContext(
		width, height, AV_PIX_FMT_RGBA,
		video_codec_ctx->width, video_codec_ctx->height, AV_PIX_FMT_YUV420P,
		SWS_BILINEAR, nullptr, nullptr, nullptr
	);

	if (!sws_ctx) {
		throw std::runtime_error("Could not initialize pixel format conversion context");
	}

	packet = av_packet_alloc();
	if (!packet) {
		throw std::runtime_error("Could not allocate packet");
	}
}

void VideoEncoder::writeAudioFramesFromFile() {
	if (audio_stream_index == -1 || !audio_codec_ctx || !audio_encoder_ctx || !audio_fifo) {
		return;
	}

	AVPacket* pkt = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();
	if (!pkt || !frame) {
		throw std::runtime_error("Could not allocate internal structures");
	}

	while (av_read_frame(audio_input_ctx, pkt) >= 0) {
		if (pkt->stream_index != audio_stream_index) {
			av_packet_unref(pkt);
			continue;
		}

		if (avcodec_send_packet(audio_codec_ctx, pkt) < 0) {
			throw std::runtime_error("Error sending audio packet to decoder");
		}

		while (avcodec_receive_frame(audio_codec_ctx, frame) >= 0) {
			av_frame_unref(resampled_frame);
			av_channel_layout_copy(&resampled_frame->ch_layout, &audio_encoder_ctx->ch_layout);
			resampled_frame->sample_rate = audio_encoder_ctx->sample_rate;
			resampled_frame->format      = audio_encoder_ctx->sample_fmt;
			resampled_frame->nb_samples  = frame->nb_samples;

			if (av_frame_get_buffer(resampled_frame, 0) < 0) {
				throw std::runtime_error("Failed to allocate resampled frame data buffers");
			}

			const int64_t delay =
				swr_get_delay(swr_ctx, audio_codec_ctx->sample_rate);

			const int output_samples = static_cast<int>(
				av_rescale_rnd(
					delay + frame->nb_samples,
					audio_encoder_ctx->sample_rate,
					audio_codec_ctx->sample_rate,
					AV_ROUND_UP
				)
			);

			av_frame_unref(resampled_frame);

			av_channel_layout_copy(
				&resampled_frame->ch_layout,
				&audio_encoder_ctx->ch_layout
			);

			resampled_frame->sample_rate = audio_encoder_ctx->sample_rate;
			resampled_frame->format = audio_encoder_ctx->sample_fmt;
			resampled_frame->nb_samples = output_samples;

			if (av_frame_get_buffer(resampled_frame, 0) < 0) {
				throw std::runtime_error(
					"Failed to allocate resampled frame data buffers"
				);
			}

			const int converted = swr_convert(
				swr_ctx,
				resampled_frame->data,
				output_samples,
				(const uint8_t**)frame->data,
				frame->nb_samples
			);

			if (converted < 0) {
				throw std::runtime_error("Error processing audio resampling");
			}

			if (converted > 0) {
				if (av_audio_fifo_write(
						audio_fifo,
						reinterpret_cast<void **>(resampled_frame->data),
						converted
					) < converted)
				{
					throw std::runtime_error("Failed to write samples to FIFO queue");
				}
			}

			int frame_size = audio_encoder_ctx->frame_size;
			while (av_audio_fifo_size(audio_fifo) >= frame_size) {
				AVFrame* enc_frame = av_frame_alloc();
				av_channel_layout_copy(&enc_frame->ch_layout, &audio_encoder_ctx->ch_layout);
				enc_frame->sample_rate = audio_encoder_ctx->sample_rate;
				enc_frame->format      = audio_encoder_ctx->sample_fmt;
				enc_frame->nb_samples  = frame_size;

				if (av_frame_get_buffer(enc_frame, 0) < 0) {
					throw std::runtime_error("Failed to allocate encoder frame buffer");
				}

				if (av_audio_fifo_read(audio_fifo, reinterpret_cast<void **>(enc_frame->data), frame_size) < frame_size) {
					throw std::runtime_error("Failed to read samples from FIFO queue");
				}

				enc_frame->pts = audio_pts;
				audio_pts += frame_size;

				if (avcodec_send_frame(audio_encoder_ctx, enc_frame) < 0) {
					throw std::runtime_error("Error sending audio frame to encoder");
				}

				encodeAndWriteAudioFrame();
				av_frame_free(&enc_frame);
			}

			av_frame_unref(frame);
		}
		av_packet_unref(pkt);
	}

	int remaining_samples = av_audio_fifo_size(audio_fifo);
	if (remaining_samples > 0) {
		AVFrame* enc_frame = av_frame_alloc();
		av_channel_layout_copy(&enc_frame->ch_layout, &audio_encoder_ctx->ch_layout);
		enc_frame->sample_rate = audio_encoder_ctx->sample_rate;
		enc_frame->format      = audio_encoder_ctx->sample_fmt;
		enc_frame->nb_samples  = audio_encoder_ctx->frame_size;

		av_frame_get_buffer(enc_frame, 0);

		av_samples_set_silence(enc_frame->data, 0, enc_frame->nb_samples,
				   enc_frame->ch_layout.nb_channels, static_cast<enum AVSampleFormat>(enc_frame->format));

		av_audio_fifo_read(audio_fifo, reinterpret_cast<void **>(enc_frame->data), remaining_samples);

		enc_frame->pts = audio_pts;
		audio_pts += enc_frame->nb_samples;

		avcodec_send_frame(audio_encoder_ctx, enc_frame);
		encodeAndWriteAudioFrame();
		av_frame_free(&enc_frame);
	}

	avcodec_send_frame(audio_encoder_ctx, nullptr);
	encodeAndWriteAudioFrame();

	av_frame_free(&frame);
	av_packet_free(&pkt);
}

void VideoEncoder::initAudioEncoder(const std::string& audio_path) {
	if (avformat_open_input(&audio_input_ctx, audio_path.c_str(), nullptr, nullptr) < 0) {
		throw std::runtime_error("Could not open audio file: " + audio_path);
	}

	if (avformat_find_stream_info(audio_input_ctx, nullptr) < 0) {
		throw std::runtime_error("Could not find audio stream info");
	}

	audio_stream_index = -1;
	for (unsigned int i = 0; i < audio_input_ctx->nb_streams; i++) {
		if (audio_input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audio_stream_index = static_cast<int>(i);
			break;
		}
	}

	if (audio_stream_index == -1) {
		throw std::runtime_error("Could not find audio stream");
	}

	AVStream* input_audio_stream = audio_input_ctx->streams[audio_stream_index];
	const AVCodec* decoder = avcodec_find_decoder(input_audio_stream->codecpar->codec_id);
	if (!decoder) {
		throw std::runtime_error("Audio decoder codec not found");
	}

	audio_codec_ctx = avcodec_alloc_context3(decoder);
	if (!audio_codec_ctx) {
		throw std::runtime_error("Could not allocate audio decoder context");
	}

	if (avcodec_parameters_to_context(audio_codec_ctx, input_audio_stream->codecpar) < 0) {
		throw std::runtime_error("Could not copy audio decoder parameters");
	}

	if (avcodec_open2(audio_codec_ctx, decoder, nullptr) < 0) {
		throw std::runtime_error("Could not open audio decoder");
	}

	audio_stream = avformat_new_stream(format_ctx, nullptr);
	if (!audio_stream) {
		throw std::runtime_error("Could not create audio stream");
	}

	const AVCodec* encoder = nullptr;
	if (format_ctx->oformat->name) {
		std::string format_name(format_ctx->oformat->name);
		if (format_name.find("webm") != std::string::npos) {
			encoder = avcodec_find_encoder_by_name("libopus");
		} else {
			encoder = avcodec_find_encoder_by_name("aac");
		}
	}

	if (!encoder) {
		encoder = avcodec_find_encoder_by_name("aac");
	}

	AVCodecContext* audio_enc_ctx = avcodec_alloc_context3(encoder);
	if (!audio_enc_ctx) {
		throw std::runtime_error("Could not allocate audio encoder context");
	}

	audio_enc_ctx->sample_rate = 48000;

	int channels = audio_codec_ctx->ch_layout.nb_channels;
	av_channel_layout_default(&audio_enc_ctx->ch_layout, channels);

	audio_enc_ctx->time_base = AVRational{1, 48000};

	const enum AVSampleFormat *sample_fmts = nullptr;
	int ret = avcodec_get_supported_config(audio_enc_ctx, encoder,
							   AV_CODEC_CONFIG_SAMPLE_FORMAT,
							   0, reinterpret_cast<const void **>(&sample_fmts), nullptr);

	if (ret >= 0 && sample_fmts && sample_fmts[0] != AV_SAMPLE_FMT_NONE) {
		audio_enc_ctx->sample_fmt = sample_fmts[0];
	} else {
		audio_enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
	}

	if (format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
		audio_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	if (avcodec_open2(audio_enc_ctx, encoder, nullptr) < 0) {
		throw std::runtime_error("Could not open audio encoder");
	}

	if (avcodec_parameters_from_context(audio_stream->codecpar, audio_enc_ctx) < 0) {
		throw std::runtime_error("Could not copy audio encoder parameters");
	}

	this->audio_encoder_ctx = audio_enc_ctx;

	swr_ctx = swr_alloc();
	if (!swr_ctx) {
		throw std::runtime_error{"Could not allocate SwrContext"};
	}

	av_opt_set_chlayout(swr_ctx, "in_chlayout",     &audio_codec_ctx->ch_layout, 0);
	av_opt_set_int(swr_ctx,      "in_sample_rate",   audio_codec_ctx->sample_rate, 0);
	av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt",  audio_codec_ctx->sample_fmt, 0);

	av_opt_set_chlayout(swr_ctx, "out_chlayout",    &audio_encoder_ctx->ch_layout, 0);
	av_opt_set_int(swr_ctx,      "out_sample_rate",  audio_encoder_ctx->sample_rate, 0);
	av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", audio_encoder_ctx->sample_fmt, 0);

	if (swr_init(swr_ctx) < 0) {
		throw std::runtime_error("Could not initialize SwrContext");
	}

	resampled_frame = av_frame_alloc();

	audio_fifo = av_audio_fifo_alloc(audio_encoder_ctx->sample_fmt, audio_encoder_ctx->ch_layout.nb_channels, 1);

	if (!audio_fifo) {
		throw std::runtime_error{"Could not allocate AVAudioFifo"};
	}
}

void VideoEncoder::writeVideoFrame(const uint8_t* rgba_data) {
    if (finished) {
        throw std::runtime_error("VideoEncoder already finished");
    }

    constexpr int bpp = 4;

    const uint8_t* srcPtr = rgba_data;
    int srcStride = width * bpp;
    int srcH = video_codec_ctx->height;

    const uint8_t* srcData[1] = { srcPtr };
    int srcLinesize[1] = { srcStride };

    sws_scale(sws_ctx, srcData, srcLinesize, 0, srcH,
              video_frame->data, video_frame->linesize);

    video_frame->pts = frame_count++;

    if (avcodec_send_frame(video_codec_ctx, video_frame) < 0) {
        throw std::runtime_error("Error sending frame to encoder");
    }

    encodeAndWriteVideoFrame();
}

void VideoEncoder::encodeAndWriteVideoFrame() const {
	while (true) {
		av_packet_unref(packet);
		int ret = avcodec_receive_packet(video_codec_ctx, packet);

		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			break;
		}

		if (ret < 0) {
			throw std::runtime_error("Error encoding video frame");
		}

		packet->stream_index = video_stream->index;
		av_packet_rescale_ts(packet, video_codec_ctx->time_base, video_stream->time_base);

		if (av_interleaved_write_frame(format_ctx, packet) < 0) {
			throw std::runtime_error("Error writing video frame");
		}
	}
}

void VideoEncoder::finish() {
	if (finished) {
		return;
	}

	if (avcodec_send_frame(video_codec_ctx, nullptr) < 0) {
		throw std::runtime_error("Error flushing video encoder");
	}

	encodeAndWriteVideoFrame();

	if (audio_input_ctx) {
		writeAudioFramesFromFile();
	}

	if (av_write_trailer(format_ctx) < 0) {
		throw std::runtime_error("Error writing output file trailer");
	}

	if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
		avio_closep(&format_ctx->pb);
	}

	finished = true;
	cleanup();

	std::cout << "VideoEncoder finished. Wrote " << frame_count << " video frames.\n";
}

void VideoEncoder::encodeAndWriteAudioFrame() const {
	AVPacket* pkt = av_packet_alloc();
	if (!pkt) {
		throw std::runtime_error("Could not allocate packet");
	}

	while (true) {
		int ret = avcodec_receive_packet(audio_encoder_ctx, pkt);

		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			break;
		}

		if (ret < 0) {
			av_packet_free(&pkt);
			throw std::runtime_error("Error encoding audio frame");
		}

		pkt->stream_index = audio_stream->index;
		av_packet_rescale_ts(pkt, audio_encoder_ctx->time_base, audio_stream->time_base);

		if (av_interleaved_write_frame(format_ctx, pkt) < 0) {
			av_packet_free(&pkt);
			throw std::runtime_error("Error writing audio frame");
		}

		av_packet_unref(pkt);
	}

	av_packet_free(&pkt);
}

void VideoEncoder::cleanup() {
    if (video_frame) { av_frame_free(&video_frame); }
    if (audio_frame) { av_frame_free(&audio_frame); }
    if (packet) { av_packet_free(&packet); }

    if (sws_ctx) { sws_freeContext(sws_ctx); sws_ctx = nullptr; }

    if (video_codec_ctx) { avcodec_free_context(&video_codec_ctx); }
    if (audio_codec_ctx) { avcodec_free_context(&audio_codec_ctx); audio_codec_ctx = nullptr; }

    if (format_ctx) {
        if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&format_ctx->pb);
        }
        avformat_free_context(format_ctx);
        format_ctx = nullptr;
    }

    if (audio_input_ctx) { avformat_close_input(&audio_input_ctx); audio_input_ctx = nullptr; }

    if (swr_ctx) { swr_free(&swr_ctx); swr_ctx = nullptr; }

    if (resampled_frame) { av_frame_free(&resampled_frame); }

    if (audio_fifo) { av_audio_fifo_free(audio_fifo); audio_fifo = nullptr; }
}

VideoEncoder::~VideoEncoder() {
	try {
		if (!finished) {
			finish();
		}
	} catch (const std::exception& e) {
		std::cerr << "Error in VideoEncoder destructor: " << e.what() << "\n";
		cleanup();
	}
	cleanup();
}
