//
//  VideoQueue.hpp
//  video_player
//
//  Created by wbxie on 2024/2/22.
//

#ifndef VideoQueue_hpp
#define VideoQueue_hpp

extern "C"{
#include "libavcodec/avcodec.h"
}

#include <stdio.h>

#include "ThreadSafeQueueTemp.hpp"

class VideoQueue:public ThreadSafeQueueTemp<AVPacket>{
    
};


#endif /* VideoQueue_hpp */
