#pragma once
#include <QImage>
#include <CoreGraphics/CoreGraphics.h>

namespace photo {
QImage imageFromCGImage(CGImageRef image);
}
