#pragma once

#include <string>
#include <queue>
#include <thread>
#include <atomic>
#include <mutex>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

enum class StreamStatus
{
    kOk = 0,
    kEndOfStream
};

class StreamReader
{
public:
    StreamReader(const std::string& url, const AVMediaType& type);
    StreamStatus Read(AVFrame*& avframe);
    double GetTimeBase();

    ~StreamReader();

protected:
    AVFormatContext* pformatctx_;
    AVPacket* pkt_;
    AVCodecContext* pcodec_ctx_;
    int stream_index_;
    double time_base_;

    bool RcvInternalDecodedFrame(AVFrame*& frame);
};
