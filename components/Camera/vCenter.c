#include <string.h>

#include "esp_log.h"

#include "vCenter.h"

#define TAG "vCenter"
#define VIDEO_FRAME_BUFFER_COUNT 5
#define MUTEX_TIMEOUT_TICKS (50 / portTICK_PERIOD_MS)

typedef struct _video_center
{
    struct list_head video_list;
    size_t video_buffer_size;
    TaskHandle_t task_handle;
    SemaphoreHandle_t mutex;
} video_center;

static video_center l_v_center;

static void video_center_task(void *arg)
{
    camera_fb_t *pic = NULL;
    while (1)
    {
        pic = esp_camera_fb_get();
        if (pic)
        {
            put_vframe_to_center(pic->timestamp.tv_sec * 1000 + pic->timestamp.tv_usec / 1000, pic->format, pic->buf, pic->len);
            esp_camera_fb_return(pic);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool init_video_center(void)
{
    memset(&l_v_center, 0, sizeof(video_center));
    l_v_center.mutex = xSemaphoreCreateMutex();
    int frameSize = get_camera_frame_size();

    l_v_center.video_buffer_size = frameData[frameSize].frameWidth * frameData[frameSize].frameHeight / 5; // rough estimate for JPEG buffer size

    INIT_LIST_HEAD(&l_v_center.video_list);

    for (int i = 0; i < VIDEO_FRAME_BUFFER_COUNT; i++)
    {
        video_node *node = malloc(sizeof(video_node));
        memset(node, 0, sizeof(video_node));
        if (node)
        {
            node->data = malloc(l_v_center.video_buffer_size);
            if (node->data)
            {
                // Add to free list
                list_add_tail(&node->list, &l_v_center.video_list);
            }
            else
            {
                free(node);
                return false;
            }
        }
    }

    xTaskCreate(video_center_task, "video_center_task", 4096, NULL, 5, &l_v_center.task_handle);

    return true;
}

void deinit_video_center(void)
{
    struct list_head *pos, *n;

    vSemaphoreDelete(l_v_center.mutex);
    vTaskDelete(l_v_center.task_handle);

    list_for_each_safe(pos, n, &l_v_center.video_list)
    {
        video_node *node = (video_node *)pos;
        list_del(pos);
        if (node->data)
        {
            free(node->data);
        }
        free(node);
    }
}

bool put_vframe_to_center(unsigned int timestamp, pixformat_t format, uint8_t *data, size_t size)
{
    struct list_head *pos;
    video_node *node = NULL;

    if (size > l_v_center.video_buffer_size)
    {
        ESP_LOGW(TAG, "Video frame size too large, truncating to %d bytes", l_v_center.video_buffer_size);
        return false;
    }

    if(pdFALSE == xSemaphoreTake(l_v_center.mutex, MUTEX_TIMEOUT_TICKS))
    {
        return false;
    }

    // Find a free node
    list_for_each(pos, &l_v_center.video_list)
    {
        video_node *tmp = (video_node *)pos;
        if (tmp->ref_count == 0)
        {
            node = tmp;
            break;
        }
    }

    // 1. remove node from list
    if (node)
    {
        list_del(&node->list);
    }
    else
    {
        xSemaphoreGive(l_v_center.mutex);
        ESP_LOGW(TAG, "No free video node available");
        return false;
    }

    // 2. fill data
    memcpy(node->data, data, size);
    node->format = format;
    node->size = size;
    node->timestamp = timestamp;

    // 3. add node to the end of the list
    list_add_tail(&node->list, &l_v_center.video_list);

    xSemaphoreGive(l_v_center.mutex);
    return true;
}

video_node *get_video_frame(unsigned int timestamp)
{
    struct list_head *pos;
    video_node *node = NULL;

    if(pdFALSE == xSemaphoreTake(l_v_center.mutex, MUTEX_TIMEOUT_TICKS))
    {
        return NULL;
    }

    // Find the first node with timestamp >= requested timestamp
    list_for_each(pos, &l_v_center.video_list)
    {
        video_node *tmp = (video_node *)pos;
        if (tmp->size > 0 && tmp->timestamp >= timestamp)
        {
            node = tmp;
            node->ref_count++;
            break;
        }
    }

    xSemaphoreGive(l_v_center.mutex);
    return node;
}

video_node *get_latest_video_frame()
{
    struct list_head *pos;
    video_node *node = NULL;

    if(pdFALSE == xSemaphoreTake(l_v_center.mutex, MUTEX_TIMEOUT_TICKS))
    {
        return NULL;
    }

    list_for_each_prev(pos, &l_v_center.video_list)
    {
        video_node *tmp = (video_node *)pos;
        if (tmp->size > 0)
        {
            node = tmp;
            node->ref_count++;
            break;
        }
    }

    xSemaphoreGive(l_v_center.mutex);

    return node;
}

void put_video_frame(video_node *node)
{
    xSemaphoreTake(l_v_center.mutex, portMAX_DELAY);
    if (node && node->ref_count > 0)
    {
        node->ref_count--;
    }
    xSemaphoreGive(l_v_center.mutex);
}
