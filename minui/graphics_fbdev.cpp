/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include "minui.h"
#include "graphics.h"

static GRSurface* fbdev_init(minui_backend*);
static GRSurface* fbdev_flip(minui_backend*);
static void fbdev_blank(minui_backend*, bool);
static void fbdev_exit(minui_backend*);

static GRSurface gr_framebuffer[2];
static bool double_buffered;
static GRSurface* gr_draw = NULL;
static GRSurface temp_buffer[1];
static int displayed_buffer;

static fb_var_screeninfo vi;
static int fb_fd = -1;

static minui_backend my_backend = {
    .init = fbdev_init,
    .flip = fbdev_flip,
    .blank = fbdev_blank,
    .exit = fbdev_exit,
};

minui_backend* open_fbdev() {
    return &my_backend;
}

static void fbdev_blank(minui_backend* backend __unused, bool blank)
{
    int ret;

    ret = ioctl(fb_fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
    if (ret < 0)
        perror("ioctl(): blank");
}

static void set_displayed_framebuffer(unsigned n)
{
    if (n > 1 || !double_buffered) return;

    vi.yres_virtual = gr_framebuffer[0].height * 2;
    vi.yoffset = n * gr_framebuffer[0].height;
    vi.bits_per_pixel = gr_framebuffer[0].pixel_bytes * 8;
    if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        perror("active fb swap failed");
    }
    displayed_buffer = n;
}

static GRSurface* fbdev_init(minui_backend* backend) {
    int fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd == -1) {
        perror("cannot open fb0");
        return NULL;
    }

    fb_fix_screeninfo fi;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return NULL;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return NULL;
    }

    // set 32 RGBA
    vi.bits_per_pixel = 32;
    vi.red.offset     = 0;
    vi.red.length     = 8;
    vi.green.offset   = 8;
    vi.green.length   = 8;
    vi.blue.offset    = 16;
    vi.blue.length    = 8;
    vi.transp.offset  = 24;
    vi.transp.length  = 8;

    if (ioctl (fd, FBIOPUT_VSCREENINFO, &vi) < 0) {
        perror("failed to put fb0 info!");
        close(fd);
        return NULL;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        perror("failed to get fb0 info");
        close(fd);
        return NULL;
    }

    // We print this out for informational purposes only, but
    // throughout we assume that the framebuffer device uses an RGBX
    // pixel format.  This is the case for every development device I
    // have access to.  For some of those devices (eg, hammerhead aka
    // Nexus 5), FBIOGET_VSCREENINFO *reports* that it wants a
    // different format (XBGR) but actually produces the correct
    // results on the display when you write RGBX.
    //
    // If you have a device that actually *needs* another pixel format
    // (ie, BGRX, or 565), patches welcome...

    printf("fb0 reports (possibly inaccurate):\n"
           "  vi.bits_per_pixel = %d\n"
           "  vi.red.offset   = %3d   .length = %3d\n"
           "  vi.green.offset = %3d   .length = %3d\n"
           "  vi.blue.offset  = %3d   .length = %3d\n",
           vi.bits_per_pixel,
           vi.red.offset, vi.red.length,
           vi.green.offset, vi.green.length,
           vi.blue.offset, vi.blue.length);

    void* bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bits == MAP_FAILED) {
        perror("failed to mmap framebuffer");
        close(fd);
        return NULL;
    }

    memset(bits, 0, fi.smem_len);

#if defined (RECOVERY_ROTATE_90) || defined (RECOVERY_ROTATE_270)
    gr_framebuffer[0].width = vi.yres;
    gr_framebuffer[0].height = vi.xres;
#else
    gr_framebuffer[0].width = vi.xres;
    gr_framebuffer[0].height = vi.yres;
#endif
    gr_framebuffer[0].row_bytes = fi.line_length;
    gr_framebuffer[0].pixel_bytes = vi.bits_per_pixel / 8;
    gr_framebuffer[0].data = reinterpret_cast<uint8_t*>(bits);
    memset(gr_framebuffer[0].data, 0, gr_framebuffer[0].height * gr_framebuffer[0].row_bytes);

    /* check if we can use double buffering */
    if (vi.yres * fi.line_length * 2 <= fi.smem_len) {
        double_buffered = true;

        // set temp_buffer to draw
#if defined (RECOVERY_ROTATE_90) || defined (RECOVERY_ROTATE_270)
        temp_buffer[0].height = vi.xres;
        temp_buffer[0].width = vi.yres;
        temp_buffer[0].row_bytes = fi.line_length * vi.yres / vi.xres;
#else
        temp_buffer[0].height = vi.yres;
        temp_buffer[0].width = vi.xres;
        temp_buffer[0].row_bytes = fi.line_length;
#endif

        temp_buffer[0].pixel_bytes = vi.bits_per_pixel / 8;
        temp_buffer[0].data = (unsigned char *)malloc(vi.yres * gr_framebuffer[0].row_bytes);
        memset(temp_buffer[0].data, 0, vi.yres * gr_framebuffer[0].row_bytes);

        memcpy(gr_framebuffer+1, gr_framebuffer, sizeof(GRSurface));

        gr_framebuffer[1].data = gr_framebuffer[0].data +
            vi.yres * gr_framebuffer[0].row_bytes;
        gr_draw = temp_buffer;
    } else {
        double_buffered = false;

        // Without double-buffering, we allocate RAM for a buffer to
        // draw in, and then "flipping" the buffer consists of a
        // memcpy from the buffer we allocated to the framebuffer.

        gr_draw = (GRSurface*) malloc(sizeof(GRSurface));
        memcpy(gr_draw, gr_framebuffer, sizeof(GRSurface));
        gr_draw->data = (unsigned char*) malloc(gr_draw->height * gr_draw->row_bytes);
        if (!gr_draw->data) {
            perror("failed to allocate in-memory surface");
            return NULL;
        }
    }

    memset(gr_draw->data, 0, vi.yres * gr_draw->row_bytes);
    fb_fd = fd;
    set_displayed_framebuffer(0);

    printf("framebuffer: %d (%d x %d)\n", fb_fd, gr_draw->width, gr_draw->height);

    fbdev_blank(backend, true);
    fbdev_blank(backend, false);

    return gr_draw;
}

static GRSurface* fbdev_flip(minui_backend* backend __unused) {
    if (double_buffered) {
#if defined(RECOVERY_ROTATE_90)
        unsigned int i = 0,  j = 0;
        unsigned int *src, *dest;
        src = (unsigned int *)gr_draw->data;
        dest = (unsigned int *)gr_framebuffer[1-displayed_buffer].data;

        for (i = 0; i < vi.xres; i++) {
            for (j = 0; j < vi.yres; j++) {
                dest[vi.xres - 1 + vi.xres * j - i] = src[vi.yres * i + j];
            }
        }
#elif defined(RECOVERY_ROTATE_180)
        unsigned int i = 0,  max = 0;
        max = gr_draw->height * gr_draw->width;
        unsigned int *src, *dest;
        src = (unsigned int *)gr_draw->data;
        dest = (unsigned int *)gr_framebuffer[1 - displayed_buffer].data;

        for (max; max > 0; max--, i++) {
            dest[max - 1] = src[i];
        }
#elif defined(RECOVERY_ROTATE_270)
        unsigned int i = 0,  j = 0;
        unsigned int *src, *dest;
        src = (unsigned int *)gr_draw->data;
        dest = (unsigned int *)gr_framebuffer[1-displayed_buffer].data;

        for (i = 0; i < vi.xres; i++) {
            for (j = 0; j < vi.yres; j++) {
                dest[vi.xres * (vi.yres - 1) - vi.xres * j + i]  =  src[vi.yres * i + j];
            }
        }
#else
        memcpy(gr_framebuffer[0].data, gr_draw->data,
            gr_framebuffer[0].row_bytes * vi.yres);

#if defined(RECOVERY_BGRA)
        // In case of BGRA, do some byte swapping
        unsigned int idx;
        unsigned char tmp;
        unsigned char* ucfb_vaddr = (unsigned char*)gr_draw->data;
        for (idx = 0 ; idx < (gr_draw->height * gr_draw->row_bytes);
                idx += 4) {
            tmp = ucfb_vaddr[idx];
            ucfb_vaddr[idx    ] = ucfb_vaddr[idx + 2];
            ucfb_vaddr[idx + 2] = tmp;
        }
#endif
        // Change gr_draw to point to the buffer currently displayed,
        // then flip the driver so we're displaying the other buffer
        // instead.
        gr_draw = gr_framebuffer + displayed_buffer;
#endif

        set_displayed_framebuffer(1-displayed_buffer);
    } else {// if single buffer
        // Copy from the in-memory surface to the framebuffer.
#if defined(RECOVERY_BGRA)
        unsigned int idx;
        unsigned char* ucfb_vaddr = (unsigned char*)gr_framebuffer[0].data;
        unsigned char* ucbuffer_vaddr = (unsigned char*)gr_draw->data;
        for (idx = 0 ; idx < (gr_draw->height * gr_draw->row_bytes); idx += 4) {
            ucfb_vaddr[idx    ] = ucbuffer_vaddr[idx + 2];
            ucfb_vaddr[idx + 1] = ucbuffer_vaddr[idx + 1];
            ucfb_vaddr[idx + 2] = ucbuffer_vaddr[idx    ];
            ucfb_vaddr[idx + 3] = ucbuffer_vaddr[idx + 3];
        }
#else
        memcpy(gr_framebuffer[0].data, gr_draw->data,
               gr_draw->height * gr_draw->row_bytes);
#endif
    }
    return gr_draw;
}

static void fbdev_exit(minui_backend* backend __unused) {
    close(fb_fd);
    fb_fd = -1;

    if (!double_buffered && gr_draw) {
        free(gr_draw->data);
        free(gr_draw);
    }
    gr_draw = NULL;
}
