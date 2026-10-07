#pragma once

#include <QImage>
#include <QColor>

namespace Delta {

[[nodiscard]] QImage AvatarImage(quint64 photoId);
[[nodiscard]] QColor SelfAvatarColor();

} // namespace Delta
