/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

/* Renders SF Symbol glyphs as CGImages for use in the Core Text backend.
   Requires macOS 11+ or iOS 13+; returns NULL on older systems. */

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

#if TARGET_OS_OSX
#   import <AppKit/AppKit.h>
#else
#   import <UIKit/UIKit.h>
#endif

CGImageRef sfSymbolCreateImage_Apple(const char *symbolName, float pointSize, CGColorRef color,
                                     int slotPixels) {
    NSString *name = [NSString stringWithUTF8String:symbolName];
    if (!name) return NULL;
#if TARGET_OS_OSX
    if (@available(macOS 11.0, *)) {
        NSImageSymbolConfiguration *cfg =
            [NSImageSymbolConfiguration configurationWithPointSize:(CGFloat)pointSize
                                                            weight:NSFontWeightRegular];
        NSImage *img = [[NSImage imageWithSystemSymbolName:name accessibilityDescription:nil]
                        imageWithSymbolConfiguration:cfg];
        if (!img) return NULL;
        NSSize sz = img.size;
        if (sz.width <= 0 || sz.height <= 0) return NULL;
        /* Compute render dimensions: fit the symbol's natural aspect ratio into slotPixels tall,
           so the bitmap is exactly the size it will be drawn at — no scaling at composite time. */
        NSInteger renderH = (NSInteger)(slotPixels > 0 ? slotPixels : (int)ceil(sz.height));
        NSInteger renderW = (NSInteger)ceil(sz.width * renderH / sz.height);
        /* Render into a concrete bitmap. Using a drawing-handler NSImage and
           CGImageForProposedRect: is unreliable — the handler may never be called
           because the image has no backing raster representation until drawn.
           NSBitmapImageRep forces immediate rendering. */
        NSBitmapImageRep *rep =
            [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                    pixelsWide:renderW
                                                    pixelsHigh:renderH
                                                 bitsPerSample:8
                                               samplesPerPixel:4
                                                      hasAlpha:YES
                                                      isPlanar:NO
                                                colorSpaceName:NSCalibratedRGBColorSpace
                                                   bytesPerRow:0
                                                  bitsPerPixel:0];
        if (!rep) return NULL;
        NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:gc];
        /* Tint: fill with the target color, then mask to symbol alpha with DestinationIn. */
        [[NSColor colorWithCGColor:color] set];
        NSRectFill(NSMakeRect(0, 0, renderW, renderH));
        [img drawInRect:NSMakeRect(0, 0, renderW, renderH)
               fromRect:NSZeroRect
              operation:NSCompositingOperationDestinationIn
               fraction:1.0
         respectFlipped:YES
                  hints:nil];
#if 0
        /* DEBUG: 1-pixel checkerboard to verify 1:1 pixel mapping on screen. */ {
            uint8_t  *px  = [rep bitmapData];
            NSInteger bpr = [rep bytesPerRow];
            for (NSInteger y = 0; y < renderH; y++) {
                for (NSInteger x = 0; x < renderW; x++) {
                    if ((x ^ y) & 1) {
                        uint8_t *p = px + y * bpr + x * 4;
                        p[0] = 255; p[1] = 0; p[2] = 0; p[3] = 255; /* opaque red */
                    }
                }
            }
        }
#endif
        [NSGraphicsContext restoreGraphicsState];
        CGImageRef cg = [rep CGImage];
        return cg ? CGImageRetain(cg) : NULL;
    }
#else /* iOS */
    if (@available(iOS 13.0, *)) {
        /* TODO: Should work the same way as on macOS (see above). */
        UIImageSymbolConfiguration *cfg =
            [UIImageSymbolConfiguration configurationWithPointSize:(CGFloat)pointSize
                                                            weight:UIImageSymbolWeightRegular];
        UIImage *img = [UIImage systemImageNamed:name withConfiguration:cfg];
        if (!img) return NULL;
        UIColor *tint   = [UIColor colorWithCGColor:color];
        UIImage *tinted = [img imageWithTintColor:tint
                                    renderingMode:UIImageRenderingModeAlwaysOriginal];
        return tinted.CGImage ? CGImageRetain(tinted.CGImage) : NULL;
    }
#endif
    return NULL;
}
