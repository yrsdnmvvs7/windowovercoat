// Exercise the production event router without querying or modifying real windows.
#define SLSGetWindowOwner mock_owner
#define SLSConnectionGetPID mock_pid
#include "../src/events.c"

struct table g_windows;
pid_t g_pid = 100;
static int owner_queries, pid_queries, moves, resizes, hides, unhides, closes, updates;
CGError mock_owner(int cid, uint32_t wid, int* owner) {
  owner_queries++; *owner = (wid == 2 ? 100 : 200); return 0;
}
CGError mock_pid(int cid, pid_t* pid) { pid_queries++; *pid = cid; return 0; }
static unsigned long hash(void* p) { return *(uint32_t*)p; }
static int equal(void* a, void* b) { return *(uint32_t*)a == *(uint32_t*)b; }
void windows_window_move(struct table* t, uint32_t wid) { moves++; }
void windows_window_resize(struct table* t, uint32_t wid) { resizes++; }
void windows_window_hide(struct table* t, uint32_t wid) { hides++; }
void windows_window_unhide(struct table* t, uint32_t wid) { unhides++; }
void windows_window_update(struct table* t, uint32_t wid) { updates++; }
bool windows_window_destroy(struct table* t, uint32_t wid, uint64_t sid) {
  closes++; table_remove(t, &wid); return true;
}
bool windows_window_create(struct table* t, uint32_t wid, uint64_t sid) { return false; }
void windows_reorder_all(struct table* t) {}
void windows_determine_and_focus_active_window(struct table* t) {}
void windows_draw_borders_on_current_spaces(struct table* t) {}
static void send(uint32_t event, uint32_t wid) { window_modify_handler(event, &wid, sizeof(wid), 1); }

int main(void) {
  table_init(&g_windows, 16, hash, equal);
  uint32_t wid = 3;
  table_add(&g_windows, &wid, (void*)1);
  send(EVENT_WINDOW_MOVE, wid); send(EVENT_WINDOW_RESIZE, wid);
  assert(moves == 1 && resizes == 1 && owner_queries == 0 && pid_queries == 0);
  send(EVENT_WINDOW_MOVE, 9); send(EVENT_WINDOW_RESIZE, 2);
  assert(moves == 1 && resizes == 1 && owner_queries == 0);
  send(EVENT_WINDOW_REORDER, 2); // Our own overlay must not trigger repairs.
  assert(updates == 0 && owner_queries == 1 && pid_queries == 1);
  send(EVENT_WINDOW_REORDER, 9); // Untracked foreign windows still affect depth.
  assert(updates == 1 && owner_queries == 2);
  window_modify_handler(EVENT_WINDOW_MOVE, NULL, 4, 1);
  window_modify_handler(EVENT_WINDOW_MOVE, &wid, 1, 1);
  window_spawn_handler(EVENT_WINDOW_CREATE, NULL, 12, 1);
  struct window_spawn_data spawn = {1, 3};
  window_spawn_handler(EVENT_WINDOW_CREATE, &spawn, 8, 1);
  assert(moves == 1 && owner_queries == 2);
  send(EVENT_WINDOW_HIDE, wid); send(EVENT_WINDOW_UNHIDE, wid); send(EVENT_WINDOW_CLOSE, wid);
  assert(hides == 1 && unhides == 1 && closes == 1 && owner_queries == 2);
  send(EVENT_WINDOW_MOVE, wid); send(EVENT_WINDOW_RESIZE, wid);
  assert(moves == 1 && resizes == 1 && owner_queries == 2);
  table_add(&g_windows, &wid, (void*)1);
  spawn.sid = 0;
  window_spawn_handler(EVENT_WINDOW_DESTROY, &spawn, 12, 1);
  assert(closes == 1 && hides == 2 && table_find(&g_windows, &wid) && owner_queries == 2);
  table_free(&g_windows);
  puts("PASS: tracked events avoid owner queries, foreign depth events survive, closed targets and malformed payloads are ignored");
}
