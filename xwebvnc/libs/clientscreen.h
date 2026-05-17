#ifndef CLIENT_SCREEN
#define CLIENT_SCREEN

#define MAX_WIDTH 1920
#define MIN_WIDTH 1280
#define MAX_HEIGHT 1080
#define MIN_HEIGHT 720

#define MOBILE_MIN_HEIGHT 1200
#define MOBILE_MAX_HEIGHT 2400
#define MOBILE_MIN_WIDTH 540
#define MOBILE_MAX_WIDTH 1080

#include "webvnc.h"

static int current_width = 0;
static int current_height = 0;
static time_t last_request = 0;
struct ClientScreenConf clientScreenConf = {0};

int can_refresh(int width, int height);
void calculateScreenSize(int *width, int *height);
void setup_local_screen_info(int width , int height);
void getMaxResolution(int *width, int *height);
void updateCurrentResolution(int width, int height);

void updateCurrentResolution(int width, int height) {
    current_height = height;
    current_width = width;
}

int can_refresh(int width, int height) {
    time_t now = time(NULL);
    // if(width == current_width && height == current_height) return 0;
    if (last_request == 0 || difftime(now, last_request) >= 0) {
        last_request = now;
        return 1;
    }
    return 0;
}

void calculateScreenSize(int *width, int *height) {
    int w = *width;
    int h = *height;

    // 1. Clamp to min/max ranges
    if (w > MAX_WIDTH) w = MAX_WIDTH;
    if (w < MIN_WIDTH) w = MIN_WIDTH;
    if (h > MAX_HEIGHT) h = MAX_HEIGHT;
    if (h < MIN_HEIGHT) h = MIN_HEIGHT;

    if ((*width < MIN_WIDTH || *height < MIN_HEIGHT)) {
        for(int i=1;i<15;i++) {
            int doubled_w = (*width) * (1+(0.1*i));
            int doubled_h = (*height) * (1+(0.1*i));

            // PC where width > height
            if (    (doubled_w >= doubled_h 
                    && (doubled_w <= MAX_WIDTH 
                    && doubled_w >= MIN_WIDTH) 
                    && (doubled_h <= MAX_HEIGHT 
                    && doubled_h >= MIN_HEIGHT))
                    ||
                    (((double)doubled_w / doubled_h) > 2.1
                    && ((doubled_w <= MAX_WIDTH 
                    && doubled_w >= MIN_WIDTH) 
                    || (doubled_h <= MAX_HEIGHT 
                    && doubled_h >= MIN_HEIGHT)))  ) {
                w = doubled_w;
                h = doubled_h;
                break;
            }
            //mobile where height > width
            if (    doubled_w < doubled_h
                    && (doubled_w <= MOBILE_MAX_WIDTH
                    && doubled_w >= MOBILE_MIN_WIDTH)
                    && (doubled_h <= MOBILE_MAX_HEIGHT
                    && doubled_h >= MOBILE_MIN_HEIGHT)     ) {
                w = doubled_w;
                h = doubled_h;
                break;
            }
        }
    }
    if (w > clientScreenConf.max_allowed_width)
        w = clientScreenConf.max_allowed_width;
    if (h > clientScreenConf.max_allowed_height)
        h = clientScreenConf.max_allowed_height;

    *width = w;
    *height = h;
}

void setup_local_screen_info(int width , int height) {
    if(clientScreenConf.isSet) return;
    clientScreenConf.max_allowed_height = height;
    clientScreenConf.max_allowed_width = width;
    clientScreenConf.isSet = 1;
}

void getMaxResolution(int *width, int *height) {
    *width  = clientScreenConf.max_allowed_width;
    *height = clientScreenConf.max_allowed_height;
}

#endif