#import <Cocoa/Cocoa.h>
#import "misc/status_icon.h"

// Render the bundle icon from the exact vector used by the status item.
int main(int argc, const char** argv) {
  @autoreleasepool {
    if (argc != 2) return 1;
    NSString* directory = [NSString stringWithUTF8String:argv[1]];
    for (NSNumber* value in @[@16, @32, @128, @256, @512]) {
      NSInteger points = value.integerValue;
      for (NSInteger scale = 1; scale <= 2; scale++) {
        NSInteger pixels = points * scale;
        NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc]
          initWithBitmapDataPlanes:NULL pixelsWide:pixels pixelsHigh:pixels
          bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
          colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap]];
        NSAffineTransform* transform = [NSAffineTransform transform];
        [transform scaleBy:(CGFloat)pixels / 1024.0];
        [transform concat];
        [[NSColor colorWithSRGBRed:.97 green:.95 blue:.92 alpha:1] setFill];
        [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(24,24,976,976)
          xRadius:218 yRadius:218] fill];
        NSImage* mark = knit_status_icon();
        mark.template = NO;
        [mark drawInRect:NSMakeRect(174,174,676,676) fromRect:NSZeroRect
          operation:NSCompositingOperationSourceOver fraction:.83];
        [NSGraphicsContext restoreGraphicsState];
        NSString* name = [NSString stringWithFormat:@"icon_%ldx%ld%@.png",
          (long)points, (long)points, scale == 2 ? @"@2x" : @""];
        NSData* data = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        if (![data writeToFile:[directory stringByAppendingPathComponent:name] atomically:YES]) return 1;
      }
    }
  }
  return 0;
}
