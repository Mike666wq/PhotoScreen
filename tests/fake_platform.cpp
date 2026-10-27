#include "platform/platform.h"
namespace photo {
bool writeClipboard(const QImage &, QString *error) { if (error) *error=QStringLiteral("test clipboard unavailable"); return false; }
}
