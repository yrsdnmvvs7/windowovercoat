#pragma once
#import <Cocoa/Cocoa.h>

// Original vector mark; kept in sync with assets/overcoat-menu-icon.svg.
// A knit cardigan: shoulders slope out into sleeves that hang down, with a
// ribbed crew collar, front opening and ribbed hem. Tuned for an 18 pt
// template, where only the silhouette reads and the ribbing is decoration.
static NSImage* knit_status_icon(void) {
  NSImage* image = [NSImage imageWithSize:NSMakeSize(18, 18) flipped:NO
    drawingHandler:^BOOL(NSRect rect) {
      (void)rect;
      [NSColor.blackColor setStroke];

      // Body, sleeves and hem as one closed outline, symmetric about x = 9.
      NSBezierPath* coat = [NSBezierPath bezierPath];
      coat.lineWidth = 1.3;
      coat.lineJoinStyle = NSLineJoinStyleRound;
      coat.lineCapStyle = NSLineCapStyleRound;
      [coat moveToPoint:NSMakePoint(11.4, 13.9)];          // right neck
      [coat lineToPoint:NSMakePoint(13.0, 13.5)];          // right shoulder
      [coat lineToPoint:NSMakePoint(16.0, 9.4)];           // right cuff outer
      [coat lineToPoint:NSMakePoint(14.4, 8.2)];           // right cuff inner
      [coat lineToPoint:NSMakePoint(12.4, 10.6)];          // right armpit
      [coat lineToPoint:NSMakePoint(12.4, 2.8)];           // right hem
      [coat lineToPoint:NSMakePoint(5.6, 2.8)];            // hem
      [coat lineToPoint:NSMakePoint(5.6, 10.6)];           // left armpit
      [coat lineToPoint:NSMakePoint(3.6, 8.2)];            // left cuff inner
      [coat lineToPoint:NSMakePoint(2.0, 9.4)];            // left cuff outer
      [coat lineToPoint:NSMakePoint(5.0, 13.5)];           // left shoulder
      [coat lineToPoint:NSMakePoint(6.6, 13.9)];           // left neck
      [coat curveToPoint:NSMakePoint(11.4, 13.9)           // collar opening
            controlPoint1:NSMakePoint(7.7, 12.1)
            controlPoint2:NSMakePoint(10.3, 12.1)];
      [coat stroke];

      NSBezierPath* ribs = [NSBezierPath bezierPath];
      ribs.lineWidth = 0.95;
      ribs.lineCapStyle = NSLineCapStyleRound;

      [ribs moveToPoint:NSMakePoint(7.5, 13.6)];           // collar band
      [ribs curveToPoint:NSMakePoint(10.5, 13.6)
            controlPoint1:NSMakePoint(8.2, 11.7)
            controlPoint2:NSMakePoint(9.8, 11.7)];
      [ribs moveToPoint:NSMakePoint(8.15, 12.5)];          // collar rib
      [ribs lineToPoint:NSMakePoint(7.85, 13.35)];
      [ribs moveToPoint:NSMakePoint(9.85, 12.5)];          // collar rib
      [ribs lineToPoint:NSMakePoint(10.15, 13.35)];

      [ribs moveToPoint:NSMakePoint(9.0, 11.85)];          // front opening
      [ribs lineToPoint:NSMakePoint(9.0, 2.9)];
      [ribs moveToPoint:NSMakePoint(6.9, 2.95)];           // hem rib
      [ribs lineToPoint:NSMakePoint(6.9, 4.1)];
      [ribs moveToPoint:NSMakePoint(11.1, 2.95)];          // hem rib
      [ribs lineToPoint:NSMakePoint(11.1, 4.1)];

      [ribs moveToPoint:NSMakePoint(15.6, 10.3)];          // right cuff rib
      [ribs lineToPoint:NSMakePoint(14.0, 9.1)];
      [ribs moveToPoint:NSMakePoint(2.4, 10.3)];           // left cuff rib
      [ribs lineToPoint:NSMakePoint(4.0, 9.1)];
      [ribs stroke];

      return YES;
    }];
  image.template = YES;
  image.accessibilityDescription = NSLocalizedString(@"Window Overcoat", nil);
  return image;
}
