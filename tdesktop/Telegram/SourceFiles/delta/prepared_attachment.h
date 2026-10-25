#pragma once

#include <QtCore/QByteArray>

#include <vector>

namespace Delta {

// Photo parts contain the final edited/resized encoding. Clipboard content
// may still contain the original image, so it must not take precedence.
// Sending as a file deliberately preserves the original bytes instead.
[[nodiscard]] inline QByteArray PreparedAttachmentBytes(
		bool photo,
		const QByteArray &content,
		const std::vector<QByteArray> &parts) {
	if ((!photo || parts.empty()) && !content.isEmpty()) return content;
	auto result = QByteArray();
	for (const auto &part : parts) result.append(part);
	return result;
}

} // namespace Delta
