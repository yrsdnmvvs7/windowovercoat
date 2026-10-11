// Exercise the real focus helper without sending messages to another app.
#include <ApplicationServices/ApplicationServices.h>
#include <assert.h>
#include <stdio.h>
#include "../src/misc/extern.h"

static Boolean mock_trusted(void);
static Boolean mock_prompt(CFDictionaryRef options);
static OSStatus mock_front(ProcessSerialNumber* psn);
static CGError mock_connection(int cid, ProcessSerialNumber* psn, int* out);
static CGError mock_pid(int cid, pid_t* out);
static AXUIElementRef mock_create(pid_t pid);
static AXError mock_timeout(AXUIElementRef element, float seconds);
static AXError mock_attribute(AXUIElementRef app, CFStringRef key, CFTypeRef* out);
static CFTypeID mock_ax_type(void);
static void mock_window(CFTypeRef window, uint32_t* wid);
static void mock_release(CFTypeRef object);

#define AXIsProcessTrusted mock_trusted
#define AXIsProcessTrustedWithOptions mock_prompt
#define _SLPSGetFrontProcess mock_front
#define SLSGetConnectionIDForPSN mock_connection
#define SLSConnectionGetPID mock_pid
#define AXUIElementCreateApplication mock_create
#define AXUIElementSetMessagingTimeout mock_timeout
#define AXUIElementCopyAttributeValue mock_attribute
#define AXUIElementGetTypeID mock_ax_type
#define _AXUIElementGetWindow mock_window
#define CFRelease mock_release
#include "../src/misc/ax.h"
#undef CFRelease

enum fault {
  NONE, PERMISSION_LOST, FRONT_MISSING, CONNECTION_MISSING, PID_FAILED,
  PID_MISSING, APP_MISSING, APP_TIMEOUT_FAILED, ATTRIBUTE_FAILED,
  ATTRIBUTE_FAILED_WITH_VALUE, WINDOW_MISSING, WRONG_TYPE,
  WINDOW_TIMEOUT_FAILED, WINDOW_ID_MISSING
};
static enum fault fault;
static CFTypeRef app_object, window_object;
static int app_created, app_released, window_created, window_released;
static int attributes, window_lookups, prompts, timeout_calls, front_calls;
static bool app_bounded, window_bounded;

static Boolean mock_trusted(void) { return fault != PERMISSION_LOST; }
static Boolean mock_prompt(CFDictionaryRef options) {
  (void)options;
  prompts++;
  return false;
}
static OSStatus mock_front(ProcessSerialNumber* psn) {
  front_calls++;
  psn->lowLongOfPSN = 123;
  return fault == FRONT_MISSING ? procNotFound : noErr;
}
static CGError mock_connection(int cid, ProcessSerialNumber* psn, int* out) {
  assert(cid == 1 && psn->lowLongOfPSN == 123);
  if (fault == CONNECTION_MISSING) return kCGErrorFailure;
  *out = 2;
  return kCGErrorSuccess;
}
static CGError mock_pid(int cid, pid_t* out) {
  assert(cid == 2);
  if (fault == PID_FAILED) return kCGErrorFailure;
  if (fault != PID_MISSING) *out = 321;
  return kCGErrorSuccess;
}
static AXUIElementRef mock_create(pid_t pid) {
  assert(pid == 321);
  if (fault == APP_MISSING) return NULL;
  app_object = CFDataCreate(NULL, (const UInt8*)"app", 3);
  assert(app_object);
  app_created++;
  return (AXUIElementRef)app_object;
}
static AXError mock_timeout(AXUIElementRef element, float seconds) {
  // Both foreign calls must receive a small positive messaging budget first.
  assert(seconds > 0 && seconds <= 0.010f);
  timeout_calls++;
  if ((CFTypeRef)element == app_object) {
    assert(!attributes);
    if (fault == APP_TIMEOUT_FAILED) return kAXErrorCannotComplete;
    app_bounded = true;
  } else {
    assert((CFTypeRef)element == window_object && !window_lookups);
    if (fault == WINDOW_TIMEOUT_FAILED) return kAXErrorCannotComplete;
    window_bounded = true;
  }
  return kAXErrorSuccess;
}
static AXError mock_attribute(AXUIElementRef app, CFStringRef key, CFTypeRef* out) {
  assert((CFTypeRef)app == app_object && app_bounded);
  assert(CFEqual(key, kAXFocusedWindowAttribute));
  attributes++;
  if (fault == ATTRIBUTE_FAILED) return kAXErrorCannotComplete;
  if (fault == WINDOW_MISSING) return kAXErrorSuccess;
  window_object = fault == WRONG_TYPE
      ? (CFTypeRef)CFStringCreateWithCString(NULL, "wrong type", kCFStringEncodingUTF8)
      : (CFTypeRef)CFDataCreate(NULL, (const UInt8*)"window", 6);
  assert(window_object);
  window_created++;
  *out = window_object;
  return fault == ATTRIBUTE_FAILED_WITH_VALUE ? kAXErrorCannotComplete : kAXErrorSuccess;
}
static CFTypeID mock_ax_type(void) { return CFDataGetTypeID(); }
static void mock_window(CFTypeRef window, uint32_t* wid) {
  assert(window == window_object && window_bounded);
  window_lookups++;
  if (fault != WINDOW_ID_MISSING) *wid = 42;
}
static void mock_release(CFTypeRef object) {
  if (object == app_object) {
    app_released++;
    app_object = NULL;
  } else {
    assert(object == window_object);
    window_released++;
    window_object = NULL;
  }
  CFRelease(object);
}

static void check(enum fault scenario, uint32_t expected, int expected_attributes,
                  int expected_lookups, int expected_timeouts) {
  assert(!app_object && !window_object);
  fault = scenario;
  app_created = app_released = window_created = window_released = 0;
  attributes = window_lookups = prompts = timeout_calls = front_calls = 0;
  app_bounded = window_bounded = false;
  assert(ax_get_front_window(1) == expected);
  assert(attributes == expected_attributes && window_lookups == expected_lookups);
  assert(timeout_calls == expected_timeouts && prompts == 0);
  assert(app_created == app_released && window_created == window_released);
  assert(!app_object && !window_object);
  if (scenario == PERMISSION_LOST) assert(front_calls == 0);
}

int main(void) {
  check(NONE, 42, 1, 1, 2);
  // Revocation must be checked even after an earlier successful focus lookup.
  check(PERMISSION_LOST, 0, 0, 0, 0);
  check(FRONT_MISSING, 0, 0, 0, 0);
  check(CONNECTION_MISSING, 0, 0, 0, 0);
  check(PID_FAILED, 0, 0, 0, 0);
  check(PID_MISSING, 0, 0, 0, 0);
  check(APP_MISSING, 0, 0, 0, 0);
  check(APP_TIMEOUT_FAILED, 0, 0, 0, 1);
  check(ATTRIBUTE_FAILED, 0, 1, 0, 1);
  check(ATTRIBUTE_FAILED_WITH_VALUE, 0, 1, 0, 1);
  check(WINDOW_MISSING, 0, 1, 0, 1);
  check(WRONG_TYPE, 0, 1, 0, 1);
  check(WINDOW_TIMEOUT_FAILED, 0, 1, 0, 2);
  check(WINDOW_ID_MISSING, 0, 1, 1, 2);
  check(NONE, 42, 1, 1, 2);
  puts("PASS: bounded AX focus, revoked permission, failed/missing/wrong-type replies, reference cleanup");
  return 0;
}
