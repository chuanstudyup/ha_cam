#ifndef _VCENTER_H_
#define _VCENTER_H_

#include "Camera.h"
#include "list.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

typedef struct _video_node
{
    struct list_head list;
    unsigned int timestamp; // in ms
    pixformat_t format;
    size_t size;
    uint8_t *data;
    int ref_count;
} video_node;

bool init_video_center(void);
void deinit_video_center(void);

bool put_vframe_to_center(unsigned int timestamp, pixformat_t format, uint8_t *data, size_t size);
video_node *get_video_frame(unsigned int timestamp);
video_node *get_latest_video_frame();
void put_video_frame(video_node *node);

#endif /* _VCENTER_H_ */