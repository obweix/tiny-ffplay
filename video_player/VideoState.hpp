//
//  VideoState.hpp
//  video_player
//
//  Created by wbxie on 2024/2/22.
//

#ifndef VideoState_hpp
#define VideoState_hpp

#include <stdio.h>
#include <string>
#include "Clock.hpp"
#include "AudioQueue.hpp"
#include "VideoQueue.hpp"
#include "Decoder.hpp"
#include "SDL2/SDL.h"



extern "C"{
#include "libavformat/avformat.h"
}

class VideoState{
public:
    int last_video_stream, last_audio_stream, last_subtitle_stream;
    
    Clock audclk;
    Clock vidclk;
    Clock extclk;
    
    int video_stream;
    AVStream *video_st;
    
    int audio_stream;
    AVStream *audio_st;
    
    std::string filename;
    
    AudioQueue audioq;
    VideoQueue videoq;
    
    AVFormatContext *ic;
    
    Decoder video_decoder;
    Decoder audio_decoder;
    
    
    
    //Out Audio Param
    uint64_t out_channel_layout;
    //nb_samples: AAC-1024 MP3-1152
    int out_nb_samples;
    AVSampleFormat out_sample_fmt;
    int out_sample_rate;
    int out_channels;
    //Out Buffer Size
    int out_buffer_size;
    SDL_AudioSpec wanted_spec;
    uint8_t* out_buffer;
    
    int audio_buf_pos;
    int audio_buf_size;
    
    AVFrame* wanted_frame; // audio wanted frame in the format that set to SDL.
    
    
    
public:
    VideoState() = default;
    
    VideoState& operator=(const VideoState& is){
        this->filename = is.filename;
        this->audclk = is.audclk;
        this->vidclk = is.vidclk;
        this->extclk = is.extclk;
        this->audio_stream = is.audio_stream;
        this->video_stream = is.video_stream;
        this->ic = is.ic;
        this->audio_decoder = is.audio_decoder;
        this->video_decoder = is.video_decoder;
        this->audio_st = is.audio_st;
        this->video_st = is.video_st;
        this->video_stream = is.video_stream;
//        this->audioq = is.audioq;
        
        this->out_channel_layout = is.out_channel_layout;
        this->out_nb_samples = is.out_nb_samples;
        this->out_sample_fmt = is.out_sample_fmt;
        this->out_sample_rate = is.out_sample_rate;
        this->out_channels = is.out_channels;
        this->out_buffer_size = is.out_buffer_size;
        this->wanted_spec = is.wanted_spec;
        this->out_buffer = is.out_buffer;
        
        this->audio_buf_pos = is.audio_buf_pos;
        this->audio_buf_size = is.audio_buf_size;
        
        this->wanted_frame = is.wanted_frame;
        
        return *this;
    }
    
    VideoState(const VideoState& is){
        this->filename = is.filename;
        this->audclk = is.audclk;
        this->vidclk = is.vidclk;
        this->extclk = is.extclk;
        this->audio_stream = is.audio_stream;
        this->video_stream = is.video_stream;
        this->ic = is.ic;
        this->audio_decoder = is.audio_decoder;
        this->video_decoder = is.video_decoder;
        this->audio_st = is.audio_st;
        this->video_st = is.video_st;
        this->video_stream = is.video_stream;
        
        this->out_channel_layout = is.out_channel_layout;
        this->out_nb_samples = is.out_nb_samples;
        this->out_sample_fmt = is.out_sample_fmt;
        this->out_sample_rate = is.out_sample_rate;
        this->out_channels = is.out_channels;
        this->out_buffer_size = is.out_buffer_size;
        this->wanted_spec = is.wanted_spec;
        this->out_buffer = is.out_buffer;
        
        this->audio_buf_pos = is.audio_buf_pos;
        this->audio_buf_size = is.audio_buf_size;
        
        this->wanted_frame = is.wanted_frame;
    }
    
    
};




#endif /* VideoState_hpp */
