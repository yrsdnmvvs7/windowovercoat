// Renders the menu bar template icon for Window Overcoat at 36x36 (@2x of 18pt).
// Template icons must be pure alpha: solid shape, fully transparent background,
// antialiased edges only. The system recolors them automatically.
// Usage: swift menubar-icon.swift <output.png>
import AppKit

let px = 36
guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px,
                                 bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                                 isPlanar: false, colorSpaceName: .deviceRGB,
                                 bytesPerRow: 0, bitsPerPixel: 0) else {
  fputs("error: could not create bitmap\n", stderr)
  exit(1)
}
let ctx = NSGraphicsContext(bitmapImageRep: rep)!
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = ctx

// Coat silhouette: two sleeves plus a body, drawn as one flat shape.
NSColor.black.setFill()
let sleeveL = NSBezierPath(roundedRect: NSRect(x: 0, y: 14, width: 13, height: 8), xRadius: 3, yRadius: 3)
sleeveL.fill()
let sleeveR = NSBezierPath(roundedRect: NSRect(x: 23, y: 14, width: 13, height: 8), xRadius: 3, yRadius: 3)
sleeveR.fill()
let body = NSBezierPath(roundedRect: NSRect(x: 8, y: 4, width: 20, height: 22), xRadius: 4, yRadius: 4)
body.fill()

// V neckline cut into the body.
NSColor.clear.setFill()
let collar = NSBezierPath()
collar.move(to: NSPoint(x: 11, y: 24))
collar.line(to: NSPoint(x: 18, y: 16))
collar.line(to: NSPoint(x: 25, y: 24))
collar.close()
collar.fill()

// Browser window cut out of the coat: rounded rect plus a pane divider.
let window = NSBezierPath(roundedRect: NSRect(x: 13, y: 6, width: 10, height: 9), xRadius: 1.5, yRadius: 1.5)
window.fill()
let pane = NSBezierPath(rect: NSRect(x: 13, y: 10.4, width: 10, height: 1.2))
pane.fill()

NSGraphicsContext.restoreGraphicsState()

guard let png = rep.representation(using: .png, properties: [:]) else {
  fputs("error: could not encode PNG\n", stderr)
  exit(1)
}
let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "MenuBarIcon.png"
try! png.write(to: URL(fileURLWithPath: out))
print("wrote \(out)")
