#include "misc/overcoat.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t pixel(CGContextRef c,int x,int y) {
  size_t stride=CGBitmapContextGetBytesPerRow(c)/4;
  return ((uint32_t*)CGBitmapContextGetData(c))[y*stride+x];
}
int main(void) {
  CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef c=CGBitmapContextCreate(NULL,240,180,8,0,space,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Host);
  CGColorSpaceRelease(space);assert(c);
  uint64_t hashes[5*OC_FINISH_COUNT]={0};int count=0;
  for(int m=OC_GLASS;m<OC_MATERIAL_COUNT;m++)for(int f=0;f<OC_FINISH_COUNT;f++) {
    CGContextClearRect(c,CGRectMake(0,0,240,180));
    overcoat_draw(c,CGRectMake(30,30,180,120),12,12,m,f,0,-1,0,1);
    assert(pixel(c,120,90)==0); // Never cover the application's content.
    assert(pixel(c,2,2)==0);    // Never leak outside the outer ring.
    assert((pixel(c,120,22)>>24)>200); // Continuous, visible top edge.
    assert((pixel(c,22,90)>>24)>200);  // Continuous left edge.
    uint64_t hash=1469598103934665603ULL;
    for(int y=0;y<180;y++)for(int x=0;x<240;x++){hash^=pixel(c,x,y);hash*=1099511628211ULL;}
    for(int i=0;i<count;i++)assert(hashes[i]!=hash); // Every finish is distinct.
    hashes[count++]=hash;
    uint32_t bright=pixel(c,120,22);
    CGContextClearRect(c,CGRectMake(0,0,240,180));
    overcoat_draw(c,CGRectMake(30,30,180,120),12,12,m,f,0,-1,.4,1);
    assert((pixel(c,120,22)&0xffffff)<(bright&0xffffff));
  }
  for(int m=OC_GLASS;m<OC_MATERIAL_COUNT;m++) {
    CGContextClearRect(c,CGRectMake(0,0,240,180));
    overcoat_draw(c,CGRectMake(30,30,180,120),12,1,m,0,0,-1,0,0);
    assert(pixel(c,120,90)==0 && (pixel(c,120,29)>>24)>0);
  }
  CGContextClearRect(c,CGRectMake(0,0,240,180));
  overcoat_draw(c,CGRectMake(30,30,180,120),12,NAN,OC_GLASS,0,0,-1,0,1);
  overcoat_draw(c,CGRectMake(30,30,180,120),12,12,OC_GLASS,999,0,-1,0,1);
  assert(pixel(c,120,22)==0);
  assert(overcoat_select("metal") && g_overcoat_material==OC_METAL);
  assert(!overcoat_select("unknown") && g_overcoat_material==OC_METAL);
  assert(!overcoat_finish(-1) && !overcoat_finish(3));
  assert(overcoat_finish(2) && g_overcoat_finishes[OC_METAL]==2);
  CGContextRelease(c);puts("PASS: 15 distinct material finishes, clear content, ring bounds, thin borders, dimming, invalid inputs");
}
