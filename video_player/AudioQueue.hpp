//
//  AudioQueue.hpp
//  video_player
//
//  Created by wbxie on 2024/2/22.
//

#ifndef AudioQueue_hpp
#define AudioQueue_hpp

extern "C"{
#include "libavcodec/avcodec.h"
}

#include <stdio.h>

#include "ThreadSafeQueueTemp.hpp"

class AudioQueue:public ThreadSafeQueueTemp<AVPacket>{
    
};


#endif /* AudioQueue_hpp */
