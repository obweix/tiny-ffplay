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
#include "libswresample/swresample.h"
#include "libavformat/avformat.h"
#include "SDL2/SDL.h"
}
#endif

#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio
/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1

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

void set_clock_at(Clock& c,double pts,int serial,double time){
    c.pts = pts;
    c.last_updated = time;
    c.pts_drift = c.pts - time;
    c.serial = serial;
}

double get_clock(Clock& c){
    double time = av_gettime_relative() / 1000000.0;
    return c.pts_drift + time;
}

double get_master_clock(VideoState& is){
    return get_clock(is.audclk);
}


void print_time_base(AVFrame* frame){

    // Calculate the presentation time of the frame in seconds
    double presentation_time = frame->pts * av_q2d(frame->time_base);

    printf("Frame Time Base: %d/%d,Frame PTS: %lld,presentation time: %f seconds\n", frame->time_base.num, frame->time_base.den,frame->pts, presentation_time);
}


int decode_audio(uint8_t *audio_buf,VideoState* is,double* last_frame_pts){
    AVFrame* frame = av_frame_alloc();
    AVCodecContext* codecCtx = is->audio_decoder.avctx;
    AVPacket packet = is->audioq.tryPop();
    SwrContext* swrCtx = nullptr;
    int ret = -1;
    if(packet.size == 0){
        printf("audio queue is empty.\n");
        return ret;
    }
    
    int response = avcodec_send_packet(codecCtx, &packet);
    if(response < 0){
        fprintf(stderr, "1.Error decoding audio frame\n");
        return ret;
    }
    AVFrame* wanted_frame = is->wanted_frame;

    
    while(response >= 0){
        if(packet.pts == AV_NOPTS_VALUE){
            break;
        }
        
        AVRational avRational = {1,1000};
       int64_t progress =  av_rescale_q(packet.pts,is->ic->streams[is->audio_stream]->time_base,avRational);
//        printf("progress:%lld\n",progress);
//        printf("frame sample rate:%d,wanted sample rate:%d\n",frame->sample_rate,wanted_frame->sample_rate);
//        
        response = avcodec_receive_frame(codecCtx, frame);
  
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
            break;
        else if (response < 0) {
            fprintf(stderr, "2.Error decoding audio frame\n");
            break;
        }
    
        AVRational tb = {1,48000};
        frame->time_base = tb;
        *last_frame_pts = frame->pts * av_q2d(tb);

//        print_time_base(frame);
        
        if(frame->channels > 0 && frame->channel_layout == 0){
            frame->channel_layout = av_get_default_channel_layout(frame->channels);
            
        }else if(frame->channels == 0 && frame->channel_layout > 0){
            frame->channels = av_get_channel_layout_nb_channels(frame->channel_layout);
        }
        
        swrCtx = swr_alloc_set_opts(nullptr,wanted_frame->channel_layout,(AVSampleFormat)wanted_frame->format,wanted_frame->sample_rate,frame->channel_layout,(AVSampleFormat)frame->format,frame->sample_rate,0,nullptr);
        
        int errorNum = swr_init(swrCtx);
        if(0 != errorNum)
        {
            char errorBuff[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(errorNum, errorBuff, sizeof(errorBuff) - 1);
            break;
        }
        
        int64_t dst_nb_samples = av_rescale_rnd(
                                            swr_get_delay(
                                                          swrCtx,frame->sample_rate) + frame->nb_samples,
                                            frame->sample_rate,
                                            frame->sample_rate,
                                            AVRounding(1)
                                            );
        int len2 = swr_convert(
                               swrCtx,
                               &audio_buf,
                               dst_nb_samples,
                               (const uint8_t**)frame->data,
                               frame->nb_samples
                               );
        if(len2 < 0) break;
        
        ret = wanted_frame->channels * len2 * av_get_bytes_per_sample((AVSampleFormat)wanted_frame->format);
        
    }
    
    swr_free(&swrCtx);
    av_frame_free(&frame);
    av_packet_unref(&packet);
    
    return ret;
}

/* prepare a new audio buffer */
void sdl_audio_callback(void *opaque, Uint8 *stream, int len){
    uint8_t audio_buf[MAX_AUDIO_FRAME_SIZE];
    memset(stream,0,len);
    
    VideoState* is =(VideoState*) opaque;
    double last_frame_pts = -1;
    while(len > 0){
        if(is->audio_buf_pos >= is->audio_buf_size){
            // decode audio.
            is->audio_buf_size = decode_audio(audio_buf,is,&last_frame_pts);
            if(is->audio_buf_size < 0){
                return;
            }
            is->audio_buf_pos = 0;
        }
        
        int audio_len = is->audio_buf_size - is->audio_buf_pos;
        if(audio_len > len){
            audio_len = len;
        }
        
        SDL_MixAudio(stream,audio_buf + is->audio_buf_pos,audio_len,SDL_MIX_MAXVOLUME);
        len -= audio_len;
        is->audio_buf_pos += audio_len;
        stream += audio_len;
    }
    
    double audio_callback_time = av_gettime_relative();
    if(last_frame_pts > 0){
        set_clock_at(is->audclk, last_frame_pts,0, audio_callback_time/1000000.0);
        printf("set audio clock:%f,pts:%f\n",audio_callback_time/1000000.0,last_frame_pts);
    }
}

int audio_open(VideoState& is){
    
    is.out_channel_layout = AV_CH_LAYOUT_STEREO; // 立体声
    is.out_nb_samples = is.audio_decoder.avctx->frame_size;
    is.out_sample_fmt  = AV_SAMPLE_FMT_S16;
    is.out_sample_rate = 48000;
    is.out_channels = av_get_channel_layout_nb_channels(is.out_channel_layout);
    is.out_buffer_size = av_samples_get_buffer_size(nullptr,is.out_channels,is.out_nb_samples,is.out_sample_fmt,1) ;
    is.out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE*2);
    
    is.wanted_spec.freq = is.out_sample_rate;
    is.wanted_spec.format = AUDIO_S16SYS;
    is.wanted_spec.channels = is.out_channels;
    is.wanted_spec.silence = 0;
    is.wanted_spec.samples = is.out_nb_samples;
    is.wanted_spec.size = is.out_buffer_size;
    is.wanted_spec.callback = sdl_audio_callback;
    is.wanted_spec.userdata = &is;
    
    is.wanted_frame = av_frame_alloc();
    is.wanted_frame->channel_layout = is.out_channel_layout;
    is.wanted_frame->format = is.out_sample_fmt;
    is.wanted_frame->sample_rate = is.out_sample_rate;
    is.wanted_frame->channels = is.out_channels;
    
    
    if(SDL_OpenAudio(&is.wanted_spec,nullptr)<0){
        printf("sdl open audio error.\n");
    }else{
        printf("sdl open audio sucessfully.\n");
        // Start audio playback
        SDL_PauseAudio(0);
    }
    
    return 0;
}
double compute_target_delay(double delay, VideoState &is){
    double sync_threshold, diff = 0;
    printf("last frame duration:%f\n",delay);
    
    double a,b;
    a =get_clock(is.vidclk);
    b =get_master_clock(is);
    diff = a -b;
    
    sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
    
    if (!isnan(diff) && fabs(diff) < 10) {
        if (diff <= -sync_threshold) {
            delay = FFMAX(0, delay + diff);
            printf("sync:视频播放进度慢于主时钟\n");
        }// 视频播放进度慢于主时钟
           
        else if (diff >= sync_threshold){
            delay = delay + diff;
            printf("sync:视频播放进度快于主时钟\n");
        }  // 视频播放进度快于主时钟
           
        else if (diff >= sync_threshold)
            delay = 2 * delay;
    }
    printf("sync:compute_target_delay:%f,diff:%f,vc:%f,ac:%f,sync_threshold:%f\n",delay,diff,a,b,sync_threshold);
    return delay;
}

void video_refresh(VideoState& is,SDL_Texture* texture,double last_frame_duration){
    double time = av_gettime_relative() / 1000000.0;
    double remaining_time = 0.01; //second
    double delay = compute_target_delay(last_frame_duration, is);
    
    if(is.last_video_refresh_time <= 0){
        is.last_video_refresh_time = av_gettime_relative() / 1000000.0;
    }
    
    if(time < is.last_video_refresh_time + delay){
//        remaining_time = FFMIN(is.last_video_refresh_time + delay - time,remaining_time);
        remaining_time = delay;
        
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        printf("remaining_time:%f\n",remaining_time);
        if (remaining_time > 0.0){
            av_usleep((int64_t)(remaining_time * 1000000.0));
        }
    }
    is.last_video_refresh_time = av_gettime_relative() / 1000000.0;
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
    
    double remaining_time = 0.0;
    remaining_time = REFRESH_RATE;
    double last_frame_pts = -1;
    double last_frame_duration = -1;
    double delay = -1;
    
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
            response = avcodec_receive_frame(is.video_decoder.avctx, frame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
                break;
            else if (response < 0) {
                printf("Error while receiving a frame from the decoder\n");
                break;
            }
            
            frame->time_base = is.video_st->time_base;
            if(last_frame_pts < 0){
                last_frame_duration = 0;
                last_frame_pts = frame->pts;
                //解码出第一帧时，初始化视频时钟
                double time =av_gettime_relative()/1000000.0;
                set_clock_at(is.vidclk, frame->pts * av_q2d(frame->time_base),0, time);
                printf("init: set video clock:%f,pts:%lld\n",time,frame->pts * av_q2d(frame->time_base));
            }else{
                last_frame_duration =(frame->pts - last_frame_pts) * av_q2d(frame->time_base);
                last_frame_pts = frame->pts;
            }
        
           
            
            av_packet_unref(&pkg);
            
            sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height, pFrameYUV->data, pFrameYUV->linesize);
            
            SDL_UpdateTexture(texture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
            video_refresh(is,texture,last_frame_duration);
            
            double refresh_time = av_gettime_relative();
            set_clock_at(is.vidclk, frame->pts * av_q2d(frame->time_base),0, refresh_time/1000000.0);
            printf("refresh:set video clock:%f,pts:%f,last_frame_duration:%f\n",refresh_time/1000000.0,frame->pts * av_q2d(frame->time_base),last_frame_duration);
//            print_time_base(frame);
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
    printf("pix_fmt:%d,\n",avctx->pix_fmt,codecParams);
    if (ret < 0){
        return -1;
    }
    avctx->pkt_timebase = ic->streams[stream_index]->time_base;
    printf("avctx->pkt_timebase =%d/%d\n",avctx->pkt_timebase.num,avctx->pkt_timebase.den);
    
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
            printf("audio thread created.\n");
            audio_open(is);
            
            break;
        }
        case AVMEDIA_TYPE_VIDEO:{
            is.video_decoder = Decoder(avctx);
            std::thread videot = std::thread(video_thread,std::ref(is));
            videot.detach();
            printf("video thread created.\n");
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
            is.video_st = ic->streams[i];
            if(0 != ret){
                printf("decode video fail.\n");
            }
            break;
        }
    }
    
    for (int i = 0; i < ic->nb_streams; i++) {
        if(ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            is.audio_stream = i;
            is.audio_st = ic->streams[i];
            stream_component_open(is, is.audio_stream);
            printf("find audio stream: %d\n",i);
            break;
        }
    }
    
    while(true){
//        if(is.videoq.size() + is.audioq.size()> 500){
//            av_usleep(10 * 1000);
//            printf("sleep 10ms. \n");
//        }
        
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
        if (event->type == SDL_QUIT) {
            break;
        }
        if (remaining_time > 0.0){
            //            av_usleep((int64_t)(remaining_time * 1000000.0));
            
            //            av_log(NULL, AV_LOG_INFO, "av_usleep() called,%lf\n",remaining_time);
        }
        
        remaining_time = REFRESH_RATE;
        
//        video_refresh(is, &remaining_time);
        
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
    }
}



int main(int argc,char **argv) {
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
    
    return 0;
}

