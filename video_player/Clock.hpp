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
    double pts;           /* clock base */
    double pts_drift;     /* clock base minus time at which we updated the clock */
    double last_updated;
    double speed;
    int serial;           /* clock is based on a packet with this serial */
    int paused;
    int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
};

#endif /* Clock_hpp */
