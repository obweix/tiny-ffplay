//
//  Clock.hpp
//  video_player
//
//  Created by wbxie on 2024/2/22.
//

#ifndef Clock_hpp
#define Clock_hpp

#include <stdio.h>

class Clock{
public:
    double pts = 0.0;           /* clock base */
    double pts_drift = 0.0;     /* clock base minus time at which we updated the clock */
    double last_updated = 0.0;
    double speed = 0.0;
    int serial = 0;           /* clock is based on a packet with this serial */
    int paused = 0;
    int *queue_serial = nullptr;    /* pointer to the current packet queue serial, used for obsolete clock detection */
    
};

#endif /* Clock_hpp */
