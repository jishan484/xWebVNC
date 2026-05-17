#include <X11/Xdefs.h>
#include <dix-config.h>
#include "dix/dix_priv.h"
#include "input_priv.h"
#include "libs/clipboard.h"
#include "randr/randrstr_priv.h"
#include "screenint.h"
#include "scrnintstr.h"
#include "damage.h"
#include "webvnc.h"
#include <unistd.h>
#include <lz4.h>


#define getDelay(fps) (995000 / (fps)) //fixed drift applied


static char *app_buffer;
int app_busy_indicator=0;
int app_running_indicator = 0;
static pthread_t ws_thread;
static int app_buffer_size;
static pthread_t vnc_thread;
static pthread_t audio_thread;
struct XScreenConf screenConf;
static char vnc_App_config[255];
static DamagePtr myDamage = NULL;
static Websocket *global_websocket;
static char * app_temp_frame_buffer;
static ScreenPtr global_screen_pointer;
static DamageQueue *global_damage_queue;
static int screenResizeRequest[2] = {0};

void ws_onconnect(int sid);
void *ws_thread_func(void *arg);
void *vnc_thread_func(void *arg);
void XWEBVNC_send_next_frame(void);
void XWEBVNC_damage_setup(ScreenPtr screen);
static void DamageExtDestroy(DamagePtr pDamage, void *closure){}
void myDamageReport(DamagePtr pDamage, RegionPtr pRegion, void *closure);



/*
*  vnc setup method, call once the main method's cli arguments are parsed
-> takes nothing
-> returns nothing
*/
void XWEBVNC_setup(void) {
  XWEBVNC_log("Initializing App");

  EnableCursor = FALSE;
  global_damage_queue = dq_init(0, 0, 100);
  app_buffer_size = 1920 * 1080 * 4 * sizeof(unsigned char);
  app_buffer = (char *)malloc(app_buffer_size);
  app_temp_frame_buffer = (char *)malloc(app_buffer_size);
  global_websocket = ws_init();

  if (!global_damage_queue || !app_buffer || !global_websocket) {
    XWEBVNC_log("Initialization failed! Excluding XWebVNC extension");
    if (global_damage_queue)
      free(global_damage_queue);
    if (global_websocket)
      free(global_websocket);
    if (app_buffer)
      free(app_buffer);
    return;
  }

  app_running_indicator = 1;
  app_output_quality = 20;

  XWEBVNC_start_app_main_loop();
}

/*
* XWEBVNC_create_config, create the vnc_app_config data and sends to available clients
* takes no param, returns nothing, usages global screen ptr and other extern vars
*/

void XWEBVNC_create_config(void) {
  snprintf(vnc_App_config, sizeof(vnc_App_config),
             "{'screen':%d,'width':%d,'height':%d, 'quality':%d, 'depth':%d, 'audio': %d, 'isForced': %d}",
             global_screen_pointer->myNum, global_screen_pointer->width,
             global_screen_pointer->height, app_output_quality, screenConf.bit_per_pixel, app_audio_active, force_full_screen_refresh);
  ws_sendText(global_websocket, vnc_App_config, -1);
}


/*
*  vnc init method, call every time the main method's while loop iterates
-> takes screen ponter
-> returns nothing
*/
void XWEBVNC_init(ScreenPtr screen) {
    if(!app_running_indicator) return;
    global_screen_pointer = screen;
    XWEBVNC_damage_setup(screen);
    dq_reset(global_damage_queue, screen->width, screen->height);
    screen_pix_config(screen, &screenConf);
    XWEBVNC_create_config();
    XWEBVNC_log(vnc_App_config);
    input_init(global_websocket, screen->width, screen->height);
}

/*
*  vnc cleanup method, call every time the main method's while loop iterates
-> takes nothing
-> returns nothing
-> does nothing ( required for future plans )
*/
void XWEBVNC_cleanup(void) {
  // nothing to do for now
  XWEBVNC_log("X Server Session restarted");
}

/*
vnc close method, call at the end of main method
-> takes nothing
-> returns nothing
*/
void XWEBVNC_close(void) {
    XWEBVNC_log("XServer on-close cleanup activity started");
    ServerClearClipboard();

    while(app_busy_indicator) {
      usleep(1000);
    }
    app_busy_indicator = 1;
    app_running_indicator = 0;
    force_full_screen_refresh = 0;

    pthread_join(audio_thread, NULL);
    pthread_join(vnc_thread, NULL);
    ws_close(global_websocket);
    usleep(10000);
    pthread_join(ws_thread, NULL);

    if (global_damage_queue) {
        dq_destroy(global_damage_queue);
        free(global_damage_queue);
        global_damage_queue = NULL;
    }

    if (app_buffer) {
        free(app_buffer);
        app_buffer = NULL;
    }

    if (app_temp_frame_buffer) {
        free(app_temp_frame_buffer);
        app_temp_frame_buffer = NULL;
    }

    if(global_websocket) {
        free(global_websocket);
        global_websocket = NULL;
    }

    XWEBVNC_log("Session closed successfully");
}

void XWEBVNC_start_app_main_loop(void) {
  ws_begin(global_websocket, XWEBVNC_http_server_port);
  ws_assign(global_websocket, process_client_Input, ws_onconnect);

  if (pthread_create(&ws_thread, NULL, ws_thread_func, 0) != 0) {
    perror("Failed to create Websocket thread");
    return;
  }

  if (pthread_create(&vnc_thread, NULL, vnc_thread_func, 0) != 0) {
    perror("Failed to create VNC thread");
    return;
  }

  if (pthread_create(&audio_thread, NULL, audioLoop, global_websocket) != 0) {
    perror("Failed to create Audio thread");
    return;
  }
}

/*
vnc send frame method, call to send frame of size x,y,w,h
-> takes size of frame
-> must be valid size (no check has been added in this)
-> returns nothing
*/
void XWEBVNC_send_frame(int x, int y, int width, int height) {
  app_busy_indicator = 1;
  if(!app_running_indicator) printf("app closed but\n");
  if(!app_running_indicator) return;
  if (force_full_screen_refresh) {
    x = 0;
    y = 0;
    width = global_screen_pointer->width;
    height = global_screen_pointer->height;
    XWEBVNC_create_config();
    force_full_screen_refresh = 0;
  }
  
  int img_size = 0;
  char data[256];
  if(!app_running_indicator) return;
  unsigned char *screen_image_rect_ptr = extractRectRGB16or32(global_screen_pointer, x, y, width, height, &screenConf);
  char *jpeg_data = (char* )compress_image_to_jpeg(screen_image_rect_ptr, screenConf.stride, width, height, &img_size, app_output_quality);

  snprintf(
      data, sizeof(data),
      "VPD%d %d %d %d %d %d \n", x, y, width, height, 24, img_size
  );
  ws_p_sendRaw(global_websocket, 130, data, jpeg_data, strlen(data), img_size, -1);
  free(jpeg_data);
  app_busy_indicator = 0;
}




  //------------------//
 // internal methods //
//------------------//

void ws_onconnect(int sid) {
  XWEBVNC_log_append("New Websocket client connection established! sid: ", sid);
  if (!global_screen_pointer) return;
  ws_sendText(global_websocket, vnc_App_config, sid);
  XWEBVNC_send_frame(0, 0, global_screen_pointer->width,
                     global_screen_pointer->height);
}

void XWEBVNC_send_next_frame(void) {
  if (!app_running_indicator) return;
  int i = dq_merge(global_damage_queue);
  while (app_running_indicator && !app_busy_indicator && ws_has_client(global_websocket) && i--) {
    Rect r;
    if (!dq_get(global_damage_queue, &r)) return;
    XWEBVNC_send_frame(r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1);
  }
}

void *ws_thread_func(void *arg) {
  free(arg);
  arg = NULL;
  ws_connections(global_websocket);
  XWEBVNC_log("http/ws server thread ended");
  return NULL;
}

void *vnc_thread_func(void *arg) {
  int delay = getDelay(15);
  while (app_running_indicator) {
    XWEBVNC_send_next_frame();
    usleep(delay);
  }
  XWEBVNC_log("vnc main thread ended");
  return NULL;
}

void XWEBVNC_damage_setup(ScreenPtr screen) {
  myDamage = DamageCreate(myDamageReport,
                            DamageExtDestroy,
                            DamageReportRawRegion,
                            FALSE,
                            screen,
                            NULL);
    if (!myDamage) {
        XWEBVNC_log("Failed to create Damage object\n");
        return;
    }
    // : Attach to root window is required
    DamageRegister((DrawablePtr)screen->root, myDamage);
    ErrorF("[XwebVNC] > LOG: Damage listener attached to screen %d root window\n", screen->myNum);
}

void myDamageReport(DamagePtr pDamage, RegionPtr pRegion, void *closure) {
    int nboxes;
    pixman_box16_t *boxes = pixman_region_rectangles(pRegion, &nboxes);
    if(!app_running_indicator && !global_damage_queue) return;
    for (int i = 0; i < nboxes; i++) {
        Rect r;
        r.x1 = boxes[i].x1;
        r.y1 = boxes[i].y1;
        r.x2 = boxes[i].x2;
        r.y2 = boxes[i].y2;
        dq_push(global_damage_queue, r);
    }
}

// ------------------//
//      External    //
//-----------------//
Bool ResizeWorkProc(ClientPtr client, void *any) {
  force_full_screen_refresh = 1;
  if (global_screen_pointer == 0)
    return 0;
  int width = screenResizeRequest[0];
  int height = screenResizeRequest[1];
  int mmWidth =
      (width * global_screen_pointer->mmWidth) / global_screen_pointer->width;
  int mmHeight = (height * global_screen_pointer->mmHeight) /
                 global_screen_pointer->height;
  RRScreenSizeSet(global_screen_pointer, width, height, mmWidth, mmHeight);
  return 1;
}

int resizeScreen(int width, int height) {
  screenResizeRequest[0] = width;
  screenResizeRequest[1] = height;
  return QueueWorkProc(ResizeWorkProc, NULL, NULL);
}

//------------------//
// logs for XWebVNC //
//------------------//
void XWEBVNC_log(const char *message) { ErrorF("[XwebVNC] > %s\n", message); }
void XWEBVNC_log_append(const char *message, int number) {
  ErrorF("[XwebVNC] > %s%d\n", message, number);
}