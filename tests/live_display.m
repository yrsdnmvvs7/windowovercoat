// Owned-window-only display transfer probe. --list reads display geometry;
// --transfer creates one temporary window, transfers it out and back, and closes it.
#include "../src/border.h"
static int32_t display_sublevel(int cid, uint32_t wid);
#define window_sub_level display_sublevel
#define main unused_resize_main
#include "live_resize.m"
#undef main
#undef window_sub_level
static uint32_t display_source;
static int32_t display_sublevel(int cid, uint32_t wid) {
  assert(wid == display_source);
  return SLSGetWindowSubLevel(cid, wid);
}
static uint64_t screen_space(NSScreen* screen) {
  CGDirectDisplayID did = [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
  CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(did);
  CFStringRef name = uuid ? CFUUIDCreateString(NULL, uuid) : NULL;
  uint64_t sid = name ? SLSManagedDisplayGetCurrentSpace(SLSMainConnectionID(), name) : 0;
  if (name) CFRelease(name);
  if (uuid) CFRelease(uuid);
  return sid;
}
@interface DisplayProbe : NSObject<NSApplicationDelegate>
@property NSWindow* window;
@property NSArray<NSScreen*>* screens;
@property int step;
@property BOOL failed;
@end
@implementation DisplayProbe
- (void)finish {
  if (probe_border) { border_destroy(probe_border); probe_border = NULL; }
  [self.window orderOut:nil]; [self.window close];
  dispatch_async(dispatch_get_main_queue(), ^{ fflush(stdout); exit(self.failed ? 1 : 0); });
}
- (void)transfer {
  if (self.step == 2) { [self finish]; return; }
  NSScreen* screen = self.screens[self.step == 0 ? 1 : 0];
  uint64_t sid = screen_space(screen);
  if (!sid) { self.failed = YES; [self finish]; return; }
  NSRect frame = screen.visibleFrame;
  [self.window setFrameOrigin:NSMakePoint(NSMidX(frame)-190, NSMidY(frame)-100)];
  // Explicitly reproduce the Space reassignment of a cross-display handoff;
  // programmatic frame changes alone do not always migrate an AppKit window.
  if (!getenv("KNIT_PROBE_FRAME_ONLY")) window_send_to_space(SLSMainConnectionID(), display_source, sid);
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 250*NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    uint64_t before = window_space_id(probe_border->cid, probe_border->wid);
    if (getenv("KNIT_PROBE_REBUILD")) border_reset_surface(probe_border);
    border_update_geometry(probe_border);
    border_refresh_space(probe_border);
    border_update_geometry(probe_border);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100*NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
      uint64_t source = window_space_id(probe_border->cid, display_source);
      uint64_t overlay = window_space_id(probe_border->cid, probe_border->wid);
      bool ordered = false;
      SLSWindowIsOrderedIn(probe_border->cid, probe_border->wid, &ordered);
      CGRect model = CGRectZero;
      SLSGetWindowBounds(probe_border->cid, display_source, &model);
      CFArrayRef entries = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, 0);
      for (NSDictionary* entry in (__bridge NSArray*)entries) {
        uint32_t wid = [entry[(__bridge NSString*)kCGWindowNumber] unsignedIntValue];
        if (wid != display_source && wid != probe_border->wid) continue;
        CGRect shown = CGRectZero;
        CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)entry[(__bridge NSString*)kCGWindowBounds], &shown);
        printf("owned %s: %.0f %.0f %.0f %.0f model=%.0f %.0f %.0f %.0f\n", wid == display_source ? "source" : "border", shown.origin.x,shown.origin.y,shown.size.width,shown.size.height,model.origin.x,model.origin.y,model.size.width,model.size.height);
      }
      if(entries) CFRelease(entries);
      BOOL ok = source == sid && overlay == sid && ordered && probe_border->visible;
      printf("transfer %d: source=%llu border-before=%llu border-after=%llu ordered=%d result=%s\n",
        self.step+1, source, before, overlay, ordered, ok ? "PASS" : "FAIL");
      fflush(stdout);
      if (!ok) self.failed = YES;
      self.step++;
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 500*NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ [self transfer]; });
    });
  });
}
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  self.screens = NSScreen.screens;
  if (self.screens.count < 2) { puts("SKIP: two displays are required"); [self finish]; return; }
  NSRect frame = self.screens[0].visibleFrame;
  self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(NSMidX(frame)-190,NSMidY(frame)-100,380,200)
      styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
  self.window.title = @"Window Overcoat - temporary display test";
  self.window.releasedWhenClosed = NO;
  display_source = (uint32_t)self.window.windowNumber;
  uint64_t tag = WINDOW_TAG_IGNORES_CYCLE;
  SLSSetWindowTags(SLSMainConnectionID(),display_source,&tag,64);
  [self.window orderFront:nil];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 200*NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    probe_border = border_create();
    probe_border->target_wid=display_source;
    probe_border->sid=window_space_id(probe_border->cid,display_source);
    probe_border->radius=10;probe_border->inner_radius=10;probe_border->focused=true;
    snprintf(probe_border->app,sizeof probe_border->app,"Finder");
    border_update(probe_border,false);
    if (!probe_border->wid || !probe_border->context) { self.failed=YES; [self finish]; return; }
    [self transfer];
  });
}
@end
int main(int argc,const char* argv[]) { @autoreleasepool {
  if (argc == 2 && !strcmp(argv[1],"--list")) {
    for (NSScreen* screen in NSScreen.screens) {
      CGDirectDisplayID did=[screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
      CGRect bounds=CGDisplayBounds(did);
      printf("display=%u builtin=%d scale=%.1f space=%llu bounds=%.0f,%.0f,%.0f,%.0f\n",did,CGDisplayIsBuiltin(did),screen.backingScaleFactor,screen_space(screen),bounds.origin.x,bounds.origin.y,bounds.size.width,bounds.size.height);
    }
    return 0;
  }
  if (argc != 2 || strcmp(argv[1],"--transfer")) return 2;
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  knit_charts_load(NULL);g_knit_pattern_by_app=true;g_settings.border_order=BORDER_ORDER_ABOVE;
  DisplayProbe* delegate=[DisplayProbe new];NSApp.delegate=delegate;
  [NSApp run];
  return delegate.failed ? 1 : 0;
} }
