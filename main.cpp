#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

#include <string>
#include <iostream>
#include <memory>
#include <sys/time.h>

#include "StreamReader.h"

using namespace std;

string GetTime()
{
    char time_buffer[64];
    struct timeval t;
    gettimeofday(&t, NULL);
    int64_t tUs = ((int64_t) t.tv_sec) * 1000000 + ((int64_t) t.tv_usec);
    time_t now = (time_t)(tUs/1000000);
    int us = tUs%1000000;
    struct tm* today = localtime(&now);

    sprintf(time_buffer,
        "%04d%02d%02d %02d:%02d:%02d.%06d",
        today->tm_year + 1900,
        today->tm_mon + 1,
        today->tm_mday,
        today->tm_hour,
        today->tm_min,
        today->tm_sec,
        us);

    return string(time_buffer);
}

static void on_pad_added(GstElement *src, GstPad *new_pad, gpointer data)
{
  GstPad *sink_pad = gst_element_get_static_pad(static_cast<GstElement*>(data), "sink");
  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

  /* If our converter is already linked, we have nothing to do here */
  if (gst_pad_is_linked (sink_pad)) {
    g_print ("We are already linked. Ignoring.\n");
    goto exit;
  }

  /* Check the new pad's type */
  new_pad_caps = gst_pad_get_current_caps (new_pad);
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type = gst_structure_get_name (new_pad_struct);
  if (!g_str_has_prefix (new_pad_type, "video/x-h264")) {
    g_print ("It has type '%s' which is not x264 video. Ignoring.\n", new_pad_type);
    goto exit;
  }

  /* Attempt the link */
  ret = gst_pad_link (new_pad, sink_pad);
  if (GST_PAD_LINK_FAILED (ret)) {
    g_print ("Type is '%s' but link failed.\n", new_pad_type);
  } else {
    g_print ("Link succeeded (type '%s').\n", new_pad_type);
  }

exit:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);

  /* Unreference the sink pad */
  gst_object_unref (sink_pad);
}

FILE* file;

static GstFlowReturn on_new_sample (GstElement *sink, gpointer *data)
{
    GstSample *sample;
    GstBuffer *buf;
    GstMapInfo map;

    /* Retrieve the buffer */
    g_signal_emit_by_name (sink, "pull-sample", &sample);
    if (sample) {
      buf = gst_sample_get_buffer(sample);
      gst_buffer_map (buf, &map, GST_MAP_READ);

      //cout << "time " << (float)GST_BUFFER_PTS(buf)/(float)GST_SECOND << endl;

      fwrite(map.data, sizeof (uint8_t), map.size, file);
      gst_sample_unref (sample);

      return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}

std::shared_ptr<StreamReader> audio;
std::shared_ptr<StreamReader> video;
AVFrame* audio_frame;
AVFrame* video_frame;

GstFlowReturn OnNeedDataVideo(GstElement *appsrc, guint, gpointer)
{
    GstBuffer *buffer;
    GstMapInfo info;
    GstFlowReturn ret;

    StreamStatus status = video->Read(video_frame);
    if (status == StreamStatus::kEndOfStream)
    {
        g_signal_emit_by_name (appsrc, "end-of-stream", &ret);
        return GST_FLOW_OK;
    }

    size_t size = video_frame->width * video_frame->height * 1.5;
    buffer = gst_buffer_new_allocate(NULL, size, NULL);
    gst_buffer_map(buffer, &info, GST_MAP_WRITE);

    uint8_t *avY = video_frame->data[0];
    uint8_t *avU = video_frame->data[1];
    uint8_t *avV = video_frame->data[2];
    int ptr = 0;

    for (int i = 0; i < video_frame->height; i++)
    {
        memcpy(info.data+ptr, avY, video_frame->width);
        avY += video_frame->linesize[0];
        ptr += video_frame->width;
    }

    for (int i = 0; i < video_frame->height/2; i++)
    {
        memcpy(info.data+ptr, avU, video_frame->width/2);
        avU += video_frame->linesize[1];
        ptr += video_frame->width/2;
    }

    for (int i = 0; i < video_frame->height/2; i++)
    {
        memcpy(info.data+ptr, avV, video_frame->width/2);
        avV += video_frame->linesize[2];
        ptr += video_frame->width/2;
    }

    GST_BUFFER_DURATION(buffer) = video_frame->pkt_duration * GST_SECOND * video->GetTimeBase();
    GST_BUFFER_PTS(buffer) = video_frame->pts * GST_SECOND * video->GetTimeBase();

    /*
    cout << "before pts " << (float) GST_BUFFER_PTS(buffer) / (float) GST_SECOND << endl;
    uint64_t pts_ms = video_frame->pts * video->GetTimeBase() * 1000.0;
    uint64_t remainder = pts_ms % 20;
    if (remainder > 0 && remainder < 7)
        GST_BUFFER_PTS(buffer) += ((7 - remainder) / 1000.0) * GST_SECOND;

    cout << "after pts " << (float) GST_BUFFER_PTS(buffer) / (float) GST_SECOND << endl;
    */
    g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);

    //cout << "OnNeedDataVideo pts " << video_frame->pts * video->GetTimeBase() << endl;

    return GST_FLOW_OK;
}

GstFlowReturn OnNeedDataAudio(GstElement *appsrc, guint, gpointer)
{
    GstBuffer *buffer;
    GstMapInfo info;
    GstFlowReturn ret;
    static int64_t pts = 0;

    StreamStatus status = audio->Read(audio_frame);
    if (status == StreamStatus::kEndOfStream)
    {
        cout << "End of stream ffmpeg" << endl;
        g_signal_emit_by_name (appsrc, "end-of-stream", &ret);
        return GST_FLOW_OK;
    }

    AVSampleFormat format = static_cast<AVSampleFormat>(audio_frame->format);
    size_t src_sample_size = av_get_bytes_per_sample(format);
    size_t out_sample_size = sizeof(int16_t);
    size_t size = out_sample_size * audio_frame->nb_samples * audio_frame->channels;

    buffer = gst_buffer_new_allocate(NULL, size, NULL);
    gst_buffer_map(buffer, &info, GST_MAP_WRITE);

    int ptr = 0;
    for (int i = 0; i < audio_frame->nb_samples; i++)
        for (int j = 0; j < audio_frame->channels; j++)
        {
            float out_float = *reinterpret_cast<float*>(audio_frame->data[j] + src_sample_size*i) * 32767.0;
            int16_t out = out_float;
            memcpy(info.data + ptr, &out, out_sample_size);
            ptr += out_sample_size;
        }

    GST_BUFFER_DURATION(buffer) = audio_frame->pkt_duration  * GST_SECOND * audio->GetTimeBase();
    GST_BUFFER_PTS(buffer) = pts  * GST_SECOND * audio->GetTimeBase();

    cout << "OnNeedDataAudio pts " << pts  * audio->GetTimeBase() << endl;
    pts += audio_frame->pkt_duration;

    g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);

    return GST_FLOW_OK;
}

/*
gst-launch-1.0 webmmux name=mux ! filesink location=test.webm            \
  videotestsrc num-buffers=250 ! video/x-raw,framerate=25/1 ! videoconvert ! vp8enc ! queue ! mux.video_0 \
  audiotestsrc samplesperbuffer=44100 num-buffers=10 ! audio/x-raw,rate=44100 ! vorbisenc ! queue ! mux.audio_0
*/
//deadline=1 и cpu-used=5
int main (int argc, char *argv[])
{
    audio = make_shared<StreamReader>("/home/oleg/projects/mseplayer/build-cpp-client-Desktop_Qt_5_14_1_GCC_64bit-Debug/st.mp4",
                       AVMediaType::AVMEDIA_TYPE_AUDIO);

    video = make_shared<StreamReader>("/home/oleg/projects/mseplayer/build-cpp-client-Desktop_Qt_5_14_1_GCC_64bit-Debug/st.mp4",
                       AVMediaType::AVMEDIA_TYPE_VIDEO);

    audio_frame = av_frame_alloc();
    video_frame = av_frame_alloc();

    GstElement *pipeline;
    GstBus *bus;
    GstMessage *msg;
    gboolean terminate = FALSE;
    GstCaps *video_caps, *audio_caps;
    file = fopen("test1.webm", "wb");

    /* Initialize GStreamer */
    gst_init (&argc, &argv);

    /* Create gstreamer elements */
    //GstElement *video_source = gst_element_factory_make ("videotestsrc", NULL);
    GstElement *video_source = gst_element_factory_make("appsrc", NULL);
    GstElement *video_encoder = gst_element_factory_make ("vp8enc", NULL);
    GstElement *video_queue = gst_element_factory_make ("queue", NULL);
    GstElement *muxer = gst_element_factory_make ("webmmux", NULL);
    GstElement *app_sink = gst_element_factory_make ("appsink", NULL);

    //GstElement *audio_source = gst_element_factory_make ("audiotestsrc", NULL);
    GstElement *audio_source = gst_element_factory_make("appsrc", NULL);
    GstElement *audio_encoder = gst_element_factory_make ("opusenc", NULL);
    GstElement *audio_queue = gst_element_factory_make ("queue", NULL);

    /* Create the empty pipeline */
    pipeline = gst_pipeline_new ("webmux");

    if (!pipeline || !video_source || !video_encoder || !video_queue || !muxer || !app_sink ||
        !audio_source || !audio_encoder || !audio_queue)
    {
        g_printerr ("One element could not be created. Exiting.\n");
        return -1;
    }

    /* we add all elements into the pipeline */
    gst_bin_add_many (GST_BIN (pipeline), video_source, video_encoder, video_queue, muxer, app_sink,
                      audio_source, audio_encoder, audio_queue, NULL);
    video_caps = gst_caps_new_simple("video/x-raw",
        "width", G_TYPE_INT, 1280,
        "height", G_TYPE_INT, 720,
        "format", G_TYPE_STRING, "I420",
        "framerate", GST_TYPE_FRACTION, 25, 1, NULL);

    audio_caps = gst_caps_new_simple ("audio/x-raw", "format", G_TYPE_STRING, "S16LE",
        "layout", G_TYPE_STRING, "interleaved", "rate", G_TYPE_INT, 48000,
        "channels", G_TYPE_INT, 2,
        "channel-mask", GST_TYPE_BITMASK, 3, NULL);

    g_object_set (audio_source, "caps", audio_caps, NULL);

    gst_element_link_filtered (video_source, video_encoder, video_caps);
    gst_element_link_many (video_source, video_encoder, NULL);
    gst_element_link_many (video_encoder, video_queue, muxer, app_sink, NULL);

    gst_element_link_many (audio_source, audio_encoder, audio_caps, NULL);//gst_element_link_filtered (audio_source, audio_encoder, audio_caps);
    gst_element_link_many (audio_encoder, audio_queue, muxer, NULL);

    g_signal_connect (app_sink, "new-sample", G_CALLBACK (on_new_sample), NULL);
    g_signal_connect(video_source, "need-data", G_CALLBACK(OnNeedDataVideo), NULL);
    g_signal_connect(audio_source, "need-data", G_CALLBACK(OnNeedDataAudio), NULL);

    /* Set Params */
    //g_object_set (video_source, "num-buffers", 250, NULL);
    //g_object_set (audio_source, "samplesperbuffer", 44100, NULL);
    //g_object_set (audio_source, "num-buffers", 10, NULL);
    g_object_set (video_source, "is-live", TRUE, NULL);
    g_object_set (video_encoder, "deadline", 1, NULL);
    g_object_set (video_encoder, "cpu-used", 5, NULL);
    g_object_set (audio_source, "is-live", TRUE, NULL);
    //g_object_set (muxer, "streamable", TRUE, NULL);

    g_object_set (app_sink, "emit-signals", TRUE, NULL);
    g_object_set (G_OBJECT(video_source), "stream-type", 0, "format", GST_FORMAT_TIME, NULL);
    g_object_set (G_OBJECT(audio_source), "stream-type", 0, "format", GST_FORMAT_TIME, NULL);

    /* Start playing */
    gst_element_set_state (pipeline, GST_STATE_PLAYING);


    /* Listen to the bus */
    bus = gst_element_get_bus (pipeline);
    do {
      msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
          static_cast<GstMessageType>(GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

      /* Parse message */
      if (msg != NULL) {
        GError *err;
        gchar *debug_info;

        switch (GST_MESSAGE_TYPE (msg)) {
          case GST_MESSAGE_ERROR:
            gst_message_parse_error (msg, &err, &debug_info);
            g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
            g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
            g_clear_error (&err);
            g_free (debug_info);
            terminate = TRUE;
            break;
          case GST_MESSAGE_EOS:
            g_print ("End-Of-Stream reached.\n");
            terminate = TRUE;
            break;
          case GST_MESSAGE_STATE_CHANGED:
            /* We are only interested in state-changed messages from the pipeline */
            if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipeline)) {
              GstState old_state, new_state, pending_state;
              gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
              g_print ("Pipeline state changed from %s to %s:\n",
                  gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
            }
            break;
          default:
            /* We should not reach here */
            g_printerr ("Unexpected message received.\n");
            break;
        }
        gst_message_unref (msg);
      }
    } while (!terminate);

    /* Free resources */
    gst_object_unref (bus);
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
    fclose(file);
    av_frame_free(&audio_frame);
    av_frame_free(&video_frame);
}

