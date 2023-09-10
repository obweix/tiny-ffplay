//
//  main.cpp
//  video_player
//
//  Created by wbxie on 2023/7/9.
//
#include <iostream>
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
#include "SDL2/SDL.h"
}
#endif


int main() {
    AVFrame *pFrameYUV;
    AVFrame* frame = av_frame_alloc();
    unsigned char *out_buffer;
    pFrameYUV=av_frame_alloc();

    
    // Initialize FFmpeg and open the video file
//    av_register_all();
    avformat_network_init();
    std::string path = "/Users/wbxie/Movies/968445306-1-208.mp4";
    

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
    if (avcodec_parameters_to_context(codecCtx, codecParams) < 0) {
        printf("Error: Could not copy codec parameters to context\n");
        return -1;
    }

    if (avcodec_open2(codecCtx, codec, NULL) < 0) {
        printf("Error: Could not open codec\n");
        return -1;
    }
    
    out_buffer=(unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,  codecCtx->width, codecCtx->height,1));
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize,out_buffer,
        AV_PIX_FMT_YUV420P,codecCtx->width, codecCtx->height,1);

    
    
    // Calculate the time per frame
    const double frame_duration = 1.0 / av_q2d(formatCtx->streams[videoStream]->avg_frame_rate);

    // Create a timestamp variable to control frame rendering
    double timestamp = 0.0;

    // Variables for FPS calculation
    double fps_counter = 0.0;
    int frame_count = 0;
    
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
        
        // Calculate the time elapsed since the last frame
        double current_time = SDL_GetTicks() / 1000.0;
        double elapsed_time = current_time - timestamp;
        
        // If enough time has passed, render the next frame
        if (elapsed_time >= frame_duration) {
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
                    
                    av_frame_unref(frame);
                }
               
            }
            av_packet_unref(&packet);
            
            // Update the timestamp
            timestamp = current_time;
            
            // Calculate FPS
           fps_counter += elapsed_time;
           frame_count++;

           // Log FPS every second
           if (fps_counter >= 1.0) {
               printf("FPS: %.d\n", frame_count);
               fps_counter = 0;
               frame_count = 0;
           }else{
//               printf("%ld\n",current_time);
           }// Variables for FPS calculation
  
            
            // Delay to match the original video's frame rate
//            SDL_Delay((Uint32)(frame_duration * 1000.0));
        }
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
