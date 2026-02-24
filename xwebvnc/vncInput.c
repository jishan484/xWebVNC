#include <X11/X.h>
#include <stdio.h>
#include <unistd.h>
#include "input.h"
#include "inputstr.h"
#include "libs/clipboard.h"
#include "mi_priv.h"
#include "webvnc.h"


int wake_pipe[2];
int app_output_quality = 100;
int force_full_screen_refresh = 0;

static DeviceIntPtr kbd;
static char buffer_[30];
static Websocket * gl_ws;
static DeviceIntPtr mouse;
static ValuatorMask *mask = NULL;
static int *keysym_table[256] = {0};
static void wakeup_handler(int fd, int ready, void *data);


void add_mapping(long sym, int keycode) {
    int hi = (sym >> 8) & 0xFF;
    int lo = sym & 0xFF;
    if (!keysym_table[hi]) {
        keysym_table[hi] = calloc(256, sizeof(int));
    }
    keysym_table[hi][lo] = keycode;
}

int lookup_keycode(KeySym sym) {
    int hi = (sym >> 8) & 0xFF;
    int lo = sym & 0xFF;
    return keysym_table[hi] ? keysym_table[hi][lo] : 0;
}

void input_init(Websocket * gws) {
    kbd = inputInfo.keyboard;
    mouse = inputInfo.pointer;
    gl_ws = gws;
    if (!mask) {
        mask = valuator_mask_new(2);
        if (!mask) return;  // failed to allocate
    }
    if (!mask) return;
    // setup keycode mapping for current master keyboard
    setup_wakeup_pipe();
    XWEBVNC_init_input();
}

void process_key_press(int keycode, int is_pressed) {
    if(!app_running_indicator) return;
    QueueKeyboardEvents(kbd, is_pressed ? KeyPress : KeyRelease, keycode);
    write(wake_pipe[1], "x", 1);
}

void process_mouse_move(int x, int y) {
    if(!app_running_indicator) return;
    valuator_mask_zero(mask);
    valuator_mask_set(mask, 0, x);  // axis 0 = X
    valuator_mask_set(mask, 1, y);  // axis 1 = Y
    QueuePointerEvents(mouse, MotionNotify,0, POINTER_ABSOLUTE, mask);
    write(wake_pipe[1], "x", 1);
    int ns = buildstr(buffer_, "P ", XWEBVNC_get_pointer_sprite_name());
    ws_sendRaw(gl_ws, 129, buffer_, ns, -1);
}
extern int isProcessLockedByOtherThread;
void process_mouse_click(int button);
void process_mouse_click(int button) {
    if(!app_running_indicator) return;
    QueuePointerEvents(mouse, ButtonPress, button, 0, NULL);
    QueuePointerEvents(mouse, ButtonRelease, button, 0, NULL);
    write(wake_pipe[1], "x", 1);
}

void process_mouse_drag(int x1, int y1, int x2, int y2) {
    if(!app_running_indicator) return;
    process_mouse_move(x1, y1);
    QueuePointerEvents(mouse, ButtonPress, 1, 0, NULL);
    for (int i = 0; i <= 5; i++) {
        int nx = x1 + (x2 - x1) * i / 5;
        int ny = y1 + (y2 - y1) * i / 5;
        process_mouse_move(nx, ny);
        usleep(10000);
    }
    process_mouse_move(x2, y2);
    QueuePointerEvents(mouse, ButtonRelease, 1, 0, NULL);
    write(wake_pipe[1], "x", 1);
}

void process_mouse_scroll(int direction) {
    if(!app_running_indicator) return;
    int button = (direction > 0) ? 4 : 5; // >0 = up, <0 = down
    QueuePointerEvents(mouse, ButtonPress, button, 0, NULL);
    QueuePointerEvents(mouse, ButtonRelease, button, 0, NULL);
    write(wake_pipe[1], "x", 1);
}



void process_client_Input(char *data, uint64_t len, int clientSD) {
    if(!app_running_indicator) return;
    int x = 0, y = 0, i = 1, x2 = 0, y2 = 0;

    if (data[0] == 'C')
    {
        while (data[i] != 32 && i < len)
            x = x * 10 + data[i++] - 48;
        i++;
        while (data[i] != 32 && i < len)
            y = y * 10 + data[i++] - 48;
        process_mouse_move(x, y);
        process_mouse_click(1);
    }
    else if (data[0] == 'M')
    {
        while (data[i] != 32 && i < len)
            x = x * 10 + data[i++] - 48;
        i++;
        while (data[i] != 32 && i < len)
            y = y * 10 + data[i++] - 48;
        process_mouse_move(x,y);
    }
    else if (data[0] == 'R')
    {
        while (data[i] != 32 && i < len)
            x = x * 10 + data[i++] - 48;
        i++;
        while (data[i] != 32 && i < len)
            y = y * 10 + data[i++] - 48;
        process_mouse_move(x, y);
        process_mouse_click(3);
    }
    else if (data[0] == 'D')
    {
        while (data[i] != 32 && i < len)
            x = x * 10 + data[i++] - 48;
        i++;
        while (data[i] != 32 && i < len)
            y = y * 10 + data[i++] - 48;
        i++;
        while (data[i] != 32 && i < len)
            x2 = x2 * 10 + data[i++] - 48;
        i++;
        while (data[i] != 32 && i < len)
            y2 = y2 * 10 + data[i++] - 48;
        
        process_mouse_drag(x, y, x2, y2);
    }
    else if (data[0] == 'S')
    {
        if (data[1] == 'U')
        {
            process_mouse_scroll(1);
        }
        else
        {
            process_mouse_scroll(-1);
        }
    }
    else if (data[0] == 'K')
    {
        if (data[1] == 49)
        {
            int sym = atol(data+2);
            int keycode = lookup_keycode(sym);
            process_key_press(keycode, 1);
            process_key_press(keycode, 0);
        }
        else if (data[1] == 50)
        {
            char buffer[15] = {0};
            int _i = 2;
            while (data[_i] != ' ')
            {
                buffer[_i - 2] = data[_i];
                _i++;
            }
            _i++;
            long sym1 = atol(buffer);
            long sym2 = atol(data+_i);
            int keycode1 = lookup_keycode(sym1);
            int keycode2 = lookup_keycode(sym2);
            process_key_press(keycode1, 1);
            process_key_press(keycode2, 1);
            process_key_press(keycode2, 0);
            process_key_press(keycode1, 0);
        }
    }
    else if (data[0] == 'Q') {
        if(data[1] == 'H') app_output_quality = 90;
        else if(data[1] == 'S') app_output_quality = 20;
        else if(data[1] == 'R') app_output_quality = 100;
        force_full_screen_refresh = 1;
    }
    else if (data[0] == 'P') {
        ServerSetClipboard(data+1, len - 1);
        process_key_press(37, 1);
        process_key_press(55, 1);
        process_key_press(37, 0);
        process_key_press(55, 0);
    }
}



int buildstr(char *buff, const char *prefix, int val) {
    int n = 0;
    // copy prefix
    while (prefix && prefix[n]) {
        buff[n] = prefix[n];
        n++;
    }
    // special case for 0
    if (val == 0) {
        buff[n++] = '0';
        buff[n] = '\0';
        return n;
    }
    // write digits in reverse order
    int start = n;
    while (val != 0) {
        buff[n++] = (val % 10) + '0';
        val /= 10;
    }
    // reverse the digits part
    for (int i = 0, j = n - 1; i < (n - start) / 2; i++, j--) {
        char tmp = buff[start + i];
        buff[start + i] = buff[j];
        buff[j] = tmp;
    }
    buff[n] = '\0';
    return n;
}


void setup_wakeup_pipe(void) {
    if (pipe(wake_pipe) < 0) {
        FatalError("Failed to create wakeup pipe\n");
    }
    SetNotifyFd(wake_pipe[0], wakeup_handler, X_NOTIFY_READ, NULL);
}

static void wakeup_handler(int fd, int ready, void *data) {
    char buf[8];
    read(fd, buf, sizeof(buf));
}

void cleanup_wakeup_pipe(void) {
    close(wake_pipe[0]);
    close(wake_pipe[1]);
}