/*
 * Copyright © Microsoft Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <assert.h>

#include "frontend/drm_driver.h"
#include "pipe-loader/pipe_loader.h"
#include "pipe/p_screen.h"

#include "util/u_memory.h"
#include "vl/vl_winsys.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "gallium/include/frontend/xlibsw_api.h"
#include "gallium/winsys/sw/xlib/xlib_sw_winsys.h"
#include "target-helpers/sw_helper_public.h"
#include "vl/vl_compositor.h"

struct vl_xlib_screen {
  struct vl_screen base;
  struct pipe_context *pContext;
  Display *display;
  int screen;
  struct u_rect dirty_area;
  XVisualInfo xlib_drawable_visualInfo;
  struct xlib_drawable xlib_drawable_handle;

  struct pipe_resource *drawable_texture;
};

///
/// Destroys vl_screen (base member of extended vl_XX_screen types)
///
static void vl_screen_destroy(struct vl_screen *vscreen) {
  if (vscreen == NULL)
    return;

  if (vscreen->pscreen)
    vscreen->pscreen->destroy(vscreen->pscreen);

  if (vscreen->dev)
    pipe_loader_release(&vscreen->dev, 1);

  FREE(vscreen);
}

struct pipe_resource *vl_swrast_texture_from_drawable(struct vl_screen *vscreen,
                                                      void *drawable);

struct u_rect *vl_swrast_get_dirty_area(struct vl_screen *vscreen);

void *vl_swrast_get_private(struct vl_screen *vscreen);

static void vl_xlib_screen_destroy(struct vl_screen *vscreen) {
  if (vscreen == NULL) {
    return;
  }

  struct vl_xlib_screen *vXlibScreen = (struct vl_xlib_screen *)vscreen;
  assert(vXlibScreen);

  ///
  /// Destroy the vl_xlib_screen members
  ///
  if (vXlibScreen->drawable_texture) {
    pipe_resource_reference(&vXlibScreen->drawable_texture, NULL);
  }

  if (vXlibScreen->pContext)
    vXlibScreen->pContext->destroy(vXlibScreen->pContext);

  ///
  /// Destroy the base vl_screen (and free all the memory of vXlibScreen)
  ///
  vl_screen_destroy(&vXlibScreen->base);
}

struct vl_screen *vl_xlib_swrast_screen_create(Display *display, int screen) {
  struct vl_xlib_screen *vscreen;

  vscreen = CALLOC_STRUCT(vl_xlib_screen);
  if (!vscreen)
    goto handle_err_xlib_swrast_create;

  struct sw_winsys *xlibWinsys = xlib_create_sw_winsys(display);
  if (xlibWinsys == NULL)
    goto handle_err_xlib_swrast_create;

  vscreen->base.pscreen = sw_screen_create(xlibWinsys);

  if (!vscreen->base.pscreen)
    goto handle_err_xlib_swrast_create;

  vscreen->base.get_private = vl_swrast_get_private;
  vscreen->base.texture_from_drawable = vl_swrast_texture_from_drawable;
  vscreen->base.get_dirty_area = vl_swrast_get_dirty_area;
  vscreen->base.destroy = vl_xlib_screen_destroy;
  vscreen->pContext =
      vscreen->base.pscreen->context_create(vscreen->base.pscreen, NULL, 0);

  vl_compositor_reset_dirty_area(&vscreen->dirty_area);
  vscreen->display = display;
  vscreen->screen = screen;

  debug_printf("[vl_xlib_swrast_screen_create] - SUCCEEDED!\n");
  return &vscreen->base;

handle_err_xlib_swrast_create:
  debug_printf("[vl_xlib_swrast_screen_create] - FAILED!\n");
  if (vscreen)
    vl_xlib_screen_destroy(&vscreen->base);

  return NULL;
}

void vl_swrast_fill_xlib_drawable_desc(struct vl_screen *vscreen,
                                       Window x11VideoTargetWindow,
                                       struct xlib_drawable *pDrawableDesc);

void vl_swrast_fill_xlib_drawable_desc(struct vl_screen *vscreen,
                                       Window x11VideoTargetWindow,
                                       struct xlib_drawable *pDrawableDesc) {
  struct vl_xlib_screen *scrn = (struct vl_xlib_screen *)vscreen;
  assert(scrn);

  XWindowAttributes targetWindowAttrs = {};
  assert(XGetWindowAttributes(scrn->display, x11VideoTargetWindow,
                              &targetWindowAttrs) != 0);
  XMatchVisualInfo(scrn->display, scrn->screen, targetWindowAttrs.depth,
                   TrueColor, &scrn->xlib_drawable_visualInfo);
  scrn->xlib_drawable_handle.depth = targetWindowAttrs.depth;
  scrn->xlib_drawable_handle.drawable = x11VideoTargetWindow;
  scrn->xlib_drawable_handle.visual = scrn->xlib_drawable_visualInfo.visual;
}

struct pipe_resource *vl_swrast_texture_from_drawable(struct vl_screen *vscreen,
                                                      void *drawable) {
  struct vl_xlib_screen *scrn = (struct vl_xlib_screen *)vscreen;
  assert(scrn);
  Window x11VideoTargetWindow = (Window)drawable;
  vl_swrast_fill_xlib_drawable_desc(vscreen, x11VideoTargetWindow,
                                    &scrn->xlib_drawable_handle);

  XWindowAttributes winAttrs = {};
  assert(XGetWindowAttributes(scrn->display, x11VideoTargetWindow, &winAttrs) >
         0);
  enum pipe_format winFormat =
      vl_dri2_format_for_depth(&scrn->base, winAttrs.depth);

  bool bAllocateNewBackBuffer = true;
  if (scrn->drawable_texture) {
    bAllocateNewBackBuffer =
        (scrn->drawable_texture->width0 != winAttrs.width ||
         scrn->drawable_texture->height0 != winAttrs.height ||
         scrn->drawable_texture->format != winFormat);
  }

  if (bAllocateNewBackBuffer) {
    if (scrn->drawable_texture) {
      pipe_resource_reference(&scrn->drawable_texture, NULL);
    }

    struct pipe_resource templat;
    memset(&templat, 0, sizeof(templat));
    templat.target = PIPE_TEXTURE_2D;
    templat.format = winFormat;
    templat.width0 = winAttrs.width;
    templat.height0 = winAttrs.height;
    templat.depth0 = 1;
    templat.array_size = 1;
    templat.last_level = 0;
    templat.bind = (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET);

    scrn->drawable_texture =
        vscreen->pscreen->resource_create(vscreen->pscreen, &templat);
  } else {
    struct pipe_resource *pDrawableTex = NULL;
    pipe_resource_reference(&pDrawableTex, scrn->drawable_texture);
  }

  return scrn->drawable_texture;
}

void *vl_swrast_get_private(struct vl_screen *vscreen) {
  struct vl_xlib_screen *scrn = (struct vl_xlib_screen *)vscreen;
  assert(scrn);
  return &scrn->xlib_drawable_handle;
}

struct u_rect *vl_swrast_get_dirty_area(struct vl_screen *vscreen) {
  struct vl_xlib_screen *scrn = (struct vl_xlib_screen *)vscreen;
  assert(scrn);
  vl_compositor_reset_dirty_area(&scrn->dirty_area);
  return &scrn->dirty_area;
}
