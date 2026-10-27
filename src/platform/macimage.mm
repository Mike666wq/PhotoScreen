#include "macimage.h"

namespace photo {
QImage imageFromCGImage(CGImageRef image) {
    if (!image) return {};
    const int width=int(CGImageGetWidth(image)), height=int(CGImageGetHeight(image));
    QImage result(width,height,QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) return {};
    CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx=CGBitmapContextCreate(result.bits(),width,height,8,result.bytesPerLine(),color,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(color);
    if (!ctx) return {};
    CGContextTranslateCTM(ctx,0,height); CGContextScaleCTM(ctx,1,-1);
    CGContextDrawImage(ctx,CGRectMake(0,0,width,height),image); CGContextRelease(ctx);
    return result;
}
}
