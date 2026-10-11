#include "border.h"
#include "misc/overcoat.h"
#include "misc/autoyarn.h"
// A weak default so the test targets, which compile this file without the
// ObjC module, still link. src/autoyarn.m provides the real implementation
// and overrides this at link time in the app build.
__attribute__((weak))
bool knit_auto_yarn(const char* app, pid_t pid, uint32_t* yarn, int* chart) {
  (void)app; (void)pid; (void)yarn; (void)chart; return false;
}
__attribute__((weak))
bool knit_zigzag_active(void) { return false; }
__attribute__((weak))
bool knit_zigzag_yarn(const char* app, pid_t pid, uint32_t* yarn, int* chart) {
  (void)app; (void)pid; (void)yarn; (void)chart; return false;
}
#include "misc/apps.h"
#include "misc/chart.h"
#include <math.h>
#include "hashtable.h"
#include "misc/extern.h"
#include "windows.h"
#include <pthread.h>
#include <time.h>

extern struct settings g_settings;

static void coat_release(struct border* border);   // defined with the coat cache below

struct settings* border_get_settings(struct border* border) {
  assert(pthread_main_np() != 0);
  return border->setting_override.enabled
         ? &border->setting_override
         : &g_settings;
}

static void border_destroy_window(struct border* border) {
  if (border->context) CGContextRelease(border->context);
  coat_release(border);
  if (border->wid) SLSReleaseWindow(border->cid, border->wid);
  border->wid = 0;
  border->context = NULL;
}

// Last-resort recovery for an onscreen target whose overlay stays absent.
// Recreate only our surface; preserve the target, app settings and Space cache.
void border_reset_surface(struct border* border) {
  assert(pthread_main_np());
  if (border->is_proxy || border->external_proxy_wid) return;
  pthread_mutex_lock(&border->mutex);
  border_destroy_window(border);
  border->visible = false;
  border->geometry_valid = false;
  border->metadata_dirty = true;
  border->needs_redraw = true;
  // A newly created WindowServer window starts at full opacity.
  border->opacity = 1;
  pthread_mutex_unlock(&border->mutex);
}

static bool border_check_too_small(struct border* border, CGRect window_frame) {
  CGRect smallest_rect = CGRectInset(window_frame, 1.0, 1.0);
  if (smallest_rect.size.width < 2.f * border->inner_radius
      || smallest_rect.size.height < 2.f * border->inner_radius) {
    return true;
  }
  return false;
}

static bool border_calculate_bounds(struct border* border, CGRect* frame, struct settings* settings,
                                    const CGRect* observed_bounds) {
  CGRect window_frame = CGRectZero;
  if (observed_bounds) window_frame = *observed_bounds;
  else if (border->is_proxy) window_frame = border->target_bounds;
  else if (SLSGetWindowBounds(border->cid, border->target_wid, &window_frame)
           != kCGErrorSuccess) {
    border_hide(border);
    return false;
  }
  if (!isfinite(window_frame.origin.x) || !isfinite(window_frame.origin.y)
      || !isfinite(window_frame.size.width) || !isfinite(window_frame.size.height)
      || window_frame.size.width <= 0 || window_frame.size.height <= 0) {
    border_hide(border);
    return false;
  }

  border->target_bounds = window_frame;
  border->too_small = border_check_too_small(border, window_frame);
  if (border->too_small) {
    border_hide(border);
    return false;
  }

  float border_offset = - settings->border_width - BORDER_PADDING;
  *frame = CGRectInset(window_frame, border_offset, border_offset);

  border->origin = frame->origin;
  frame->origin = CGPointZero;


  window_frame.origin = (CGPoint){ -border_offset, -border_offset };
  border->drawing_bounds = window_frame;

  return true;
}

// ---------------------------------------------------------------------------
// Coat bitmap cache, generated off the main thread.
//
// A material's ring is static: its pixels depend only on the style and the
// window's geometry, never on what happens between repaints. So the ring is
// rendered once into an offscreen bitmap, cut into four edge strips (a whole
// window's worth of pixels would cost ~36MB per 4K window; the band costs a
// few), and every later repaint is four blits. The render itself runs on a
// worker queue, so neither the menu click that turns coats on nor a resize
// ever waits for a material's ~57ms of pixels.
//
// The knit material keeps its own path: it already draws from a cached tile
// and costs ~6ms, so strips would only spend memory.
// ---------------------------------------------------------------------------

extern struct table g_windows;   // main.c owns it; results resolve borders by id

struct coat_result {
  uint32_t target_wid;
  uint64_t job;
  struct coat_key key;
  CGImageRef strip[COAT_STRIP_COUNT];
  CGRect device_rect[COAT_STRIP_COUNT];
};

static void border_coat_ready(struct coat_result* result);
static void coat_generate(struct coat_result* result);

static dispatch_queue_t coat_queue(void) {
  static dispatch_once_t once;
  static dispatch_queue_t queue;
  // Serial on purpose: a job holds a full-window bitmap while it paints, so
  // running several at once would multiply that transient peak.
  dispatch_once(&once, ^{ queue = dispatch_queue_create("com.windowovercoat.coat", DISPATCH_QUEUE_SERIAL); });
  return queue;
}

static bool coat_key_equal(const struct coat_key* a, const struct coat_key* b) {
  return a->material == b->material && a->finish == b->finish
      && a->chart == b->chart && a->yarn == b->yarn
      && a->band == b->band && a->radius == b->radius && a->tuck == b->tuck
      && CGRectEqualToRect(a->win, b->win) && CGSizeEqualToSize(a->device, b->device)
      && CGAffineTransformEqualToTransform(a->ctm, b->ctm)
      && a->bits_per_component == b->bits_per_component
      && a->alpha_info == b->alpha_info && a->byte_order == b->byte_order
      && !strcmp(a->colorspace, b->colorspace);
}

// Everything a procedural coat's pixels depend on, captured from the border's
// own drawing state. The overlay window's context is not a bitmap context, so
// its pixel format cannot be read back: the offscreen is rendered into one
// fixed known-good layout (sRGB BGRA premultiplied, with alpha) and the blit
// lets CoreGraphics convert if the target differs.
static bool coat_build_key(struct border* border, struct settings* settings,
                           struct coat_key* key) {
  CGContextRef context = border->context;
  if (!context) return false;
  memset(key, 0, sizeof *key);
  CGAffineTransform ctm = CGContextGetCTM(context);
  float scale = ctm.a;
  if (!isfinite(scale) || scale <= 0 || !isfinite(ctm.d) || ctm.d <= 0
      || ctm.b != 0.f || ctm.c != 0.f) return false;
  CGSize pts = border->frame.size;
  if (!isfinite(pts.width) || !isfinite(pts.height)
      || pts.width < 2.f || pts.height < 2.f) return false;
  size_t pw = CGBitmapContextGetWidth(context);
  size_t ph = CGBitmapContextGetHeight(context);
  if (pw > 1 && ph > 1) key->device = CGSizeMake(pw, ph);
  else key->device = CGSizeMake(ceilf(pts.width * scale), ceilf(pts.height * scale));
  if (key->device.width < 2 || key->device.height < 2) return false;
  key->material = g_overcoat_material;
  key->finish = g_overcoat_finishes[g_overcoat_material];
  key->yarn = 0;
  key->chart = -1;
  key->band = settings->border_width;
  key->radius = border->radius;
  key->tuck = settings->border_order == BORDER_ORDER_ABOVE ? 1.f : g_knit.tuck;
  key->win = border->drawing_bounds;
  key->ctm = ctm;
  key->bits_per_component = 8;
  key->alpha_info = kCGImageAlphaPremultipliedFirst;
  key->byte_order = kCGBitmapByteOrder32Little;
  snprintf(key->colorspace, sizeof key->colorspace, "sRGB");
  return true;
}

// Depth of one strip, in device pixels: the slack between the overlay edge and
// the ring's outer edge, the band outside the window, the tuck under it, the
// inner corner's arc sag, and 1pt of antialiasing margin.
static bool coat_device_rects(const struct coat_key* key, CGRect out[COAT_STRIP_COUNT]) {
  float scale = key->ctm.a;
  if (scale <= 0 || key->device.width < 2 || key->device.height < 2) return false;
  float irad = fmaxf(0.f, key->radius - key->tuck);
  float depth = BORDER_PADDING + key->band + key->tuck
              + irad * (1.f - (float)M_SQRT1_2) + 1.f;
  float d = ceilf(depth * scale);
  float shorter = fminf(key->device.width, key->device.height);
  if (d * 2 > shorter) d = floorf(shorter / 2.f);   // a small window is all coat
  if (d < 1) return false;
  out[COAT_STRIP_BOTTOM] = CGRectMake(0, 0, key->device.width, d);
  out[COAT_STRIP_TOP] = CGRectMake(0, key->device.height - d, key->device.width, d);
  out[COAT_STRIP_LEFT] = CGRectMake(0, d, d, key->device.height - 2 * d);
  out[COAT_STRIP_RIGHT] = CGRectMake(key->device.width - d, d, d, key->device.height - 2 * d);
  for (int i = 0; i < COAT_STRIP_COUNT; i++)
    if (out[i].size.width < 1 || out[i].size.height < 1) return false;
  return true;
}

static void coat_release(struct border* border) {
  for (int i = 0; i < COAT_STRIP_COUNT; i++) {
    if (border->coat_strip[i]) { CGImageRelease(border->coat_strip[i]); border->coat_strip[i] = NULL; }
  }
  border->coat_valid = false;
}

// Cut one strip out of the rendered bitmap by copying rows: no resampling, so
// Release callback must be a plain C function (not a block) for
// CGDataProviderCreateWithData.
static void coat_data_release(void* info, const void* data, size_t size) {
  (void)data; (void)size; free(info);
}
// the pixels that reach the window are the pixels CoreGraphics produced.
static CGImageRef coat_cut(const uint8_t* source, size_t source_row, CGRect rect,
                           CGColorSpaceRef space, CGBitmapInfo info) {
  size_t width = (size_t)rect.size.width, height = (size_t)rect.size.height;
  size_t row = width * 4;
  uint8_t* buffer = malloc(row * height);
  if (!buffer) return NULL;
  const uint8_t* from = source + (size_t)rect.origin.y * source_row + (size_t)rect.origin.x * 4;
  for (size_t y = 0; y < height; y++)
    memcpy(buffer + y * row, from + y * source_row, row);
  CGDataProviderRef provider = CGDataProviderCreateWithData(buffer, buffer, row * height,
    coat_data_release);
  if (!provider) { free(buffer); return NULL; }
  CGImageRef image = CGImageCreate(width, height, 8, 32, row, space, info, provider, NULL,
      false, kCGRenderingIntentDefault);
  CGDataProviderRelease(provider);
  return image;
}

static void coat_generate(struct coat_result* result) {   // worker thread
  const struct coat_key* key = &result->key;
  size_t width = (size_t)key->device.width, height = (size_t)key->device.height;
  size_t row = width * 4;
  uint8_t* buffer = calloc(row * height, 1);
  if (buffer) {
    // One fixed, known-good layout — sRGB, 8 bits per component, BGRA
    // premultiplied little-endian, with alpha (the ring's middle is
    // transparent). CGContextDrawImage converts on the blit if the target's
    // backing store arranges its bytes differently.
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGBitmapInfo info = (CGBitmapInfo)key->alpha_info | (CGBitmapInfo)key->byte_order;
    CGContextRef context = CGBitmapContextCreate(buffer, width, height,
        key->bits_per_component, row, space, info);
    if (context) {
      CGContextConcatCTM(context, key->ctm);
      // dim stays 0: unfocused darkening is painted over the blit, so one
      // bitmap serves the focused and the unfocused state of this geometry.
      overcoat_draw(context, key->win, key->radius, key->band, (int)key->material,
                    (int)key->finish, key->yarn, key->chart, 0.f, key->tuck);
      CGContextFlush(context);
      CGRect rects[COAT_STRIP_COUNT];
      if (coat_device_rects(key, rects)) {
        bool complete = true;
        for (int i = 0; i < COAT_STRIP_COUNT; i++) {
          result->device_rect[i] = rects[i];
          result->strip[i] = coat_cut(buffer, row, rects[i], space, info);
          if (!result->strip[i]) complete = false;
        }
        if (!complete) for (int i = 0; i < COAT_STRIP_COUNT; i++) {
          if (result->strip[i]) { CGImageRelease(result->strip[i]); result->strip[i] = NULL; }
        }
      }
      CGContextRelease(context);
    }
    CGColorSpaceRelease(space);
    free(buffer);
  }
  dispatch_async(dispatch_get_main_queue(), ^{ border_coat_ready(result); });
}

static void coat_schedule(struct border* border, const struct coat_key* key) {
  assert(pthread_main_np());
  struct coat_result* result = calloc(1, sizeof(*result));
  if (!result) return;
  result->target_wid = border->target_wid;
  result->key = *key;
  border->coat_key = *key;
  border->coat_pending = true;
  border->coat_job++;
  result->job = border->coat_job;
  dispatch_async(coat_queue(), ^{ coat_generate(result); });
}

void border_coat_ready(struct coat_result* result) {      // main thread
  assert(pthread_main_np());
  struct border* border = table_find(&g_windows, &result->target_wid);
  if (!border || border->coat_job != result->job) {       // closed or repainted since
    for (int i = 0; i < COAT_STRIP_COUNT; i++) if (result->strip[i]) CGImageRelease(result->strip[i]);
    free(result);
    return;
  }
  coat_release(border);
  bool complete = true;
  for (int i = 0; i < COAT_STRIP_COUNT; i++) {
    border->coat_strip[i] = result->strip[i];
    border->coat_device_rect[i] = result->device_rect[i];
    if (!border->coat_strip[i]) complete = false;
  }
  border->coat_valid = complete;
  if (!complete) for (int i = 0; i < COAT_STRIP_COUNT; i++)
    if (border->coat_strip[i]) { CGImageRelease(border->coat_strip[i]); border->coat_strip[i] = NULL; }
  border->coat_pending = false;
  struct coat_key key = result->key;
  free(result);
  if (!border->coat_valid) { border->coat_key = key; return; }   // fall back next pass
  // The direct render that ran while the strips were generated is already on
  // screen; repaint once through the blit path so later repaints are cheap.
  border->needs_redraw = true;
  border_update(border, true);
}

// The blit path: four strips and, if the window is unfocused, the dimming the
// direct renderer would have painted inside the same ring clip.
static bool coat_blit(struct border* border, struct settings* settings, float dim, CGRect frame) {
  CGContextRef context = border->context;
  if (!context) return false;
  CGContextSetInterpolationQuality(context, kCGInterpolationNone);
  for (int i = 0; i < COAT_STRIP_COUNT; i++) {
    CGRect user = CGContextConvertRectToDeviceSpace(context, border->coat_device_rect[i]);
    (void)user;
    CGRect target = CGContextConvertRectToUserSpace(context, border->coat_device_rect[i]);
    if (CGRectIsEmpty(target)) return false;
    CGContextDrawImage(context, target, border->coat_strip[i]);
  }
  if (dim > 0.001f) {
    float tuck = settings->border_order == BORDER_ORDER_ABOVE ? 1.f : g_knit.tuck;
    CGRect outer = CGRectInset(border->drawing_bounds, -settings->border_width, -settings->border_width);
    CGRect inner = CGRectInset(border->drawing_bounds, tuck, tuck);
    CGContextSaveGState(context);
    CGMutablePathRef ring = CGPathCreateMutable();
    CGPathAddRoundedRect(ring, NULL, outer, border->radius + settings->border_width, border->radius + settings->border_width);
    CGPathAddRoundedRect(ring, NULL, inner, fmaxf(0.f, border->radius - tuck), fmaxf(0.f, border->radius - tuck));
    CGContextAddPath(context, ring);
    CGPathRelease(ring);
    CGContextEOClip(context);
    CGContextSetRGBFillColor(context, 0, 0, 0, fminf(1, dim));
    CGContextFillRect(context, outer);
    CGContextRestoreGState(context);
  }
  (void)frame;
  return true;
}

static void border_draw(struct border* border, CGRect frame, struct settings* settings) {
  CGContextSaveGState(border->context);
  border->needs_redraw = false;

  if (settings->border_style == BORDER_STYLE_KNIT) {
    CGContextClearRect(border->context, frame);
    if (!g_knit_on) {
      CGContextFlush(border->context);
      CGContextRestoreGState(border->context);
      SLSFlushWindowContentRegion(border->cid, border->wid, NULL);
      SLSWindowThaw(border->cid, border->wid);
      return;
    }
    // A per-app rule wins over the colour this window would otherwise be
    // handed, and may carry its own pattern.
    uint32_t yarn = knit_color_for_app(border->app);
    int chart = knit_pattern_for_app(border->app);
    const struct app_rule* rule = knit_app_rule(border->app);
    if (rule) yarn = rule->color;
    if (g_overcoat_material == OC_KNIT && knit_zigzag_active()) {
      // One shared pattern, every app in its icon's colour, the 37
      // hand-designed sweaters included; their own colours stay in By App.
      knit_zigzag_yarn(border->app, border->owner_pid, &yarn, &chart);
    } else if (g_overcoat_material == OC_KNIT && !rule) {
      // No hand-picked sweater: borrow the app's own colour from its icon
      // rather than hashing its name into an arbitrary one.
      uint32_t auto_yarn; int auto_chart;
      if (knit_auto_yarn(border->app, border->owner_pid, &auto_yarn, &auto_chart)) {
        yarn = auto_yarn;
        if (auto_chart >= 0) chart = auto_chart;
      }
    }

    float coat_tuck = settings->border_order == BORDER_ORDER_ABOVE ? 1.f : g_knit.tuck;
    float coat_dim = border->focused ? 0.f : g_knit_dim;

    // A procedural material costs tens of milliseconds per render; pay that
    // once per geometry on the worker queue and blit the cached strips after.
    // The knit keeps its own path: it draws from a cached tile and is cheap.
    struct coat_key key;
    bool cacheable = g_overcoat_material != OC_KNIT
                     && coat_build_key(border, settings, &key);
    if (cacheable && border->coat_valid
        && coat_key_equal(&border->coat_key, &key)
        && coat_blit(border, settings, coat_dim, frame)) {
      CGContextFlush(border->context);
      CGContextRestoreGState(border->context);
      SLSFlushWindowContentRegion(border->cid, border->wid, NULL);
      SLSWindowThaw(border->cid, border->wid);
      return;
    }
    if (cacheable && !border->coat_pending
        && !coat_key_equal(&border->coat_key, &key))
      coat_schedule(border, &key);

    overcoat_draw(border->context,
              border->drawing_bounds,
              border->radius,
              settings->border_width,
              g_overcoat_material,
              g_overcoat_finishes[g_overcoat_material],
              yarn,
              chart,
              coat_dim,
              // drawn above the window, a deep tuck would cover its content
              coat_tuck);
    CGContextFlush(border->context);
    CGContextRestoreGState(border->context);
    SLSFlushWindowContentRegion(border->cid, border->wid, NULL);
    SLSWindowThaw(border->cid, border->wid);
    return;
  }

  struct color_style color_style = border->focused
                                   ? settings->active_window
                                   : settings->inactive_window;

  CGGradientRef gradient = NULL;
  CGPoint gradient_dir[2];
  if (color_style.stype == COLOR_STYLE_SOLID
     || color_style.stype == COLOR_STYLE_GLOW) {
    bool glow = color_style.stype == COLOR_STYLE_GLOW;
    drawing_set_stroke_and_fill(border->context, color_style.color, glow);
  } else if (color_style.stype == COLOR_STYLE_GRADIENT) {
    CGAffineTransform trans = CGAffineTransformMakeScale(frame.size.width,
                                                         frame.size.height);
    gradient = drawing_create_gradient(&color_style.gradient,
                                       trans,
                                       gradient_dir          );
  }

  CGContextSetLineWidth(border->context, settings->border_width);
  CGContextClearRect(border->context, frame);

  CGRect path_rect = border->drawing_bounds;
  CGMutablePathRef inner_clip_path = CGPathCreateMutable();
  if (settings->border_style == BORDER_STYLE_SQUARE
      && settings->border_order == BORDER_ORDER_ABOVE
      && settings->border_width >= BORDER_TSMW) {
    // Inset the frame to overlap the rounding of macOS windows to create a
    // truly square border
    path_rect = CGRectInset(border->drawing_bounds,
                            BORDER_TSMN,
                            BORDER_TSMN            );

    CGPathAddRect(inner_clip_path, NULL, path_rect);
  } else {
    CGPathAddRoundedRect(inner_clip_path,
                         NULL,
                         CGRectInset(path_rect, 1.0, 1.0),
                         border->inner_radius,
                         border->inner_radius             );
  }
  drawing_clip_between_rect_and_path(border->context, frame, inner_clip_path);

  if (settings->border_style == BORDER_STYLE_SQUARE) {
    if (color_style.stype == COLOR_STYLE_SOLID
       || color_style.stype == COLOR_STYLE_GLOW) {
      drawing_draw_square_with_inset(border->context,
                                     path_rect,
                                     -settings->border_width / 2.f);
    }
    else if (color_style.stype == COLOR_STYLE_GRADIENT) {
      drawing_draw_square_gradient_with_inset(border->context,
                                              gradient,
                                              gradient_dir,
                                              path_rect,
                                              -settings->border_width / 2.f);
    }
  } else {
    float corner_radius = settings->border_style == BORDER_STYLE_ROUND_UNIFORM ? 9.0 : border->radius;

    if (settings->border_style == BORDER_STYLE_ROUND_UNIFORM) {
      drawing_draw_rounded_rect_with_inset(border->context,
                                           path_rect,
                                           corner_radius,
                                           true            );
    }

    if (color_style.stype == COLOR_STYLE_SOLID
       || color_style.stype == COLOR_STYLE_GLOW) {
      drawing_draw_rounded_rect_with_inset(border->context,
                                           path_rect,
                                           corner_radius,
                                           false           );
    } else if (color_style.stype == COLOR_STYLE_GRADIENT) {
      drawing_draw_rounded_gradient_with_inset(border->context,
                                               gradient,
                                               gradient_dir,
                                               path_rect,
                                               corner_radius  );
    }
  }
  CGGradientRelease(gradient);

  if (settings->show_background && settings->border_order != 1) {
    CGContextRestoreGState(border->context);
    CGContextSaveGState(border->context);
    color_style = settings->background;
    if (color_style.stype == COLOR_STYLE_SOLID
       || color_style.stype == COLOR_STYLE_GLOW) {
      drawing_draw_filled_path(border->context,
                               inner_clip_path,
                               color_style.color);
    }
  }
  CFRelease(inner_clip_path);
  CGContextFlush(border->context);
  CGContextRestoreGState(border->context);
  SLSFlushWindowContentRegion(border->cid, border->wid, NULL);
  SLSWindowThaw(border->cid, border->wid);
}

void border_create_window(struct border* border, CGRect frame, bool unmanaged, bool hidpi) {
  pthread_mutex_lock(&border->mutex);
  int cid = border->cid;
  border->wid = window_create(cid, frame, hidpi, unmanaged);
  if (border->wid) border->opacity = 1;

  border->frame = frame;
  border->needs_redraw = true;
  border->context = SLWindowContextCreate(cid, border->wid, NULL);
  if (border->context) {
    CGContextSetInterpolationQuality(border->context, kCGInterpolationNone);
  } else {
    border_destroy_window(border);
  }

  if (!border->sid) border->sid = window_space_id(cid, border->target_wid);
  if (border->wid) window_send_to_space(cid, border->wid, border->sid);
  pthread_mutex_unlock(&border->mutex);
}

// A display handoff need not emit a Space-change notification. Verify actual
// membership as well as the cache so an incomplete transfer is retried.
// Called by the bounded snapshot repair, never for every mouse-move event.
void border_refresh_space(struct border* border) {
  if (border->is_proxy || border->external_proxy_wid || border->sticky) return;
  pthread_mutex_lock(&border->mutex);
  uint64_t sid = window_space_id(border->cid, border->target_wid);
  if (sid) {
    uint64_t overlay_sid = border->wid ? window_space_id(border->cid, border->wid) : sid;
    if (sid != border->sid || overlay_sid != sid) {
      if (border->wid) window_send_to_space(border->cid, border->wid, sid);
      border->sid = sid;
      border->metadata_dirty = true;
      border->geometry_valid = false;
      border->needs_redraw = true; // restore geometry/order after the transfer
    }
  }
  pthread_mutex_unlock(&border->mutex);
}

void border_update_internal(struct border* border, struct settings* settings, const CGRect* observed_bounds) {
  if (border->external_proxy_wid || border->resize_suppressed) return;
  border->geometry_valid = false;

  int cid = border->cid;
  CGRect frame;
  if (!border_calculate_bounds(border, &frame, settings, observed_bounds)) return;

  // Geometry events are frequent; stacking and space metadata change only on
  // their own notifications. Avoid these synchronous server queries per stitch
  // redraw while dragging a resize handle.
  if (border->metadata_dirty) {
    uint64_t tags = window_tags(cid, border->target_wid);
    border->sticky = tags & WINDOW_TAG_STICKY;
    border->level = window_level(cid, border->target_wid);
    border->sub_level = window_sub_level(cid, border->target_wid);
    uint64_t sid = window_space_id(cid, border->target_wid);
    if (sid && sid != border->sid) {
      border->sid = sid;
      if (border->wid) window_send_to_space(cid, border->wid, sid);
    }
    border->metadata_dirty = false;
  }
  if (!border->sticky && !is_space_visible(cid, border->sid)) return;


  bool shown = false;
  SLSWindowIsOrderedIn(cid, border->target_wid, &shown);
  if (!shown && !border->is_proxy) {
    border_hide(border);
    return;
  } 

  if (!border->wid) {
    border_create_window(border,
                         frame,
                         border->is_proxy,
                         settings->hidpi  );
  }
  if (!border->wid || !border->context) return;
  if (!CGRectEqualToRect(frame, border->frame)) border->needs_redraw = true;

  // Acquire this before disabling updates: every failure path below must
  // release the server's update lock and thaw a reshaped window.
  CFTypeRef transaction = SLSTransactionCreate(cid);
  if (!transaction) return;

  bool disabled_update = false;
  if (!CGRectEqualToRect(frame, border->frame)) {
    CFTypeRef frame_region = NULL;
    CGSNewRegionWithRect(&frame, &frame_region);
    if (!frame_region) {
      CFRelease(transaction);
      return;
    }
    disabled_update = true;
    SLSDisableUpdate(cid);
    SLSWindowFreezeWithOptions(border->cid, border->wid, NULL);
    CGError shape_error = SLSSetWindowShape(border->cid, border->wid,
                                           border->origin.x, border->origin.y,
                                           frame_region);
    CFRelease(frame_region);
    if (shape_error != kCGErrorSuccess) {
      SLSWindowThaw(cid, border->wid);
      SLSReenableUpdate(cid);
      CFRelease(transaction);
      return;
    }

    // The drawing context is created once, against the window's backing store
    // as it was at creation time. Reshaping the window does not grow it, so
    // after a window grows, the newly exposed area cannot be painted — and
    // since a CGContext is bottom-left origin, that area is the TOP of the
    // window on screen. Left alone this shows as the knit detaching from the
    // top edge whenever a window is resized larger. Rebind it to the reshaped
    // window before drawing.
    CGContextRef resized_context = SLWindowContextCreate(cid, border->wid, NULL);
    if (!resized_context) {
      SLSWindowThaw(cid, border->wid);
      SLSReenableUpdate(cid);
      CFRelease(transaction);
      return;
    }
    CGContextRelease(border->context);
    border->context = resized_context;
    CGContextSetInterpolationQuality(border->context, kCGInterpolationNone);

    border->needs_redraw = true;
    border->frame = frame;
  }

  if (border->needs_redraw) border_draw(border, frame, settings);

  SLSTransactionMoveWindowWithGroup(transaction, border->wid, border->origin);

  if (!border->is_proxy) {
    CGAffineTransform transform = CGAffineTransformIdentity;
    transform.tx = -border->origin.x;
    transform.ty = -border->origin.y;
    SLSTransactionSetWindowTransform(transaction,
                                     border->wid,
                                     0,
                                     0,
                                     transform   );
  }
  SLSTransactionSetWindowLevel(transaction, border->wid, border->level);
  SLSTransactionSetWindowSubLevel(transaction, border->wid, border->sub_level);
  SLSTransactionOrderWindow(transaction,
                            border->wid,
                            settings->border_order,
                            border->target_wid      );
  SLSTransactionCommit(transaction, 0);
  CFRelease(transaction);

  uint64_t set_tags = (1ULL << 1) | (1ULL << 9);
  uint64_t clear_tags = 0;

  if (border->sticky) {
    set_tags |= WINDOW_TAG_STICKY;
    clear_tags |= (1ULL << 45);
  } else {
    clear_tags |= WINDOW_TAG_STICKY;
  }

  SLSSetWindowTags(cid, border->wid, &set_tags, 0x40);
  SLSClearWindowTags(cid, border->wid, &clear_tags, 0x40);

  if (disabled_update) SLSReenableUpdate(cid);
  border->geometry_valid = true;
  border->visible = true;
}

void border_init(struct border* border, int cid) {
  memset(border, 0, sizeof(struct border));
  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&border->mutex, &mattr);
  pthread_mutexattr_destroy(&mattr);
  border->metadata_dirty = true;
  border->opacity = 1;
  animation_init(&border->animation);
  if (cid) border->cid = cid;
  else border->cid = SLSMainConnectionID();
}

struct border* border_create() {
  struct border* border = malloc(sizeof(struct border));
  int cid = 0;
  SLSNewConnection(0, &cid);
  border_init(border, cid);
  return border;
}

void border_destroy(struct border* border) {
  border_hide(border);
  dispatch_async(dispatch_get_main_queue(), ^{
    pthread_mutex_lock(&border->mutex);
    border_destroy_window(border);
    if (border->proxy) border_destroy(border->proxy);
    animation_stop(&border->animation);
    if (!border->is_proxy && border->cid != SLSMainConnectionID())
      SLSReleaseConnection(border->cid);
    pthread_mutex_unlock(&border->mutex);
    pthread_mutex_destroy(&border->mutex);
    free(border);
  });
}

bool border_suppress_live_resize(struct border* border, CGRect bounds) {
  assert(pthread_main_np());
  if (!g_knit_on || border->is_proxy || border->external_proxy_wid) return false;
  if (!border->resize_suppressed && (!border->visible
      || CGSizeEqualToSize(bounds.size, border->drawing_bounds.size))) return false;
  bool held = CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
  CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
  if (!border->resize_suppressed || held
      || !CGSizeEqualToSize(bounds.size, border->resize_observed_size)) {
    border->resize_observed_size = bounds.size;
    border->resize_settle_after = now + .06;
  }
  if (!held && now >= border->resize_settle_after) {
    border->resize_suppressed = false;
    return false;
  }
  border->resize_suppressed = true;
  if (border->visible) border_hide(border);
  return true;
}

static void border_apply_geometry(struct border* border, CGRect window_frame) {
  if (border_suppress_live_resize(border, window_frame)) return;
  struct settings* settings = border_get_settings(border);
  pthread_mutex_lock(&border->mutex);
  if (border->external_proxy_wid) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }
  // Keep movement in the same main-queue order as resize, hide and destroy.
  // A queued worker could otherwise move a hidden border or access it after
  // close, and bursts of workers add visible trailing latency to a drag.
  // AppKit may issue a move before the matching size event when resizing
  // from the top or left. Reconcile both dimensions from the same geometry
  // path instead of moving the old-size sweater to the new origin.
  if (!border->geometry_valid || !border->wid || !border->context
      || border->needs_redraw || border->too_small || border->metadata_dirty
      || !isfinite(window_frame.origin.x) || !isfinite(window_frame.origin.y)
      || !CGSizeEqualToSize(window_frame.size, border->drawing_bounds.size)) {
    border_update_internal(border, settings, &window_frame);
    pthread_mutex_unlock(&border->mutex);
    return;
  }
  // MOVE and RESIZE can describe the same change. Keep reading fresh bounds,
  // but do not repeat server queries or submit another transaction for it.
  if (CGRectEqualToRect(window_frame, border->target_bounds)) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }
  CGPoint origin = { .x = window_frame.origin.x
                          - settings->border_width
                          - BORDER_PADDING,
                     .y = window_frame.origin.y
                          - settings->border_width
                          - BORDER_PADDING          };

  CFTypeRef transaction = SLSTransactionCreate(border->cid);
  if (transaction) {
    SLSTransactionMoveWindowWithGroup(transaction, border->wid, origin);

    // Re-assert where we sit in the stack. A move never goes through
    // border_update_internal, so without this the border keeps whatever
    // z-position it was last given — and dragging a window across another
    // one leaves its knit stranded underneath that window. Ordering is
    // relative to the target; including it in the move transaction keeps the
    // border's position and depth together.
    SLSTransactionOrderWindow(transaction,
                              border->wid,
                              settings->border_order,
                              border->target_wid     );

    SLSTransactionCommit(transaction, 0);
    CFRelease(transaction);
    border->target_bounds = window_frame;
    border->origin = origin;
  }
  pthread_mutex_unlock(&border->mutex);
}

static bool border_observe_window(struct border* border, CGRect* bounds, double* opacity) {
  *opacity = border->opacity;
  if (border->native_transform) return false;
  if (border->is_proxy) { *bounds = border->target_bounds; return true; }
  // Keep synchronous dictionary creation out of MOVE/RESIZE callbacks. The
  // recovery snapshot independently verifies actual visibility and opacity.
  return SLSGetWindowBounds(border->cid, border->target_wid, bounds) == kCGErrorSuccess;
}

static void border_apply_opacity(struct border* border, double opacity) {
  if (!isfinite(opacity)) return;
  opacity = fmax(0, fmin(1, opacity));
  if (border->wid && fabs(border->opacity - opacity) > .001
      && SLSSetWindowAlpha(border->cid, border->wid, opacity) == kCGErrorSuccess)
    border->opacity = opacity;
}

void border_update_geometry(struct border* border) {
  CGRect bounds;
  double opacity;
  if (!border_observe_window(border, &bounds, &opacity)) {
    border_hide(border);
    return;
  }
  border_update_geometry_from_snapshot(border, bounds, opacity);
}

void border_update_geometry_from_snapshot(struct border* border, CGRect bounds, double opacity) {
  border_apply_geometry(border, bounds);
  border_apply_opacity(border, opacity);
}

void border_update(struct border* border, bool try_async) {
  (void)try_async; // Geometry and cached drawing state are main-queue owned.
  pthread_mutex_lock(&border->mutex);
  struct settings* settings = border_get_settings(border);
  CGRect bounds;
  double opacity;
  if (border_observe_window(border, &bounds, &opacity)
      && !border_suppress_live_resize(border, bounds)) {
    border_update_internal(border, settings, &bounds);
    border_apply_opacity(border, opacity);
  } else border_hide(border);
  pthread_mutex_unlock(&border->mutex);
}

// An app can raise a whole group of windows without changing its focused
// window. Re-read stacking after that operation has settled; do not resize,
// redraw, or unhide an overlay as a side effect of repairing its depth.
void border_reorder(struct border* border) {
  struct settings* settings = border_get_settings(border);
  pthread_mutex_lock(&border->mutex);
  if (border->resize_suppressed || !border->wid || !border->context || border->too_small
      || border->is_proxy || border->external_proxy_wid
      || (!border->sticky && !is_space_visible(border->cid, border->sid))) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }
  bool target_shown = false, border_shown = false;
  if (SLSWindowIsOrderedIn(border->cid, border->target_wid, &target_shown) != kCGErrorSuccess
      || !target_shown) {
    border_hide(border);
    pthread_mutex_unlock(&border->mutex);
    return;
  }
  if (SLSWindowIsOrderedIn(border->cid, border->wid, &border_shown) != kCGErrorSuccess
      || !border_shown) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }
  int level = window_level(border->cid, border->target_wid);
  int sub_level = window_sub_level(border->cid, border->target_wid);
  CFTypeRef transaction = SLSTransactionCreate(border->cid);
  if (transaction) {
    SLSTransactionSetWindowLevel(transaction, border->wid, level);
    SLSTransactionSetWindowSubLevel(transaction, border->wid, sub_level);
    SLSTransactionOrderWindow(transaction, border->wid, settings->border_order,
                             border->target_wid);
    SLSTransactionCommit(transaction, 0);
    // Match the existing transaction paths: the private commit return is not
    // a reliable acknowledgement. Cache the target metadata we actually read.
    border->level = level;
    border->sub_level = sub_level;
    CFRelease(transaction);
  }
  pthread_mutex_unlock(&border->mutex);
}

void border_hide(struct border* border) {
  pthread_mutex_lock(&border->mutex);
  border->geometry_valid = false;
  border->visible = false;
  if (border->wid) {
    CFTypeRef transaction = SLSTransactionCreate(border->cid);
    if (transaction) {
      SLSTransactionOrderWindow(transaction,
                                border->wid,
                                0,
                                border->target_wid);
      SLSTransactionCommit(transaction, 0);
      CFRelease(transaction);
    }
  }
  pthread_mutex_unlock(&border->mutex);
}

void border_unhide(struct border* border) {
  pthread_mutex_lock(&border->mutex);
  if (border->resize_suppressed || border->native_transform || border->too_small
      || border->external_proxy_wid
      || (!border->sticky && !is_space_visible(border->cid, border->sid))) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }

  if (border->wid) {
    struct settings* settings = border_get_settings(border);
    CFTypeRef transaction = SLSTransactionCreate(border->cid);
    if (transaction) {
      SLSTransactionOrderWindow(transaction,
                                border->wid,
                                settings->border_order,
                                border->target_wid      );
      SLSTransactionCommit(transaction, 0);
      CFRelease(transaction);
      border->visible = true;
    }
  }
  pthread_mutex_unlock(&border->mutex);
}
