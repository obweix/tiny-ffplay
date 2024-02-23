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
        this->video_stream = is.video_stream;
//        this->audioq = is.audioq;
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
        this->video_stream = is.video_stream;
    }
    
    
};




#endif /* VideoState_hpp */
