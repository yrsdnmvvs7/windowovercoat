#pragma once
#include "extern.h"
#include "helpers.h"

extern void _AXUIElementGetWindow(CFTypeRef window, uint32_t* wid);
static bool g_ax_trust = false;

static inline bool ax_check_trust(bool silent) {
  if (silent) return AXIsProcessTrusted();
  CFStringRef key =  kAXTrustedCheckOptionPrompt;
  CFDictionaryRef dict = CFDictionaryCreate(kCFAllocatorDefault, (const void**)&key, (const void**)&kCFBooleanTrue, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  bool trusted = AXIsProcessTrustedWithOptions(dict);
  CFRelease(dict);

  g_ax_trust = trusted;
  return trusted;
}

static inline uint32_t ax_get_front_window(int cid) {
  // Permission can be revoked while the app is running. Fall back quietly;
  // a decorative border should not quit or summon a permission prompt here.
  if (!ax_check_trust(true)) return 0;

  ProcessSerialNumber psn = {0};
  if (_SLPSGetFrontProcess(&psn) != noErr) return 0;
  int target_cid = 0;
  if (SLSGetConnectionIDForPSN(cid, &psn, &target_cid) != kCGErrorSuccess) return 0;

  pid_t pid = 0;
  if (SLSConnectionGetPID(target_cid, &pid) != kCGErrorSuccess || pid <= 0) return 0;

  AXUIElementRef app = AXUIElementCreateApplication(pid);
  if (!app) return 0;
  // Focus is advisory: a busy foreign app must not hold our drawing thread
  // for the accessibility API's default timeout. The caller has a server fallback.
  if (AXUIElementSetMessagingTimeout(app, 0.008f) != kAXErrorSuccess) {
    CFRelease(app);
    return 0;
  }
  CFTypeRef window = NULL;
  AXError error = AXUIElementCopyAttributeValue(app, kAXFocusedWindowAttribute, &window);
  CFRelease(app);
  if (!window) return 0;
  uint32_t wid = 0;
  if (error == kAXErrorSuccess && CFGetTypeID(window) == AXUIElementGetTypeID()
      && AXUIElementSetMessagingTimeout((AXUIElementRef)window, 0.008f) == kAXErrorSuccess) {
    _AXUIElementGetWindow(window, &wid);
  }
  CFRelease(window);
  return wid;
}
