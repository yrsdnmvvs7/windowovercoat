#pragma once
#import <Cocoa/Cocoa.h>
void overcoat_show_settings(void);
void overcoat_refresh_settings(void);
void overcoat_save_preferences(void);
NSMenu* overcoat_style_menu(void);
NSMenu* overcoat_apps_menu(void);
void overcoat_toggle(void);
void overcoat_choose_material(int material);
void overcoat_choose_finish(int finish);
