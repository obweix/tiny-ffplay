//
//  main.cpp
//  video_player
//
//  Created by wbxie on 2023/7/9.
//
#include <iostream>
#include <thread>
//#define __STDC_CONSTANT_MACROS



#ifdef __ANDROID__
extern "C" {
#include "3rdparty/ffmpeg/include/libavformat/avformat.h"
#include "3rdparty/ffmpeg/include/libswscale/swscale.h"
#include "3rdparty/ffmpeg/include/libswresample/swresample.h"
#include "3rdparty/ffmpeg/include/libavutil/pixdesc.h"
}
#elif defined(__APPLE__) // iOS或OS X
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "libavutil/time.h"
#include "SDL2/SDL.h"
}
#endif

static int default_width  = 1920;
static int default_height = 1080;

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_RendererInfo renderer_info = {0};
static SDL_AudioDeviceID audio_dev;
static int64_t duration = AV_NOPTS_VALUE;
static int64_t start_time = AV_NOPTS_VALUE;




#include "VideoState.hpp"

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

AVPacket copy_packet(AVPacket* pkt){
    AVPacket *pkt1;
    pkt1 = av_packet_alloc();
    if (!pkt1) {
        av_packet_unref(pkt);
        return *pkt;
    }
    
    av_packet_move_ref(pkt1, pkt);
    
    return *pkt1;
    
}




void video_thread(VideoState &is){
    AVFrame* frame = av_frame_alloc();
    AVFrame *pFrameYUV;
    unsigned char *out_buffer;
    pFrameYUV=av_frame_alloc();
    AVCodecContext* codecCtx = is.video_decoder.avctx;
    
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, is.video_decoder.avctx->width, is.video_decoder.avctx->height); // Use SDL_PIXELFORMAT_IYUV
    
    if(!texture){
        printf("creat texture fail.\n");
    }
    
    out_buffer=(unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,  codecCtx->width, codecCtx->height,1));
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize,out_buffer,
                         AV_PIX_FMT_YUV420P,codecCtx->width, codecCtx->height,1);
    
//    av_dump_format(is.ic,0,"test",0);
    
    struct SwsContext* sws_ctx = sws_getContext(codecCtx->width, codecCtx->height,AV_PIX_FMT_YUV420P , codecCtx->width, codecCtx->height, codecCtx->pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
    
    for(;;){

        if(is.video_stream == -1){
            continue;
        }
        
        AVPacket pkg = is.videoq.waitAndPop();
        
        int response = avcodec_send_packet(is.video_decoder.avctx, &pkg);
        if (response < 0) {
            printf("Error while sending a packet to the decoder\n");
            break;
        }
        
        while (response >= 0) {
//            printf(".");
            response = avcodec_receive_frame(is.video_decoder.avctx, frame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
                break;
            else if (response < 0) {
                printf("Error while receiving a frame from the decoder\n");
                break;
            }
            
//            //////////////////////////////////////////////////////////////
//            av_packet_unref(&pkg);
            
            sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height, pFrameYUV->data, pFrameYUV->linesize);
            
            SDL_UpdateTexture(texture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
            SDL_Delay(40);
        }
        
    }
}

void audio_thread(VideoState &is){
    
}

int stream_component_open(VideoState& is,int stream_index){
    AVFormatContext* ic = is.ic;
    AVCodecContext *avctx;
    const AVCodec *codec;
   
    const char *forced_codec_name = NULL;
    int ret = 0;
    
    if(stream_index < 0){
        return -1;
    }
    AVCodecParameters* codecParams = ic->streams[stream_index]->codecpar;
  
//    if (avformat_find_stream_info(ic, NULL) < 0) {
//        printf("Error: Could not find stream information\n");
//    }
//    
    codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        printf("Error: Codec not found\n");
        return -1;
    }
    
    
    avctx = avcodec_alloc_context3(codec);
    if (!avctx)
        return AVERROR(ENOMEM);
    
  
    
    ret = avcodec_parameters_to_context(avctx, codecParams);
    printf("pic format:%d,\n",avctx->pix_fmt,codecParams);
    if (ret < 0){
        return -1;
    }
    avctx->pkt_timebase = ic->streams[stream_index]->time_base;

    
//    printf("---------------- File Information ---------------\n");
//    av_dump_format(ic,0,"test",0);
//    printf("-------------------------------------------------\n");
    
//    if (!codec) {
//        if (forced_codec_name) av_log(NULL, AV_LOG_WARNING,
//                                      "No codec could be found with name '%s'\n", forced_codec_name);
//        else                   av_log(NULL, AV_LOG_WARNING,
//                                      "No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
//        ret = AVERROR(EINVAL);
//        avcodec_free_context(&avctx);
//        return ret;
//    }
    
    avctx->codec_id = codec->id;
    
    
    if (avcodec_open2(avctx, codec, NULL) < 0) {
        printf("Error: Could not open codec\n");
        return -1;
    }
    
    switch (avctx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:{
            is.audio_decoder = Decoder(avctx);
            std::thread audiot = std::thread(audio_thread,std::ref(is));
            audiot.detach();
            break;
        }
        case AVMEDIA_TYPE_VIDEO:{
            is.video_decoder = Decoder(avctx);
            std::thread videot = std::thread(video_thread,std::ref(is));
            videot.detach();
            break;
        }
        default:
            printf("find decoder error.\n");
            
    }
    
    
    return 0;
}


void read_thread(VideoState &is){
    AVPacket *pkt = NULL;
    AVFormatContext *ic = NULL;
    int err,ret;
    int64_t stream_start_time;
    int64_t pkt_ts;
    int pkt_in_play_range = 0;
    
    
    pkt = av_packet_alloc();
    if (!pkt) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate packet.\n");
    }
    
    ic = avformat_alloc_context();
 
    if (!ic) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
    }
    
    err = avformat_open_input(&ic, is.filename.c_str(), NULL, NULL);
    is.ic = ic;
    
    if (avformat_find_stream_info(ic, NULL) < 0) {
        printf("Error: Could not find stream information\n");
    }
    
    if (err < 0) {
        av_log(NULL, AV_LOG_FATAL, "Could not open input.\n");
        ret = -1;
    }
    
    printf("---------------- File Information ---------------\n");
    av_dump_format(ic,0,is.filename.c_str(),0);
    printf("-------------------------------------------------\n");
    
    for (int i = 0; i < ic->nb_streams; i++) {
        if (ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            is.video_stream = i;
            printf("find video stream: %d\n",i);
            int ret =  stream_component_open(is, is.video_stream);
            if(0 != ret){
                printf("decode video fail.\n");
            }
            break;
        }
    }
    
    for (int i = 0; i < ic->nb_streams; i++) {
        if(ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            is.audio_stream = i;
//            stream_component_open(is, is.audio_stream);
            printf("find audio stream: %d\n",i);
            break;
        }
    }
    
    while(true){
                if(is.videoq.size() > 100){
                    av_usleep(10 * 1000);
//                    printf("sleep 10ms. \n");
                }
        
        ret = av_read_frame(ic, pkt);
        
        if(ret < 0){
            if(ret == AVERROR_EOF){
//                printf("do nothing when read end.\n");
            }
            
            if (ic->pb && ic->pb->error) {
                printf("read frame fail.\n");
            }
        }else{
            /* check if packet is in play range specified by user, then queue, otherwise discard */
            stream_start_time = ic->streams[pkt->stream_index]->start_time;
            pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
            pkt_in_play_range = duration == AV_NOPTS_VALUE ||
            (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
            av_q2d(ic->streams[pkt->stream_index]->time_base) -
            (double)(start_time != AV_NOPTS_VALUE ? start_time : 0) / 1000000
            <= ((double)duration / 1000000);
            
            pkt_in_play_range = true;
            
            if(pkt->stream_index == is.video_stream && pkt_in_play_range){
                is.videoq.push(copy_packet(pkt));
            }else if(pkt->stream_index == is.audio_stream && pkt_in_play_range){
                is.audioq.push(copy_packet(pkt));
            }else {
                av_packet_unref(pkt);
            }
            
        }
        
    }
}

VideoState stream_open(const std::string filename){
    VideoState is;
    is.last_audio_stream = -1;
    is.last_video_stream = -1;
    is.last_subtitle_stream = -1;
    is.filename = filename;
    
    std::thread t(read_thread,std::ref(is));
    t.detach();
    
    return is;
}

void video_refresh(const VideoState &opaque, double *remaining_time){
    
}

void refresh_loop_wait_event(const VideoState& is, SDL_Event *event){
    double remaining_time = 0.0;
    SDL_PumpEvents();
    while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
        
        if (remaining_time > 0.0){
            //            av_usleep((int64_t)(remaining_time * 1000000.0));
            
            //            av_log(NULL, AV_LOG_INFO, "av_usleep() called,%lf\n",remaining_time);
        }
        
        remaining_time = REFRESH_RATE;
        
        video_refresh(is, &remaining_time);
        
        if(event->type == SDL_QUIT){
            break;
        }
        
        SDL_PumpEvents();
    }
}

void event_loop(VideoState& is){
    SDL_Event event;
    
    for(;;){
        refresh_loop_wait_event(is, &event);
//        AVPacket packet = cur_stream.videoq.waitAndPop();
    }
}


int main(int argc,char **argv) {
//    // Initialize SDL
//       SDL_Init(SDL_INIT_VIDEO);
//
//       // Create a window
//       SDL_Window* windowtest = SDL_CreateWindow("My Window",
//                                             SDL_WINDOWPOS_UNDEFINED,
//                                             SDL_WINDOWPOS_UNDEFINED,
//                                             800, 600, // width, height
//                                             SDL_WINDOW_SHOWN);
//       if (windowtest == NULL) {
//           // Window creation failed
//           printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
//           return 1;
//       }
//
//       // Main loop
//       bool quit = false;
//       SDL_Event event1;
//       while (!quit) {
//           // Event handling
//           while (SDL_PollEvent(&event1) != 0) {
//               if (event1.type == SDL_QUIT) {
//                   quit = true;
//               }
//           }
//
//           // Rendering (not shown in this example)
//       }

    
    
#if defined(__cplusplus)
    if (__cplusplus == 202203L) std::cout << "C++23" << std::endl;
    else if (__cplusplus == 202002L) std::cout << "C++20" << std::endl;
    else if (__cplusplus == 201703L) std::cout << "C++17" << std::endl;
    else if (__cplusplus == 201402L) std::cout << "C++14" << std::endl;
    else if (__cplusplus == 201103L) std::cout << "C++11" << std::endl;
    else std::cout << "Pre-standard C++" << std::endl;
#else
    std::cout << "Pre-standard C++" << std::endl;
#endif
    
    
    av_log_set_flags(AV_LOG_SKIP_REPEATED);
    
    /* register all codecs, demux and protocols */
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
    avformat_network_init();
    
    
    std::string input_flilename = std::string(argv[1]);
    
    SDL_Init(SDL_INIT_VIDEO);
    
    printf("input_filename: %s\n",input_flilename.c_str());
    
    // create sdl window
    int flags =  SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
    window = SDL_CreateWindow("video_player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, default_width, default_height, flags);
    if (window) {
        printf("sdl window created.\n");
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) {
            av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
            renderer = SDL_CreateRenderer(window, -1, 0);
        }
        if (renderer) {
            if (!SDL_GetRendererInfo(renderer, &renderer_info))
                av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", renderer_info.name);
        }
    }else{
        av_log(NULL, AV_LOG_FATAL, "Failed to create window or renderer: %s", SDL_GetError());
    }
    
    VideoState is = stream_open(input_flilename);
    
    
    event_loop(is);
    
    // -----------------------------------------------------------------
    AVFrame *pFrameYUV;
    AVFrame* frame = av_frame_alloc();
    unsigned char *out_buffer;
    pFrameYUV=av_frame_alloc();
    
    
    // Initialize FFmpeg and open the video file
    //    av_register_all();
    avformat_network_init();
    std::string path = input_flilename;
    
    
    AVFormatContext* formatCtx = avformat_alloc_context();
    if (avformat_open_input(&formatCtx, path.c_str(), NULL, NULL) != 0) {
        printf("Error: Could not open video file\n");
        return -1;
    }
    
    if (avformat_find_stream_info(formatCtx, NULL) < 0) {
        printf("Error: Could not find stream information\n");
        return -1;
    }
    
    int videoStream = -1;
    for (int i = 0; i < formatCtx->nb_streams; i++) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }
    }
    
    if (videoStream == -1) {
        printf("Error: Could not find a video stream in the input file\n");
        return -1;
    }
    
    AVCodecParameters* codecParams = formatCtx->streams[videoStream]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        printf("Error: Codec not found\n");
        return -1;
    }
    
   
    
    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    printf("1pic format:%d\n",codecCtx->pix_fmt);
    if (avcodec_parameters_to_context(codecCtx, codecParams) < 0) {
        printf("Error: Could not copy codec parameters to context\n");
        return -1;
    }
    
    printf("2pic format:%d\n",codecCtx->pix_fmt);

    
    if (avcodec_open2(codecCtx, codec, NULL) < 0) {
        printf("Error: Could not open codec\n");
        return -1;
    }
    
    out_buffer=(unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,  codecCtx->width, codecCtx->height,1));
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize,out_buffer,
                         AV_PIX_FMT_YUV420P,codecCtx->width, codecCtx->height,1);
    
    
    printf("---------------- File Information ---------------\n");
    av_dump_format(formatCtx,0,path.c_str(),0);
    printf("-------------------------------------------------\n");
    
    
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
    SDL_Window* window = SDL_CreateWindow("Simple Video Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, codecCtx->width, codecCtx->height, SDL_WINDOW_OPENGL);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, codecCtx->width, codecCtx->height); // Use SDL_PIXELFORMAT_IYUV
    
    // Initialize the SwsContext for frame scaling
    struct SwsContext* sws_ctx = sws_getContext(codecCtx->width, codecCtx->height, codecCtx->pix_fmt, codecCtx->width, codecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
    
    // Create an SDL2 event loop for rendering frames
    SDL_Event event;
    while (1) {
        SDL_PollEvent(&event);
        if (event.type == SDL_QUIT)
            break;
        
        AVPacket packet;
        if (av_read_frame(formatCtx, &packet) < 0)
            break;
        
        if (packet.stream_index == videoStream) {
            
            if (!frame)
                break;
            
            int response = avcodec_send_packet(codecCtx, &packet);
            if (response < 0) {
                printf("Error while sending a packet to the decoder\n");
                break;
            }
            
            while (response >= 0) {
                response = avcodec_receive_frame(codecCtx, frame);
                
      
                
                if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
                    break;
                else if (response < 0) {
                    printf("Error while receiving a frame from the decoder\n");
                    break;
                }
                
                sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height, pFrameYUV->data, pFrameYUV->linesize);
                
                SDL_UpdateTexture(texture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);
                SDL_Delay(40);
                
                av_frame_unref(frame);
            }
            
        }
        av_packet_unref(&packet);
        
        
    }
    
    av_frame_free(&pFrameYUV);
    av_frame_free(&frame);
    
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    avformat_close_input(&formatCtx);
    avcodec_free_context(&codecCtx);
    
    return 0;
}

