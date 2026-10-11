#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <stdint.h>

enum overcoat_material { OC_KNIT, OC_GLASS, OC_LINE, OC_AURORA, OC_METAL, OC_LEATHER, OC_MATERIAL_COUNT };
#define OC_FINISH_COUNT 3
struct overcoat_style { const char* key; const char* title; float width; const char* finishes[OC_FINISH_COUNT]; uint32_t colors[OC_FINISH_COUNT][2]; };
extern const struct overcoat_style g_overcoat_styles[OC_MATERIAL_COUNT];
extern int g_overcoat_material;
extern int g_overcoat_finishes[OC_MATERIAL_COUNT];
extern float g_overcoat_widths[OC_MATERIAL_COUNT];
bool overcoat_select(const char* name);
bool overcoat_finish(int finish);
// Shared by the real border windows and settings previews. Material is explicit
// so rendering a thumbnail cannot change the running application's selection.
void overcoat_draw(CGContextRef ctx, CGRect win, float radius, float band,
                   int material, int finish, uint32_t yarn, int chart, float dim, float tuck);
