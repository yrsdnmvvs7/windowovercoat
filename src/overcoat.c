#include "misc/overcoat.h"
#include "misc/knit.h"
#include <math.h>
#include <string.h>

int g_overcoat_material = OC_KNIT;
int g_overcoat_finishes[OC_MATERIAL_COUNT];
float g_overcoat_widths[OC_MATERIAL_COUNT] = {12, 10, 2, 8, 8, 14};
const struct overcoat_style g_overcoat_styles[] = {
  {"knit", "Knitted Sweater", 12, {"", "", ""}, {{0}}},
  {"glass", "Clear Glass", 10, {"Crystal", "Glacier", "Smoke"}, {{0xffafc5dc,0xfff5fbff},{0xff88b6d8,0xffe3f5ff},{0xff8596a9,0xffe5edf5}}},
  {"line", "Minimal Line", 2, {"Graphite", "Mist Blue", "Pearl"}, {{0xff737c8b,0xff737c8b},{0xff719bbd,0xff719bbd},{0xffc1cbd8,0xffc1cbd8}}},
  {"aurora", "Soft Gradient", 8, {"Sky", "Dusk", "Sea Mist"}, {{0xff94c7e9,0xffc7b9e8},{0xffe6b3c0,0xffc9b3e7},{0xff9bcfc2,0xffa3c4e6}}},
  {"metal", "Brushed Metal", 8, {"Silver", "Titanium", "Champagne"}, {{0xffa2acb8,0xffedf1f7},{0xff747e89,0xffc5cdd6},{0xffb5aa96,0xffeae3d6}}},
  {"leather", "Soft Leather", 14, {"Mocha", "Chestnut", "Ink"}, {{0xff96725e,0xffb08d76},{0xff755044,0xff9d7565},{0xff3c4149,0xff626b77}}}
};
bool overcoat_select(const char* name) {
  if (!name) return false;
  for (int i=0;i<OC_MATERIAL_COUNT;i++) if (!strcmp(name,g_overcoat_styles[i].key)) { g_overcoat_material=i; return true; }
  return false;
}
bool overcoat_finish(int finish) {
  if (finish<0 || finish>=OC_FINISH_COUNT) return false;
  g_overcoat_finishes[g_overcoat_material]=finish; return true;
}
static void rounded(CGContextRef c, CGRect r, float radius) {
  CGPathRef p=CGPathCreateWithRoundedRect(r, radius, radius, NULL);
  CGContextAddPath(c,p); CGPathRelease(p);
}
static void gradient(CGContextRef c, uint32_t a, uint32_t b, CGPoint from, CGPoint to, bool reflective) {
  uint32_t colors[5]={b,a,b,a,b};
  if (!reflective) { colors[0]=a; colors[1]=b; }
  int count=reflective?5:2; CGFloat components[20];
  for(int i=0;i<count;i++) { float alpha,r,g,blue; colors_from_hex(colors[i],&alpha,&r,&g,&blue); components[i*4]=r;components[i*4+1]=g;components[i*4+2]=blue;components[i*4+3]=alpha; }
  CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGGradientRef g=CGGradientCreateWithColorComponents(space,components,NULL,count);
  if(g) { CGContextDrawLinearGradient(c,g,from,to,kCGGradientDrawsBeforeStartLocation|kCGGradientDrawsAfterEndLocation);CGGradientRelease(g); }
  CGColorSpaceRelease(space);
}
void overcoat_draw(CGContextRef c, CGRect win, float radius, float band,
                   int material, int finish, uint32_t yarn, int chart, float dim, float tuck) {
  if(material==OC_KNIT) { knit_draw(c,win,radius,band,yarn,chart,dim,tuck);return; }
  if(!c || material<0 || material>=OC_MATERIAL_COUNT || finish<0 || finish>=OC_FINISH_COUNT
     || !isfinite(band) || !isfinite(radius) || !isfinite(tuck) || !isfinite(dim)
     || !isfinite(win.origin.x) || !isfinite(win.origin.y) || !isfinite(win.size.width) || !isfinite(win.size.height)
     || win.size.width<=0 || win.size.height<=0 || band<=0) return;
  band=fminf(60,fmaxf(1,band)); radius=fminf(fmaxf(0,radius),fminf(win.size.width,win.size.height)/2);
  tuck=fmaxf(0,fminf(tuck,fmaxf(0,fminf(win.size.width,win.size.height)/2-1)));
  CGRect outer=CGRectInset(win,-band,-band),inner=CGRectInset(win,tuck,tuck);
  CGContextSaveGState(c);
  rounded(c,outer,radius+band);rounded(c,inner,fmaxf(0,radius-tuck));CGContextEOClip(c);
  const uint32_t* colors=g_overcoat_styles[material].colors[finish];
  CGPoint start=outer.origin,end=CGPointMake(CGRectGetMaxX(outer),CGRectGetMaxY(outer));
  if(material==OC_METAL) end.x=start.x;
  gradient(c,colors[0],colors[1],start,end,material==OC_GLASS||material==OC_METAL);
  if(material==OC_METAL) {
    CGContextSetLineWidth(c,.35); CGContextSetRGBStrokeColor(c,1,1,1,.16);
    for(float y=CGRectGetMinY(outer);y<CGRectGetMaxY(outer);y+=1.5f) { CGContextMoveToPoint(c,CGRectGetMinX(outer),y);CGContextAddLineToPoint(c,CGRectGetMaxX(outer),y); }
    CGContextStrokePath(c);
  }
  if(material==OC_LEATHER) {
    // Draw grain only in the four narrow edge strips; never iterate the entire
    // window area on a 6K display. Deterministic positions prevent shimmer.
    CGContextSetRGBFillColor(c,0,0,0,.08);
    for(float y=CGRectGetMinY(outer);y<CGRectGetMaxY(outer);y+=3) {
      bool edge=y<CGRectGetMinY(win)+band || y>CGRectGetMaxY(win)-band;
      for(float x=CGRectGetMinX(outer);x<CGRectGetMaxX(outer);x+=3) {
        if(!edge && x>CGRectGetMinX(win)+band && x<CGRectGetMaxX(win)-band) {x=CGRectGetMaxX(win)-band;continue;}
        CGContextFillEllipseInRect(c,CGRectMake(x+fmodf(fabsf(y),2),y,.65,1.1));
      }
    }
    CGFloat dash[]={2,3};CGContextSetLineDash(c,0,dash,2);CGContextSetLineWidth(c,.7);
    CGContextSetRGBStrokeColor(c,1,.91,.8,.55);
    rounded(c,CGRectInset(win,-band*.55f,-band*.55f),radius+band*.55f);CGContextStrokePath(c);CGContextSetLineDash(c,0,NULL,0);
  }
  if(material!=OC_LINE) {
    CGContextSetRGBStrokeColor(c,1,1,1,material==OC_GLASS?.9:.5);CGContextSetLineWidth(c,.8);
    rounded(c,CGRectInset(outer,.5,.5),radius+band-.5);CGContextStrokePath(c);
    CGContextSetRGBStrokeColor(c,.2,.3,.4,.18);CGContextSetLineWidth(c,.6);
    rounded(c,CGRectInset(win,-.4,-.4),radius+.4);CGContextStrokePath(c);
  }
  if(dim>0){CGContextSetRGBFillColor(c,0,0,0,fminf(1,dim));CGContextFillRect(c,outer);}
  CGContextRestoreGState(c);
}
