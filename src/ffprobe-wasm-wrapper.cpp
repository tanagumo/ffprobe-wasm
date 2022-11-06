#include <vector>
#include <string>
#include <vector>
#include <inttypes.h>
#include <emscripten.h>
#include <emscripten/bind.h>

using namespace emscripten;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/bprint.h>
#include <libavutil/imgutils.h>
};

const std::string c_avformat_version() {
    return AV_STRINGIFY(LIBAVFORMAT_VERSION);
}

const std::string c_avcodec_version() {
    return AV_STRINGIFY(LIBAVCODEC_VERSION);
}

const std::string c_avutil_version() {
    return AV_STRINGIFY(LIBAVUTIL_VERSION);
}

typedef struct KeyFramesResponse {
  std::vector<double> start_secs;
} KeyFramesResponse;

KeyFramesResponse get_key_frames(std::string filename) {
    av_log_set_level(AV_LOG_QUIET); // No logging output for libav.

    FILE *file = fopen(filename.c_str(), "rb");
    if (!file) {
      printf("cannot open file\n");
    }
    fclose(file);

    AVFormatContext *pFormatContext = avformat_alloc_context();
    if (!pFormatContext) {
      printf("ERROR: could not allocate memory for Format Context\n");
    }

    // Open the file and read header.
    int ret;
    if ((ret = avformat_open_input(&pFormatContext, filename.c_str(), NULL, NULL)) < 0) {
        printf("ERROR: %s\n", av_err2str(ret));
    }

    // Get stream info from format.
    if (avformat_find_stream_info(pFormatContext, NULL) < 0) {
      printf("ERROR: could not get stream info\n");
    }

    // Get streams data.
    AVCodec  *pCodec = NULL;
    AVCodecParameters *pCodecParameters = NULL;
    int video_stream_index = -1;
    int nb_frames = 0;

    // Loop through the streams.
    for (int i = 0; i < pFormatContext->nb_streams; i++) {
      AVCodecParameters *pLocalCodecParameters = NULL;
      pLocalCodecParameters = pFormatContext->streams[i]->codecpar;

      // Print out the decoded frame info.
      AVCodec *pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);
      if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (video_stream_index == -1) {
          video_stream_index = i;
          nb_frames = pFormatContext->streams[i]->nb_frames;

          // Calculate the nb_frames for MKV/WebM if nb_frames is 0.
          if (nb_frames == 0) {
            nb_frames = (pFormatContext->duration / 1000000) * pFormatContext->streams[i]->avg_frame_rate.num;
          }
          pCodec = pLocalCodec;
          pCodecParameters = pLocalCodecParameters;
        }
      }
    }

    AVRational stream_time_base = pFormatContext->streams[video_stream_index]->time_base;
    AVRational avg_frame_rate = pFormatContext->streams[video_stream_index]->avg_frame_rate;
    // printf("stream_time_base: %d / %d = %.5f\n", stream_time_base.num, stream_time_base.den, av_q2d(stream_time_base));

    const double timeBase = av_q2d(stream_time_base);
    KeyFramesResponse r;

    AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pCodecContext, pCodecParameters);
    avcodec_open2(pCodecContext, pCodec, NULL);

    AVPacket *pPacket = av_packet_alloc();
    AVFrame *pFrame = av_frame_alloc();

    int max_packets_to_process = 1000;

    av_seek_frame(pFormatContext, video_stream_index, 0, AVSEEK_FLAG_ANY);

    // Read video frames.
    while (av_read_frame(pFormatContext, pPacket) >= 0) {
      if (pPacket->stream_index == video_stream_index) {
          int response = 0;
          response = avcodec_send_packet(pCodecContext, pPacket);

          if (response >= 0) {
            response = avcodec_receive_frame(pCodecContext, pFrame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
              continue;
            }

            // Track keyframes so we paginate by each GOP.
            if (pFrame->key_frame == 1) {
                r.start_secs.push_back((int) pPacket->pts * timeBase);
            }

            if (--max_packets_to_process <= 0) break;
          }
      }
      av_packet_unref(pPacket);
    }

    avformat_close_input(&pFormatContext);
    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecContext);

    return r;
}

EMSCRIPTEN_BINDINGS(constants) {
    function("avformat_version", &c_avformat_version);
    function("avcodec_version", &c_avcodec_version);
    function("avutil_version", &c_avutil_version);
}

EMSCRIPTEN_BINDINGS(structs) {
  emscripten::value_object<KeyFramesResponse>("KeyFramesResponse")
  .field("start_secs", &KeyFramesResponse::start_secs)
  ;
  register_vector<double>("vector<double>");
  function("get_key_frames", &get_key_frames);
}
