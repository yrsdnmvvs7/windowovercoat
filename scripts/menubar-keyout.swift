import Foundation
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers

// Usage: menubar-keyout <input.png> <output.png>
// Key out near-solid backgrounds by sampling the four corners, feather the
// edge so the silhouette stays smooth at 18 pt, and premultiply correctly.
let args = CommandLine.arguments
guard args.count == 3 else { fatalError("usage: menubar-keyout in.png out.png") }
let input = args[1], output = args[2]

guard let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: input) as CFURL, nil),
      let img = CGImageSourceCreateImageAtIndex(src, 0, nil) else { fatalError("read failed") }
let w = img.width, h = img.height
var pixels = [UInt8](repeating: 0, count: w * h * 4)
let cs = CGColorSpaceCreateDeviceRGB()
let ctx = CGContext(data: &pixels, width: w, height: h, bitsPerComponent: 8,
                    bytesPerRow: w * 4, space: cs,
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
ctx.draw(img, in: CGRect(x: 0, y: 0, width: w, height: h))

func px(_ x: Int, _ y: Int) -> (r: Int, g: Int, b: Int) {
    let i = (y * w + x) * 4
    return (Int(pixels[i]), Int(pixels[i + 1]), Int(pixels[i + 2]))
}
let corners = [px(2, 2), px(w - 3, 2), px(2, h - 3), px(w - 3, h - 3)]
let bg = (r: (corners[0].r + corners[1].r + corners[2].r + corners[3].r) / 4,
          g: (corners[0].g + corners[1].g + corners[2].g + corners[3].g) / 4,
          b: (corners[0].b + corners[1].b + corners[2].b + corners[3].b) / 4)

let tol = 80
for y in 0..<h {
    for x in 0..<w {
        let p = px(x, y)
        let d = abs(p.r - bg.r) + abs(p.g - bg.g) + abs(p.b - bg.b)
        if d < tol {
            let i = (y * w + x) * 4
            let a = Double(d) / Double(tol)          // 0 = solid bg, 1 = subject
            let f = a                                   // premultiply to new alpha
            pixels[i] = UInt8(Double(pixels[i]) * f)
            pixels[i + 1] = UInt8(Double(pixels[i + 1]) * f)
            pixels[i + 2] = UInt8(Double(pixels[i + 2]) * f)
            pixels[i + 3] = UInt8(Double(pixels[i + 3]) * a)
        }
    }
}

let outCtx = CGContext(data: &pixels, width: w, height: h, bitsPerComponent: 8,
                       bytesPerRow: w * 4, space: cs,
                       bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
let outImg = outCtx.makeImage()!
let dest = CGImageDestinationCreateWithURL(URL(fileURLWithPath: output) as CFURL,
                                           UTType.png.identifier as CFString, 1, nil)!
CGImageDestinationAddImage(dest, outImg, nil)
CGImageDestinationFinalize(dest)
print("done \(w)x\(h) bg=(\(bg.r),\(bg.g),\(bg.b))")
