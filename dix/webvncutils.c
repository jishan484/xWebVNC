#include "os/osdep.h"
#include "xkbsrv.h"
#include "xwebvnc/webvnc.h"
#include "scrnintstr.h"
#include "servermd.h"
#include "windowstr.h"
#include <X11/X.h>
#include <jpeglib.h>

XID current_cursor;


void XWEBVNC_init_input(void) {
    DeviceIntPtr dev;
    for (dev = inputInfo.devices; dev; dev = dev->next) {
        if (dev->type == MASTER_KEYBOARD) {
            XkbDescPtr xkb = inputInfo.keyboard->key->xkbInfo->desc;
            if (!xkb)
                return;
            int min = xkb->min_key_code;
            int max = xkb->max_key_code;

            for (int kc = min; kc <= max; ++kc) {
                XkbSymMapPtr map = &xkb->map->key_sym_map[kc];

                for (int level = 0; level < map->width; ++level) {
                    KeySym ks = xkb->map->syms[map->offset + level];
                    if (ks != NoSymbol) {
                        if(!lookup_keycode(ks))
                            add_mapping(ks, kc);
                    }
                }
            }
        }
    }
}

void screen_pix_config(ScreenPtr pScreen, XScreenConf * screenConf) {
    PixmapPtr pPix              = (*pScreen->GetScreenPixmap)(pScreen);
    screenConf->buffer_ptr      = pPix->devPrivate.ptr;
    screenConf->stride          = pPix->devKind;       // bytes per row
    screenConf->bit_per_pixel   = pPix->drawable.bitsPerPixel;
}

unsigned char *extractRectRGB16or32(ScreenPtr pScreen,
                                int x, int y,
                                int rect_w, int rect_h,
                                XScreenConf * screenConf)
{
    return (screenConf->bit_per_pixel == 32) ? (unsigned char *)(screenConf->buffer_ptr + y * screenConf->stride + x * 4)
    : (unsigned char *)(screenConf->buffer_ptr + y * screenConf->stride + x * 2);
}

unsigned char *compress_image_to_jpeg(unsigned char *fb_data,
                                      int stride,
                                      int width, int height,
                                      int *out_size,
                                      int quality)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    unsigned char *jpeg_data = NULL;
    unsigned long jpeg_size = 0;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &jpeg_data, &jpeg_size);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 4;          // B, G, R, X
    cinfo.in_color_space = JCS_EXT_BGRX; // libjpeg-turbo extension

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    if (quality >= 100) {
        // Near-lossless: force no chroma subsampling (4:4:4)
        cinfo.comp_info[0].h_samp_factor = 2;
        cinfo.comp_info[0].v_samp_factor = 1;
        cinfo.comp_info[1].h_samp_factor = 1;
        cinfo.comp_info[1].v_samp_factor = 1;
        cinfo.comp_info[2].h_samp_factor = 1;
        cinfo.comp_info[2].v_samp_factor = 1;
    }

    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        unsigned char *row_pointer = fb_data + cinfo.next_scanline * stride;
        jpeg_write_scanlines(&cinfo, &row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    *out_size = (int)jpeg_size;
    jpeg_destroy_compress(&cinfo);

    return jpeg_data;  // must be freed by caller
}

void getSubImage(int x, int y, int rect_w, int rect_h, XScreenConf *screenConf, char *temp_buffer) {
    if (!screenConf || !screenConf->buffer_ptr || !temp_buffer) return;

    int bytes_per_pixel = screenConf->bit_per_pixel / 8;
    int row_bytes = rect_w * bytes_per_pixel;  // number of bytes to copy per row
    unsigned char *src = screenConf->buffer_ptr + y * screenConf->stride + x * bytes_per_pixel;
    unsigned char *dst = (unsigned char*)temp_buffer;

    for (int i = 0; i < rect_h; i++) {
        memcpy(dst, src, row_bytes);  // copy only rectangle width, not full stride
        dst += row_bytes;             // move output pointer
        src += screenConf->stride;    // move source pointer to next row in framebuffer
    }
}


char *XWEBVNC_get_pointer_sprite_name(size_t *out_size)
{
    static unsigned char *data = NULL;
    static size_t allocated = 0;

    CursorPtr cursor;
    CursorBitsPtr bits;

    size_t row_bytes;
    size_t bitmap_size;
    size_t total_size;
    size_t p;

    if (out_size)
        *out_size = 0;

    if (!inputInfo.pointer ||
        !inputInfo.pointer->spriteInfo ||
        !inputInfo.pointer->spriteInfo->sprite)
        return NULL;

    cursor = inputInfo.pointer->spriteInfo->sprite->current;
    if(current_cursor == cursor->id){
        *out_size = 0;
        return 0;
    }
    current_cursor = cursor->id;
    
    if (!cursor || !cursor->bits)
        return NULL;

    bits = cursor->bits;

    if (!bits->source || !bits->mask)
        return NULL;

    row_bytes = BitmapBytePad(bits->width);
    bitmap_size = row_bytes * bits->height;

    total_size = 3 + 8 + bitmap_size + bitmap_size;

    if (allocated < total_size) {
        unsigned char *tmp = realloc(data, total_size);

        if (!tmp)
            return NULL;

        data = tmp;
        allocated = total_size;
    }

    p = 0;

    /* magic */
    data[p++] = 'P';
    data[p++] = 'S';
    data[p++] = '\n';

    /* width */
    data[p++] = (bits->width >> 8) & 0xff;
    data[p++] = bits->width & 0xff;

    /* height */
    data[p++] = (bits->height >> 8) & 0xff;
    data[p++] = bits->height & 0xff;

    /* hotspot X */
    data[p++] = (bits->xhot >> 8) & 0xff;
    data[p++] = bits->xhot & 0xff;

    /* hotspot Y */
    data[p++] = (bits->yhot >> 8) & 0xff;
    data[p++] = bits->yhot & 0xff;
    /* source */
    memcpy(data + p, bits->source, bitmap_size);
    p += bitmap_size;

    /* mask */
    memcpy(data + p, bits->mask, bitmap_size);
    p += bitmap_size;

    if (out_size)
        *out_size = p;

    return (char *)data;
}