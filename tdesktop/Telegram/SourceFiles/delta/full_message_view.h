#pragma once

#include <functional>

#include <QBuffer>
#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtGui/QTextBlock>
#include <QtGui/QTextDocument>
#include <QtGui/QTextFragment>
#include <QtWidgets/QTextBrowser>

namespace Delta {

inline constexpr int kFullMessageImageBytes = 262144;
inline constexpr int kFullMessageEncodedBytes = kFullMessageImageBytes * 3;
inline constexpr int kFullMessageHeaderBytes = 128;
inline constexpr int kFullMessageImageEdge = 2048;
inline constexpr int kFullMessageMaxImages = 8;
inline constexpr qint64 kFullMessageMaxPixels = 1024 * 1024;

inline const QString kFullMessageLimitedNote = QStringLiteral(
	"Images and formatting may look different from the original message.");
inline const QString kFullMessagePlainNote = QStringLiteral(
	"The original images and formatting could not be shown. The message text is below.");

[[nodiscard]] inline bool FullMessageTextMeaningful(const QString &text) {
	for (const auto ch : text) {
		if (ch != QChar::ObjectReplacementCharacter && !ch.isSpace()) {
			return true;
		}
	}
	return false;
}

class FullMessageView final : public QTextBrowser {
public:
	using LinkHandler = std::function<void(const QUrl &)>;

	explicit FullMessageView(QWidget *parent = nullptr)
	: QTextBrowser(parent) {
		setOpenLinks(false);
		setOpenExternalLinks(false);
		setReadOnly(true);
		setUndoRedoEnabled(false);
		setContextMenuPolicy(Qt::NoContextMenu);
		setTextInteractionFlags(
			Qt::TextSelectableByMouse
			| Qt::TextSelectableByKeyboard
			| Qt::LinksAccessibleByMouse
			| Qt::LinksAccessibleByKeyboard);
		QObject::connect(
			this,
			&QTextBrowser::anchorClicked,
			this,
			[this](const QUrl &url) {
				if (_linkHandler) {
					_linkHandler(url);
				}
			});
	}

	void setLinkHandler(LinkHandler handler) {
		_linkHandler = std::move(handler);
	}

	// WHY: Qt 6.4 QTextBrowser::setSource is public non-virtual; hide it so
	// derived calls cannot navigate. Internal Qt still reaches doSetSource.
	void setSource(
		const QUrl &,
		QTextDocument::ResourceType = QTextDocument::UnknownResource) {
	}

	void setBody(const QString &html, const QString &fallback) {
		resetResources();
		_usedFallback = false;
		_statusNote = kFullMessageLimitedNote;
		document()->clear();
		if (!html.isEmpty()) {
			setHtml(html);
			resolveInlineImages();
		} else {
			setPlainText(QString());
		}
		if (FullMessageTextMeaningful(toPlainText()) || _loadedImages > 0) {
			return;
		}
		_usedFallback = true;
		_statusNote = kFullMessagePlainNote;
		document()->clear();
		const auto text = fallback.trimmed();
		setPlainText(text.isEmpty() ? kFullMessagePlainNote : fallback);
	}

	[[nodiscard]] QString visibleText() const {
		return toPlainText();
	}

	[[nodiscard]] bool usedFallback() const {
		return _usedFallback;
	}

	[[nodiscard]] QString statusNote() const {
		return _statusNote;
	}

	[[nodiscard]] QStringList deniedResources() const {
		return _denied;
	}

	[[nodiscard]] int loadedImageCount() const {
		return _loadedImages;
	}

	[[nodiscard]] qint64 decodedPixels() const {
		return _decodedPixels;
	}

	[[nodiscard]] QVariant fetchResource(int type, const QUrl &url) {
		return loadResource(type, url);
	}

protected:
	// WHY: This is the Qt 6.4 virtual navigation seam. Keep it empty so
	// QTextBrowser::setSource cannot load network/file/qrc documents.
	void doSetSource(
		const QUrl &,
		QTextDocument::ResourceType = QTextDocument::UnknownResource) override {
	}

	QVariant loadResource(int type, const QUrl &name) override {
		auto key = name.toString(QUrl::None);
		if (key.isEmpty()) {
			key = QString::fromUtf8(name.toEncoded());
		}
		if (_cache.contains(key)) {
			return _cache.value(key);
		}
		const auto deny = [&] {
			rememberDenied(name);
			_cache.insert(key, QVariant());
			return QVariant();
		};
		if (type != QTextDocument::ImageResource) {
			return deny();
		}
		if (name.scheme().compare(
				QLatin1String("data"),
				Qt::CaseInsensitive) != 0) {
			return deny();
		}
		const auto loaded = loadDataImage(name);
		_cache.insert(key, loaded);
		if (!loaded.isValid()) {
			rememberDenied(name);
		}
		return loaded;
	}

private:
	void resolveInlineImages() {
		for (auto block = document()->begin()
			; block.isValid()
			; block = block.next()) {
			for (auto it = block.begin(); !it.atEnd(); ++it) {
				const auto format = it.fragment().charFormat();
				if (!format.isImageFormat()) {
					continue;
				}
				const auto loaded = document()->resource(
					QTextDocument::ImageResource,
					QUrl(format.toImageFormat().name()));
				if (!loaded.isValid()) {
					continue;
				}
			}
		}
	}

	void resetResources() {
		_cache.clear();
		_denied.clear();
		_loadedImages = 0;
		_decodedPixels = 0;
	}

	void rememberDenied(const QUrl &name) {
		auto key = name.scheme().toLower();
		if (key.isEmpty()) {
			key = QStringLiteral("data");
		}
		if (!_denied.contains(key)) {
			_denied.push_back(key);
		}
	}

	[[nodiscard]] static const char *RasterFormat(const QString &mime) {
		if (mime == QStringLiteral("image/png")) {
			return "PNG";
		} else if (mime == QStringLiteral("image/jpeg")) {
			return "JPEG";
		} else if (mime == QStringLiteral("image/gif")) {
			return "GIF";
		} else if (mime == QStringLiteral("image/webp")) {
			return "WEBP";
		} else if (mime == QStringLiteral("image/bmp")) {
			return "BMP";
		}
		return nullptr;
	}

	[[nodiscard]] QVariant loadDataImage(const QUrl &name) {
		auto spec = name.toString(QUrl::None);
		if (spec.isEmpty()) {
			spec = QString::fromUtf8(name.toEncoded());
		}
		if (!spec.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)) {
			return {};
		}
		const auto comma = spec.indexOf(QLatin1Char(','));
		if (comma < 5) {
			return {};
		}
		const auto header = spec.mid(5, comma - 5);
		const auto payload = spec.mid(comma + 1);
		if (header.size() > kFullMessageHeaderBytes
			|| payload.size() > kFullMessageEncodedBytes) {
			return {};
		}
		const auto parts = header.split(QLatin1Char(';'));
		auto mime = parts.value(0).toLower();
		if (mime == QStringLiteral("image/jpg")) {
			mime = QStringLiteral("image/jpeg");
		}
		auto base64 = false;
		for (auto i = 1; i < parts.size(); ++i) {
			if (parts.at(i).compare(
					QLatin1String("base64"),
					Qt::CaseInsensitive) == 0) {
				base64 = true;
			}
		}
		if (mime.contains(QStringLiteral("svg"))
			|| mime.contains(QStringLiteral("xml"))) {
			return {};
		}
		const auto format = RasterFormat(mime);
		if (!format) {
			return {};
		}
		const auto encoded = payload.toUtf8();
		if (encoded.size() > kFullMessageEncodedBytes) {
			return {};
		}
		const auto bytes = base64
			? QByteArray::fromBase64(encoded)
			: QByteArray::fromPercentEncoding(encoded);
		if (bytes.isEmpty() || bytes.size() > kFullMessageImageBytes) {
			return {};
		}
		QBuffer buffer;
		buffer.setData(bytes);
		if (!buffer.open(QIODevice::ReadOnly)) {
			return {};
		}
		QImageReader reader(&buffer, QByteArray(format));
		// WHY: MIME is attacker-controlled; autodetect would pick SVG/XML plugins.
		reader.setAutoDetectImageFormat(false);
		reader.setDecideFormatFromContent(false);
		reader.setFormat(QByteArray(format));
		if (reader.format().toLower() != QByteArray(format).toLower()) {
			return {};
		}
		const auto size = reader.size();
		if (!size.isValid()
			|| size.width() < 1
			|| size.height() < 1
			|| size.width() > kFullMessageImageEdge
			|| size.height() > kFullMessageImageEdge) {
			return {};
		}
		const auto pixels = qint64(size.width()) * qint64(size.height());
		if (pixels <= 0
			|| _loadedImages >= kFullMessageMaxImages
			|| _decodedPixels > kFullMessageMaxPixels - pixels) {
			return {};
		}
		auto image = reader.read();
		if (image.isNull()
			|| image.width() < 1
			|| image.height() < 1
			|| image.width() > kFullMessageImageEdge
			|| image.height() > kFullMessageImageEdge) {
			return {};
		}
		const auto actual = qint64(image.width()) * qint64(image.height());
		if (actual <= 0
			|| _decodedPixels > kFullMessageMaxPixels - actual) {
			return {};
		}
		++_loadedImages;
		_decodedPixels += actual;
		return QVariant(QPixmap::fromImage(std::move(image)));
	}

	LinkHandler _linkHandler;
	QHash<QString, QVariant> _cache;
	QStringList _denied;
	QString _statusNote = kFullMessageLimitedNote;
	qint64 _decodedPixels = 0;
	int _loadedImages = 0;
	bool _usedFallback = false;

};

} // namespace Delta
