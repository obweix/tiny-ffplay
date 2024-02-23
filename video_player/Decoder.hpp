//
//  Decoder.hpp
//  video_player
//
//  Created by wbxie on 2024/2/23.
//

#ifndef Decoder_hpp
#define Decoder_hpp

extern "C"{
#include "libavcodec/avcodec.h"
}

#include <stdio.h>
class Decoder{
public:
    AVCodecContext *avctx;
    
    Decoder(){
        
    }
    
    Decoder (AVCodecContext* avctx){
        this->avctx = avctx;
    }
    
    Decoder& operator=(const Decoder& decorder){
        this->avctx = decorder.avctx;
        return *this;
    }
    
};
#endif /* Decoder_hpp */
