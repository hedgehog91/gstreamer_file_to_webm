#include <stdexcept>
#include <iostream>

#include "StreamReader.h"

using namespace std;

StreamReader::StreamReader(const std::string& url, const AVMediaType& type)
    : pformatctx_(nullptr),
      pkt_(nullptr),
      pcodec_ctx_(nullptr)
{
    pkt_ = av_packet_alloc();

    if(avformat_open_input(&pformatctx_, url.c_str(), NULL, NULL) < 0)
        throw invalid_argument("Couldn't open url");

    if(avformat_find_stream_info(pformatctx_, NULL) < 0)
        throw runtime_error("Couldn't find stream information");

    AVCodec *codec;
    stream_index_ = av_find_best_stream(pformatctx_, type, -1, -1, &codec, 0);
    if (stream_index_ < 0)
        throw runtime_error("Couldn't find best stream");

    codec = avcodec_find_decoder(codec->id);
    if (!codec)
        throw runtime_error("Couldn't find codec");

    pcodec_ctx_ = avcodec_alloc_context3(codec);
    if (!pcodec_ctx_)
        throw runtime_error("Couldn't allocate codec context");

    if (avcodec_parameters_to_context(pcodec_ctx_, pformatctx_->streams[stream_index_]->codecpar) < 0)
        throw runtime_error("Couldn't copy codec context");

    if (avcodec_open2(pcodec_ctx_, codec, NULL) < 0)
        throw runtime_error("Couldn't open codec");

    time_base_ = av_q2d(pformatctx_->streams[stream_index_]->time_base);
}

bool StreamReader::RcvInternalDecodedFrame(AVFrame*& frame)
{
    int res = avcodec_receive_frame(pcodec_ctx_, frame);
    if (res == AVERROR(EAGAIN) || res == AVERROR_EOF)
        return false;
    else if (res < 0)
    {
        cout << "Error during decoding";
        return false;
    }

    return true;
}

StreamStatus StreamReader::Read(AVFrame*& frame)
{
    int res;
    bool valid = RcvInternalDecodedFrame(frame);

    while(!valid)
    {
        if ((av_read_frame(pformatctx_, pkt_) >= 0))
        {
            if (pkt_->stream_index == stream_index_ && pkt_->size)
            {
                if ((res = avcodec_send_packet(pcodec_ctx_, pkt_)) < 0)
                    throw runtime_error("Error submitting the packet to the decoder");
                else
                    valid = RcvInternalDecodedFrame(frame);
            }
            av_packet_unref(pkt_);
        }
        else
            return StreamStatus::kEndOfStream;
    }

    return valid ? StreamStatus::kOk : StreamStatus::kEndOfStream;
}

double StreamReader::GetTimeBase()
{
    return time_base_;
}

StreamReader::~StreamReader()
{
    if (pformatctx_)
        avformat_close_input(&pformatctx_);
    if (pcodec_ctx_)
        avcodec_free_context(&pcodec_ctx_);
    if (pkt_)
        av_packet_free(&pkt_);
}
