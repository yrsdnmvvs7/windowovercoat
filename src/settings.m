#import <Cocoa/Cocoa.h>
#import <ServiceManagement/ServiceManagement.h>
#import "misc/settings.h"
#include "misc/knit.h"
#include "misc/overcoat.h"
#include "misc/chart.h"
#include "misc/apps.h"
#include "misc/autoyarn.h"
#include <math.h>

extern void knit_apply(const char* arg);
extern void knit_apps_filter_changed(void);
#define L(key) NSLocalizedString(key,nil)

@interface OCFlippedView : NSView
@end
@implementation OCFlippedView
- (BOOL)isFlipped { return YES; }
@end

// Navigation item: fully self-drawn icon + label so the accent capsule can never
// cover the icon, with hover feedback like System Settings.
@interface OCNavItem : NSButton
@property(copy) NSString* icon;
@property(nonatomic) BOOL active;
@property(nonatomic) BOOL hovered;
@end
@implementation OCNavItem
- (BOOL)isFlipped { return YES; }
- (instancetype)initWithFrame:(NSRect)frame title:(NSString*)title icon:(NSString*)symbol target:(id)target action:(SEL)action {
  self=[super initWithFrame:frame];
  self.title=title;self.target=target;self.action=action;self.bordered=NO;self.icon=symbol;
  self.focusRingType=NSFocusRingTypeExterior;self.toolTip=title;self.active=NO;
  return self;
}
- (void)updateTrackingAreas {
  [super updateTrackingAreas];
  NSTrackingArea* area=[[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:self userInfo:nil];
  [self addTrackingArea:area];
}
- (void)mouseEntered:(NSEvent*)event {self.hovered=YES;[self setNeedsDisplay:YES];}
- (void)mouseExited:(NSEvent*)event {self.hovered=NO;[self setNeedsDisplay:YES];}
- (void)setActive:(BOOL)active {_active=active;[self setNeedsDisplay:YES];}
- (void)drawRect:(NSRect)dirty {
  NSRect r=NSInsetRect(self.bounds,3,4);
  NSBezierPath* p=[NSBezierPath bezierPathWithRoundedRect:r xRadius:8 yRadius:8];
  if(self.active) { [[NSColor controlAccentColor] setFill]; [p fill]; }
  else if(self.hovered||self.cell.isHighlighted) { [[NSColor.controlAccentColor colorWithAlphaComponent:.14] setFill]; [p fill]; }
  BOOL white=self.active;
  NSColor* fg=white?[NSColor whiteColor]:NSColor.labelColor;
  NSColor* ic=white?[NSColor whiteColor]:NSColor.secondaryLabelColor;
  NSImage* img=[NSImage imageWithSystemSymbolName:self.icon accessibilityDescription:nil];
  NSImage* rendered=[img imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPaletteColors:@[ic]]];
  [rendered drawInRect:NSMakeRect(16,(self.bounds.size.height-17)/2,17,17)];
  NSDictionary* attrs=@{NSFontAttributeName:[NSFont systemFontOfSize:13 weight:NSFontWeightMedium],NSForegroundColorAttributeName:fg};
  [self.title drawInRect:NSMakeRect(44,11,self.bounds.size.width-56,17) withAttributes:attrs];
}
@end

static NSTextField* label(NSString* text, CGFloat size, BOOL bold) {
  NSTextField* field=[NSTextField labelWithString:text];
  field.font=bold?[NSFont systemFontOfSize:size weight:NSFontWeightSemibold]:[NSFont systemFontOfSize:size];
  field.textColor=NSColor.labelColor; field.lineBreakMode=NSLineBreakByTruncatingTail;
  return field;
}
static void put(NSView* parent, NSView* child, NSRect rect) { child.frame=rect; [parent addSubview:child]; }

// Standard card surface that follows the system appearance.
static NSView* card(NSRect frame, NSView* content) {
  NSView* v=[[NSView alloc] initWithFrame:frame];
  v.wantsLayer=YES;v.layer.backgroundColor=NSColor.controlBackgroundColor.CGColor;
  v.layer.cornerRadius=12;v.layer.masksToBounds=YES;
  v.layer.borderWidth=1;v.layer.borderColor=NSColor.separatorColor.CGColor;
  content.frame=v.bounds;content.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;[v addSubview:content];
  return v;
}

// Grouped-card row metrics shared by every settings page, so row geometry is
// always consistent (title 6-28, detail 29-46, right control vertically
// centered in the 50pt row).
static const CGFloat OCH_ROW = 50;
static const CGFloat OCH_INSET = 16;

static NSTextField* groupTitle(NSString* text) { return label(text,13,YES); }
static NSTextField* groupDetail(NSString* text) {
  NSTextField* f=label(text,12,NO);f.textColor=NSColor.secondaryLabelColor;return f;
}
static void styleCard(NSView* v) {
  v.wantsLayer=YES;v.layer.backgroundColor=NSColor.controlBackgroundColor.CGColor;
  v.layer.cornerRadius=12;v.layer.masksToBounds=YES;
  v.layer.borderWidth=1;v.layer.borderColor=NSColor.separatorColor.CGColor;
}

// Every swatch uses the production renderer with an explicit material.
@interface OCSwatch : NSButton
@property int material;
@property int finish;
@property int chart;
@property int stitch;
@property uint32_t yarn;
@property float band;
@property BOOL selected;
@property(strong) NSMenuItem* choice;
@end
@implementation OCSwatch
- (BOOL)isFlipped { return YES; }
- (instancetype)initWithFrame:(NSRect)rect {
  if((self=[super initWithFrame:rect])) {self.bordered=NO;self.chart=-1;self.stitch=-1;self.yarn=0xff6498b7;self.band=7;self.focusRingType=NSFocusRingTypeExterior;}
  return self;
}
- (void)drawRect:(NSRect)dirty {
  NSRect r=NSInsetRect(self.bounds,1,1);
  NSBezierPath* card=[NSBezierPath bezierPathWithRoundedRect:r xRadius:12 yRadius:12];
  [(self.selected?[NSColor.controlAccentColor colorWithAlphaComponent:.15]:NSColor.controlBackgroundColor) setFill];[card fill];
  [(self.selected?NSColor.controlAccentColor:NSColor.separatorColor) setStroke];card.lineWidth=self.selected?2:1;[card stroke];
  CGContextRef c=NSGraphicsContext.currentContext.CGContext;
  CGRect win=CGRectMake(16,12,r.size.width-30,r.size.height-58);
  int old=g_knit_stitch;
  if(self.material==OC_KNIT && self.stitch>=0 && old!=self.stitch) {g_knit_stitch=self.stitch;knit_flush_cache();}
  overcoat_draw(c,win,4,self.band,self.material,self.finish,self.yarn,self.chart,0,1);
  if(g_knit_stitch!=old) {g_knit_stitch=old;knit_flush_cache();}
  [NSColor.controlBackgroundColor setFill];
  [[NSBezierPath bezierPathWithRoundedRect:win xRadius:4 yRadius:4] fill];
  NSMutableParagraphStyle* paragraph=[NSMutableParagraphStyle new];paragraph.lineBreakMode=NSLineBreakByTruncatingTail;
  NSDictionary* attrs=@{NSFontAttributeName:[NSFont systemFontOfSize:12],NSForegroundColorAttributeName:NSColor.labelColor,NSParagraphStyleAttributeName:paragraph};
  [self.title drawInRect:NSMakeRect(11,r.size.height-23,r.size.width-29,18) withAttributes:attrs];
  if(self.selected) [@"✓" drawAtPoint:NSMakePoint(r.size.width-20,r.size.height-23) withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:12],NSForegroundColorAttributeName:NSColor.controlAccentColor}];
}
@end

@interface OCPreview : OCFlippedView
@property(copy) NSString* appName;
@end
@implementation OCPreview
- (void)drawRect:(NSRect)dirty {
  CGContextRef c=NSGraphicsContext.currentContext.CGContext;
  float band=knit_current_width();float inset=MAX(32,band+8);
  CGRect win=CGRectInset(self.bounds,inset,inset);
  const struct app_rule* rule=knit_app_rule(self.appName.UTF8String);
  uint32_t yarn=rule?rule->color:knit_color_for_app(self.appName.UTF8String);
  int chart=knit_pattern_for_app(self.appName.UTF8String);
  if(knit_zigzag_active()) {
    pid_t pid=0;
    for(NSRunningApplication* app in NSWorkspace.sharedWorkspace.runningApplications)
      if([app.localizedName isEqualToString:self.appName]) {pid=app.processIdentifier;break;}
    knit_zigzag_yarn(self.appName.UTF8String,pid,&yarn,&chart);
  }
  overcoat_draw(c,win,9,band,g_overcoat_material,g_overcoat_finishes[g_overcoat_material],yarn,chart,0,1);
  [NSColor.controlBackgroundColor setFill];[[NSBezierPath bezierPathWithRoundedRect:win xRadius:9 yRadius:9] fill];
  [NSColor.separatorColor setFill];
  for(int i=0;i<3;i++) [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(win.origin.x+12+i*12,win.origin.y+13,6,6)] fill];
  [self.appName drawAtPoint:NSMakePoint(win.origin.x+64,win.origin.y+9) withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:11],NSForegroundColorAttributeName:NSColor.secondaryLabelColor}];
  [NSColor.separatorColor setFill];
  NSRectFill(NSMakeRect(win.origin.x+12,win.origin.y+38,win.size.width*.23,MAX(12,win.size.height-52)));
  for(int i=0;i<3;i++) NSRectFill(NSMakeRect(win.origin.x+win.size.width*.32,win.origin.y+41+i*19,win.size.width*.60,6));
}
@end

@interface OCSettings : NSWindowController <NSWindowDelegate>
@property(strong) OCFlippedView* root;
@property(strong) NSScrollView* scroll;
@property(strong) OCFlippedView* page;
@property(strong) NSMutableArray<OCSwatch*>* cards;
@property(strong) NSMutableArray<OCNavItem*>* navigation;
@property(strong) NSTextField* status;
@property(strong) NSTextField* widthValue;
@property(strong) OCPreview* preview;
@property(strong) NSSwitch* pause;
@property(strong) NSSlider* widthSlider;
@property(strong) NSPopUpButton* gauge;
@property(strong) NSTimer* widthTimer;
@property float pendingWidth;
@property NSInteger section;
@property(copy) NSString* sampleApp;
@property(strong) NSMenu* choices;
@end
static OCSettings* settingsController;

@implementation OCSettings
- (instancetype)init {
  NSWindow* w=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1020,680) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
  if((self=[super initWithWindow:w])) {
    w.title=L(@"Settings"); w.releasedWhenClosed=NO;
    w.minSize=NSMakeSize(940,600);w.delegate=self;
    self.sampleApp=@"Finder";self.cards=[NSMutableArray array];self.navigation=[NSMutableArray array];
    self.root=[[OCFlippedView alloc] initWithFrame:NSMakeRect(0,0,1020,652)];
    self.root.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;w.contentView=self.root;
    OCFlippedView* sidebar=[[OCFlippedView alloc] initWithFrame:NSMakeRect(0,0,190,652)];
    sidebar.autoresizingMask=NSViewHeightSizable;[self.root addSubview:sidebar];
    NSArray* names=@[L(@"Coat Styles"),L(@"General")];
    NSArray* icons=@[@"square.on.square",@"slider.horizontal.3"];
    for(int i=0;i<2;i++){
      OCNavItem* b=[[OCNavItem alloc] initWithFrame:NSMakeRect(10,14+i*44,170,38) title:names[i] icon:icons[i] target:self action:@selector(navigate:)];
      b.tag=i;[sidebar addSubview:b];[self.navigation addObject:b];
    }
    // Sidebar footer: app icon, name and version, pinned to the bottom.
    OCFlippedView* footer=[[OCFlippedView alloc] initWithFrame:NSMakeRect(0,sidebar.bounds.size.height-58,190,58)];
    footer.autoresizingMask=NSViewMinYMargin;
    NSImage* appIcon=[NSImage imageNamed:@"AppIcon"];
    if(!appIcon) appIcon=[NSImage imageNamed:NSImageNameApplicationIcon];
    NSImageView* iconView=[[NSImageView alloc] initWithFrame:NSMakeRect(18,6,22,22)];
    iconView.image=appIcon;iconView.imageScaling=NSImageScaleProportionallyUpOrDown;
    iconView.wantsLayer=YES;iconView.layer.cornerRadius=5;iconView.layer.masksToBounds=YES;
    [footer addSubview:iconView];
    NSTextField* appName=label(L(@"Window Overcoat"),12,YES);
    appName.frame=NSMakeRect(46,5,132,18);[footer addSubview:appName];
    NSTextField* version=label([NSString stringWithFormat:L(@"Version %@"),NSBundle.mainBundle.infoDictionary[@"CFBundleShortVersionString"]],11,NO);
    version.textColor=NSColor.secondaryLabelColor;
    version.frame=NSMakeRect(46,25,132,16);[footer addSubview:version];
    footer.toolTip=L(@"About Window Overcoat");
    [footer addGestureRecognizer:[[NSClickGestureRecognizer alloc] initWithTarget:self action:@selector(about:)]];
    [sidebar addSubview:footer];
    NSBox* divider=[[NSBox alloc] initWithFrame:NSMakeRect(190,0,1,652)];
    divider.boxType=NSBoxSeparator;divider.autoresizingMask=NSViewHeightSizable;[self.root addSubview:divider];
    self.scroll=[[NSScrollView alloc] initWithFrame:NSMakeRect(191,0,829,652)];
    self.scroll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;self.scroll.hasVerticalScroller=YES;self.scroll.drawsBackground=NO;
    [self.root addSubview:self.scroll];[w center];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationChanged:) name:NSApplicationDidBecomeActiveNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(settingsWindowBecameKey:) name:NSWindowDidBecomeKeyNotification object:w];
    [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self selector:@selector(applicationChanged:) name:NSWorkspaceDidLaunchApplicationNotification object:nil];
    [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self selector:@selector(applicationChanged:) name:NSWorkspaceDidTerminateApplicationNotification object:nil];
  }
  return self;
}
- (void)navigate:(OCNavItem*)sender {self.section=sender.tag;[self buildPage];}
- (void)applicationChanged:(NSNotification*)note {if(self.window.visible && self.section!=0)[self buildPage];else [self sync];}
- (void)settingsWindowBecameKey:(NSNotification*)note {[self sync];}
- (void)about:(id)sender {[NSApp activateIgnoringOtherApps:YES];[NSApp orderFrontStandardAboutPanel:nil];}
- (void)windowDidResize:(NSNotification*)note {[self buildPage];}
- (BOOL)windowShouldClose:(NSWindow*)sender { [self flushWidth]; [self.window orderOut:nil]; [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory]; return NO; }
- (void)show {
  self.section=0;[self buildPage];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  [self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
}
- (NSButton*)button:(NSString*)title action:(SEL)action frame:(NSRect)frame parent:(NSView*)parent {
  NSButton* b=[NSButton buttonWithTitle:title target:self action:action];b.bezelStyle=NSBezelStyleRounded;put(parent,b,frame);return b;
}
- (OCSwatch*)swatch:(NSString*)title frame:(NSRect)frame material:(int)m finish:(int)f {
  OCSwatch* b=[[OCSwatch alloc] initWithFrame:frame];b.title=title;b.material=m;b.finish=f;b.band=g_overcoat_widths[m];b.target=self;[self.page addSubview:b];[self.cards addObject:b];return b;
}
- (void)buildPage {
  [self.cards removeAllObjects];self.preview=nil;self.gauge=nil;self.widthSlider=nil;self.widthValue=nil;
  CGFloat width=MAX(780,self.scroll.contentSize.width);
  CGFloat height=self.scroll.contentSize.height;
  self.page=[[OCFlippedView alloc] initWithFrame:NSMakeRect(0,0,width,height)];self.scroll.documentView=self.page;
  for(OCNavItem* b in self.navigation)b.active=b.tag==self.section;
  NSString* title=@[L(@"Coat Styles"),L(@"General")][self.section];
  put(self.page,label(title,22,YES),NSMakeRect(28,16,width-220,32));
  NSTextField* pauseLabel=label(L(@"Overcoats"),12,NO);
  pauseLabel.textColor=NSColor.secondaryLabelColor;pauseLabel.alignment=NSTextAlignmentRight;
  put(self.page,pauseLabel,NSMakeRect(width-190,19,120,20));
  NSSwitch* pauseSwitch=[[NSSwitch alloc] initWithFrame:NSMakeRect(width-62,17,40,22)];
  pauseSwitch.target=self;pauseSwitch.action=@selector(toggle:);[self.page addSubview:pauseSwitch];
  self.pause=pauseSwitch;
  put(self.page,label(L(@"Changes are saved automatically."),12,NO),NSMakeRect(28,54,width-220,20));
  if(self.section==0)[self buildStyles:width];else [self buildGeneral:width];
  self.status=label(@"",12,NO);self.status.textColor=NSColor.secondaryLabelColor;
  put(self.page,self.status,NSMakeRect(28,self.page.frame.size.height-36,width-56,24));[self sync];
}
- (void)buildStyles:(CGFloat)width {
  CGFloat gap=10,cardWidth=(width-56-gap*5)/6;
  for(int i=0;i<OC_MATERIAL_COUNT;i++) {
    OCSwatch* b=[self swatch:L(@(g_overcoat_styles[i].title)) frame:NSMakeRect(28+i*(cardWidth+gap),92,cardWidth,104) material:i finish:0];b.tag=i;b.action=@selector(material:);b.chart=knit_chart_index("zigzag");
  }
  CGFloat left=28,leftWidth=(width-80)*.55,right=left+leftWidth+24,rightWidth=width-right-28;
  CGFloat y=226, bottom=660;
  self.choices=overcoat_style_menu();
  if(g_overcoat_material==OC_KNIT) {
    NSMenu* patterns=[self.choices itemWithTitle:L(@"Pattern")].submenu;
    NSMutableArray<NSMenuItem*>* choices=[NSMutableArray array];
    for(NSMenuItem* item in patterns.itemArray) {
      if(item.submenu) for(NSMenuItem* child in item.submenu.itemArray){if(!child.separatorItem)[choices addObject:child];}
      else if(!item.separatorItem) [choices addObject:item];
    }
    put(self.page,label(L(@"Knitted patterns"),15,YES),NSMakeRect(left,y,leftWidth,25));y+=36;
    CGFloat cw=(leftWidth-12)/2;
    for(NSInteger i=0;i<choices.count;i++) {
      NSMenuItem* item=choices[i];OCSwatch* b=[self swatch:item.title frame:NSMakeRect(left+(i%2)*(cw+12),y+(i/2)*105,cw,95) material:OC_KNIT finish:0];b.choice=item;b.action=@selector(pattern:);
      NSString* arg=item.representedObject;
      if([arg isEqualToString:@"chart=by-app"]) {const struct app_rule* r=knit_app_rule("Finder");if(r){b.yarn=r->color;b.chart=knit_chart_index(r->chart);}}
      else if([arg hasPrefix:@"chart="])b.chart=knit_chart_index([arg substringFromIndex:6].UTF8String);
      else if([arg hasPrefix:@"yarn="])b.stitch=[arg isEqualToString:@"yarn=rib"]?KNIT_RIB:KNIT_STOCKINETTE;
      b.toolTip=item.toolTip;b.selected=item.state==NSControlStateValueOn;
    }
    bottom=MAX(bottom,y+((choices.count+1)/2)*105+60);
  } else {
    const struct overcoat_style* s=&g_overcoat_styles[g_overcoat_material];
    put(self.page,label(L(@(s->title)),16,YES),NSMakeRect(left,y,leftWidth,26));y+=32;
    NSArray* descriptions=@[@"",@"Soft highlights, a clear glass finish.",@"A quiet outline that keeps content in focus.",@"Gentle colors, without animation or flashing.",@"Subtle brushing and softly beveled edges.",@"Fine grain and a stitched leather edge."];
    NSTextField* description=label(L(descriptions[g_overcoat_material]),12,NO);description.lineBreakMode=NSLineBreakByWordWrapping;description.maximumNumberOfLines=2;put(self.page,description,NSMakeRect(left,y,leftWidth,38));y+=50;
    CGFloat cw=(leftWidth-12)/2;
    for(int i=0;i<OC_FINISH_COUNT;i++){OCSwatch* b=[self swatch:L(@(s->finishes[i])) frame:NSMakeRect(left+(i%2)*(cw+12),y+(i/2)*125,cw,112) material:g_overcoat_material finish:i];b.tag=i;b.action=@selector(finish:);}
  }
  OCFlippedView* panel=[[OCFlippedView alloc] initWithFrame:NSMakeRect(0,0,rightWidth,394)];
  NSView* surface=card(NSMakeRect(right,226,rightWidth,394),panel);[self.page addSubview:surface];
  put(panel,label(L(@"Try it on"),14,YES),NSMakeRect(18,16,130,24));
  NSPopUpButton* sample=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(rightWidth-136,13,118,28) pullsDown:NO];[sample addItemsWithTitles:@[@"Finder",@"Claude",@"Spotify",@"Notion"]];[sample selectItemWithTitle:self.sampleApp];sample.target=self;sample.action=@selector(sample:);[panel addSubview:sample];
  self.preview=[[OCPreview alloc] initWithFrame:NSMakeRect(6,50,rightWidth-12,215)];self.preview.appName=self.sampleApp;[panel addSubview:self.preview];
  put(panel,label(L(@"Border Width"),12,YES),NSMakeRect(18,276,150,20));
  self.widthValue=label(@"",12,NO);self.widthValue.alignment=NSTextAlignmentRight;put(panel,self.widthValue,NSMakeRect(rightWidth-110,276,92,20));
  self.widthSlider=[NSSlider sliderWithValue:knit_current_width() minValue:g_overcoat_material==OC_KNIT?2:1 maxValue:MAX(28,knit_current_width()) target:self action:@selector(width:)];self.widthSlider.continuous=YES;put(panel,self.widthSlider,NSMakeRect(18,301,rightWidth-36,25));
  if(g_overcoat_material==OC_KNIT){
    put(panel,label(L(@"Stitch Size"),12,YES),NSMakeRect(18,341,120,23));
    self.gauge=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(134,336,rightWidth-152,30) pullsDown:NO];self.gauge.menu=[[self.choices itemWithTitle:L(@"Stitch Size")].submenu copy];self.gauge.target=self;self.gauge.action=@selector(gauge:);[panel addSubview:self.gauge];
    put(panel,label(L(@"Options adapt to the selected pattern."),11,NO),NSMakeRect(18,373,rightWidth-36,20));
  }
  self.page.frame=NSMakeRect(0,0,width,bottom);
}
- (void)buildGeneral:(CGFloat)width {
  CGFloat x=28,w=width-56,y=96;
  // Launch group card.
  NSView* launch=[[NSView alloc] initWithFrame:NSMakeRect(x,y,w,OCH_ROW)];
  styleCard(launch);
  NSTextField* t1=groupTitle(L(@"Open at Login"));
  t1.frame=NSMakeRect(OCH_INSET,6,w-2*OCH_INSET-60,22);[launch addSubview:t1];
  NSTextField* d1=groupDetail(L(@"Start quietly in the menu bar."));
  d1.frame=NSMakeRect(OCH_INSET,29,w-2*OCH_INSET,17);[launch addSubview:d1];
  NSSwitch* loginSwitch=[[NSSwitch alloc] initWithFrame:NSMakeRect(w-58,13,40,22)];
  loginSwitch.state=SMAppService.mainAppService.status==SMAppServiceStatusEnabled?NSControlStateValueOn:NSControlStateValueOff;
  loginSwitch.target=self;loginSwitch.action=@selector(login:);[launch addSubview:loginSwitch];
  [self.page addSubview:launch];y+=OCH_ROW+14;
  // Accessibility permission group card.
  BOOL trusted=AXIsProcessTrusted();
  NSView* access=[[NSView alloc] initWithFrame:NSMakeRect(x,y,w,OCH_ROW*2)];
  styleCard(access);
  NSTextField* t2=groupTitle(L(@"Accessibility Permission"));
  t2.frame=NSMakeRect(OCH_INSET,6,w-2*OCH_INSET-110,22);[access addSubview:t2];
  NSTextField* granted=label(trusted?L(@"Granted"):L(@"Needed"),13,NO);
  granted.textColor=trusted?NSColor.systemGreenColor:NSColor.systemOrangeColor;
  granted.alignment=NSTextAlignmentRight;
  granted.frame=NSMakeRect(w-110,6,94,22);[access addSubview:granted];
  NSTextField* d2=groupDetail(L(@"Permission is needed to follow window focus."));
  d2.frame=NSMakeRect(OCH_INSET,29,w-2*OCH_INSET,17);[access addSubview:d2];
  if(!trusted) {
    [self button:L(@"Open System Settings") action:@selector(permissions:) frame:NSMakeRect(OCH_INSET,57,210,28) parent:access];
  }
  [self.page addSubview:access];y+=OCH_ROW*2+14;
  // Quiet running note card.
  NSView* note=[[NSView alloc] initWithFrame:NSMakeRect(x,y,w,OCH_ROW)];
  styleCard(note);
  NSTextField* t3=groupTitle(L(@"Closing this window keeps overcoats running."));
  t3.frame=NSMakeRect(OCH_INSET,6,w-2*OCH_INSET,22);[note addSubview:t3];
  [self.page addSubview:note];y+=OCH_ROW;
  self.page.frame=NSMakeRect(0,0,width,y+40);
}
- (void)sync {
  if(!self.page)return;
  self.pause.state=g_knit_on?NSControlStateValueOn:NSControlStateValueOff;
  BOOL none=!knit_apps_on_by_default() && knit_app_exception_count()==0;
  NSString* status=!g_knit_on?L(@"Overcoats are paused"):(none?L(@"No apps are enabled"):L(@"Saved automatically"));
  if(!AXIsProcessTrusted())status=[status stringByAppendingFormat:@" · %@",L(@"Accessibility permission required")];
  self.status.stringValue=status;
  self.widthValue.stringValue=[NSString stringWithFormat:@"%g pt",knit_current_width()];
  self.widthSlider.doubleValue=knit_current_width();self.preview.needsDisplay=YES;
  for(OCSwatch* b in self.cards){if(b.action==@selector(material:))b.selected=b.material==g_overcoat_material;else if(b.action==@selector(finish:))b.selected=b.finish==g_overcoat_finishes[g_overcoat_material];b.needsDisplay=YES;}
  if(self.gauge)for(NSMenuItem* item in self.gauge.itemArray)if(item.state==NSControlStateValueOn){[self.gauge selectItem:item];break;}
}
- (void)material:(OCSwatch*)b {[self flushWidth];overcoat_choose_material((int)b.tag);[self buildPage];}
- (void)finish:(OCSwatch*)b {overcoat_choose_finish((int)b.tag);}
- (void)pattern:(OCSwatch*)b {[NSApp sendAction:b.choice.action to:b.choice.target from:b.choice];[self buildPage];}
- (void)gauge:(NSPopUpButton*)b {NSMenuItem* item=b.selectedItem;[NSApp sendAction:item.action to:item.target from:item];[self buildPage];}
- (void)sample:(NSPopUpButton*)b {self.sampleApp=b.titleOfSelectedItem;self.preview.appName=self.sampleApp;self.preview.needsDisplay=YES;}
- (void)toggle:(id)sender {overcoat_toggle();[self sync];}
- (void)width:(NSSlider*)b {
  self.pendingWidth=roundf(b.floatValue);self.widthValue.stringValue=[NSString stringWithFormat:@"%g pt",self.pendingWidth];
  if(!self.widthTimer){__weak OCSettings* weak=self;self.widthTimer=[NSTimer timerWithTimeInterval:.04 repeats:NO block:^(NSTimer* timer){[weak flushWidth];}];[NSRunLoop.mainRunLoop addTimer:self.widthTimer forMode:NSRunLoopCommonModes];}
}
- (void)flushWidth {if(!self.widthTimer)return;[self.widthTimer invalidate];self.widthTimer=nil;char arg[64];snprintf(arg,sizeof arg,"width=%g",self.pendingWidth);knit_apply(arg);overcoat_save_preferences();}
- (void)permissions:(id)sender { [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"]]; }
- (void)login:(NSButton*)b {
  NSError* error=nil;BOOL ok=b.state==NSControlStateValueOn?[SMAppService.mainAppService registerAndReturnError:&error]:[SMAppService.mainAppService unregisterAndReturnError:&error];
  if(!ok){NSAlert* alert=[NSAlert new];alert.messageText=L(@"Could not update login settings");alert.informativeText=error.localizedDescription?:L(@"Please try again in System Settings.");[alert beginSheetModalForWindow:self.window completionHandler:nil];}
  else if(SMAppService.mainAppService.status==SMAppServiceStatusRequiresApproval)[SMAppService openSystemSettingsLoginItems];[self buildPage];
}
@end

void overcoat_show_settings(void) {if(!settingsController)settingsController=[OCSettings new];[settingsController show];}
void overcoat_refresh_settings(void) {[settingsController sync];}
