#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>
struct table;

void windows_reconcile_start(void);
// A complete front-to-back onscreen snapshot. NULL means the query failed.
void windows_reconcile_snapshot(struct table* windows, CFArrayRef snapshot);
