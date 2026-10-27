#include "full_message_view.h"

#include <QBuffer>
#include <QImage>
#include <QtTest/QtTest>
#include <QtWidgets/QTextBrowser>

using namespace Qt::StringLiterals;

namespace {

const auto kFallback = u"Integrity HTML fallback native-a19 20261026"_s;
const auto kMarker = u"HTML-only detail marker 20261026"_s;
const auto kTerminal = u"$ echo integrity-terminal-line-20261026"_s;
const auto kBody = u"<p>Integrity HTML fallback native-a19 20261026</p>"
	"<p>HTML-only detail marker 20261026</p>"
	"<pre>$ echo integrity-terminal-line-20261026</pre>"
	"<table><tr><th>Item</th><th>Detail</th></tr>"
	"<tr><td>Integrity</td><td>HTML-only detail marker 20261026</td></tr>"
	"</table>"_s;
const auto kPng = QByteArray(
	"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==");
const auto kBmp = QByteArray::fromHex(
	"424d3a000000000000003600000028000000010000000100000001001800"
	"0000000004000000000000000000000000000000000000000000ff00");
const auto kHugePng = QByteArray::fromHex(
	"89504e470d0a1a0a0000000d4948445200000bb8000000080802000000"
	"e6ba25a10000000049454e44ae426082");

[[nodiscard]] QString DataPng() {
	return u"data:image/png;base64,"_s + QString::fromLatin1(kPng);
}

[[nodiscard]] QString DataUrl(const QString &mime, const QByteArray &bytes) {
	return u"data:"_s + mime + u";base64,"_s + QString::fromLatin1(bytes.toBase64());
}

[[nodiscard]] QByteArray SolidPng(int width, int height) {
	auto image = QImage(width, height, QImage::Format_RGB32);
	image.fill(Qt::red);
	auto bytes = QByteArray();
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return bytes;
}

} // namespace

class FullMessageViewTests final : public QObject {
	Q_OBJECT

private slots:
	void ctorDisablesNavigation() {
		Delta::FullMessageView view;
		QVERIFY(!view.openLinks());
		QVERIFY(!view.openExternalLinks());
		QCOMPARE(
			int(view.contextMenuPolicy()),
			int(Qt::NoContextMenu));
	}

	void visibleTextKeepsMarkerTableAndTerminal() {
		Delta::FullMessageView view;
		view.setBody(kBody, kFallback);
		const auto text = view.visibleText();
		QVERIFY(text.contains(kMarker));
		QVERIFY(text.contains(kFallback));
		QVERIFY(text.contains(u"Item"_s));
		QVERIFY(text.contains(u"Detail"_s));
		QVERIFY(text.contains(kTerminal));
		QVERIFY(!view.usedFallback());
		QCOMPARE(view.statusNote(), Delta::kFullMessageLimitedNote);
	}

	void emptyVisibleUsesFallback() {
		Delta::FullMessageView view;
		auto opened = 0;
		view.setLinkHandler([&](const QUrl &) { ++opened; });
		view.setBody(QString(), kFallback);
		QVERIFY(view.usedFallback());
		QCOMPARE(view.visibleText(), kFallback);
		QCOMPARE(view.statusNote(), Delta::kFullMessagePlainNote);
		QCOMPARE(opened, 0);
	}

	void hostileResourcesAreDenied() {
		Delta::FullMessageView view;
		const auto html = u"<html><body>"
			"<script src='https://evil.example/x.js'>alert(1)</script>"
			"<iframe src='http://evil.example/frame'></iframe>"
			"<img src='http://evil.example/img.png'>"
			"<img src='https://evil.example/img.png'>"
			"<img src='file:///etc/passwd'>"
			"<img src='qrc:/icons/secret.png'>"
			"<img src='cid:payload@mail'>"
			"<img src='javascript:alert(1)'>"
			"<p>HTML-only detail marker 20261026</p>"
			"</body></html>"_s;
		view.setBody(html, kFallback);
		QVERIFY(view.visibleText().contains(kMarker));
		const auto image = int(QTextDocument::ImageResource);
		const auto htmlType = int(QTextDocument::HtmlResource);
		QVERIFY(!view.fetchResource(
			image,
			QUrl(u"http://evil.example/img.png"_s)).isValid());
		QVERIFY(!view.fetchResource(
			image,
			QUrl(u"https://evil.example/img.png"_s)).isValid());
		QVERIFY(!view.fetchResource(
			image,
			QUrl(u"file:///etc/passwd"_s)).isValid());
		QVERIFY(!view.fetchResource(
			image,
			QUrl(u"qrc:/icons/secret.png"_s)).isValid());
		QVERIFY(!view.fetchResource(
			image,
			QUrl(u"cid:payload@mail"_s)).isValid());
		QVERIFY(!view.fetchResource(
			image,
			QUrl(u"javascript:alert(1)"_s)).isValid());
		QVERIFY(!view.fetchResource(
			htmlType,
			QUrl(u"http://evil.example/frame"_s)).isValid());
		const auto denied = view.deniedResources().join(u' ');
		QVERIFY(denied.contains(u"http"_s));
		QVERIFY(denied.contains(u"https"_s));
		QVERIFY(denied.contains(u"file"_s));
		QVERIFY(denied.contains(u"qrc"_s));
		QVERIFY(denied.contains(u"cid"_s));
		QVERIFY(denied.contains(u"javascript"_s));
		QVERIFY(!denied.contains(u"evil.example"_s));
		QVERIFY(!denied.contains(u"/etc/passwd"_s));
		QVERIFY(!denied.contains(u"payload@mail"_s));
	}

	void httpsAnchorDoesNotOpenByItself() {
		Delta::FullMessageView view;
		auto opened = QList<QUrl>();
		view.setLinkHandler([&](const QUrl &url) { opened.push_back(url); });
		view.setBody(
			u"<p><a href='https://evil.example/secret'>link</a></p>"
			"<p>HTML-only detail marker 20261026</p>"_s,
			kFallback);
		QCOMPARE(opened.size(), 0);
		view.setSource(QUrl(u"https://evil.example/secret"_s));
		QCOMPARE(opened.size(), 0);
		QVERIFY(view.source().isEmpty());
		QVERIFY(view.visibleText().contains(kMarker));
		static_cast<QTextBrowser&>(view).setSource(
			QUrl(u"https://evil.example/secret"_s));
		QCOMPARE(opened.size(), 0);
		QVERIFY(view.visibleText().contains(kMarker));
	}

	void boundedPngIsAllowed() {
		Delta::FullMessageView view;
		const auto url = QUrl(DataPng());
		const auto loaded = view.fetchResource(
			int(QTextDocument::ImageResource),
			url);
		QVERIFY(loaded.isValid());
		QVERIFY(loaded.canConvert<QPixmap>());
		QVERIFY(view.deniedResources().isEmpty());
		QCOMPARE(view.loadedImageCount(), 1);
	}

	void svgDataIsRejected() {
		Delta::FullMessageView view;
		const auto svg = u"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg'>"
			"<image href='http://evil.example/x.png'/></svg>"_s;
		QVERIFY(!view.fetchResource(
			int(QTextDocument::ImageResource),
			QUrl(svg)).isValid());
		QVERIFY(!view.deniedResources().isEmpty());
	}

	void mimeSpoofBmpAsPngIsRejected() {
		Delta::FullMessageView view;
		const auto url = QUrl(DataUrl(u"image/png"_s, kBmp));
		QVERIFY(!view.fetchResource(
			int(QTextDocument::ImageResource),
			url).isValid());
		QVERIFY(view.deniedResources().contains(u"data"_s));
		QCOMPARE(view.loadedImageCount(), 0);
		const auto genuine = QUrl(DataUrl(u"image/bmp"_s, kBmp));
		QVERIFY(view.fetchResource(
			int(QTextDocument::ImageResource),
			genuine).isValid());
		QCOMPARE(view.loadedImageCount(), 1);
	}

	void largeDataIsRejected() {
		Delta::FullMessageView view;
		const auto raw = QByteArray(Delta::kFullMessageImageBytes + 1, 'A');
		const auto url = u"data:image/png;base64,"_s
			+ QString::fromLatin1(raw.toBase64());
		QVERIFY(!view.fetchResource(
			int(QTextDocument::ImageResource),
			QUrl(url)).isValid());
	}

	void encodedPayloadIsRejectedBeforeDecode() {
		Delta::FullMessageView view;
		const auto raw = QByteArray(Delta::kFullMessageEncodedBytes + 1, 'A');
		const auto url = u"data:image/png;base64,"_s
			+ QString::fromLatin1(raw);
		QVERIFY(!view.fetchResource(
			int(QTextDocument::ImageResource),
			QUrl(url)).isValid());
		QCOMPARE(view.loadedImageCount(), 0);
	}

	void oversizedHeaderIsRejected() {
		Delta::FullMessageView view;
		const auto header = QString(Delta::kFullMessageHeaderBytes + 1, u'x');
		const auto url = u"data:image/png;"_s
			+ header
			+ u";base64,"_s
			+ QString::fromLatin1(kPng);
		QVERIFY(!view.fetchResource(
			int(QTextDocument::ImageResource),
			QUrl(url)).isValid());
	}

	void oversizedRasterIsRejected() {
		Delta::FullMessageView view;
		auto image = QImage(
			Delta::kFullMessageImageEdge + 1,
			8,
			QImage::Format_RGB32);
		image.fill(Qt::red);
		auto bytes = QByteArray();
		QBuffer buffer(&bytes);
		QVERIFY(buffer.open(QIODevice::WriteOnly));
		QVERIFY(image.save(&buffer, "PNG"));
		const auto url = u"data:image/png;base64,"_s
			+ QString::fromLatin1(bytes.toBase64());
		QVERIFY(!view.fetchResource(
			int(QTextDocument::ImageResource),
			QUrl(url)).isValid());
		QVERIFY(!view.deniedResources().isEmpty());
		QCOMPARE(view.loadedImageCount(), 0);
	}

	void oversizedPngHeaderIsRejected() {
		Delta::FullMessageView view;
		QVERIFY(!view.fetchResource(
			int(QTextDocument::ImageResource),
			QUrl(DataUrl(u"image/png"_s, kHugePng))).isValid());
		QCOMPARE(view.loadedImageCount(), 0);
	}

	void totalPixelBudgetIsEnforced() {
		Delta::FullMessageView view;
		const auto bytes = SolidPng(800, 800);
		QVERIFY(!bytes.isEmpty());
		const auto first = QUrl(DataUrl(u"image/png;x=1"_s, bytes));
		const auto second = QUrl(DataUrl(u"image/png;x=2"_s, bytes));
		QVERIFY(view.fetchResource(
			int(QTextDocument::ImageResource),
			first).isValid());
		QCOMPARE(view.loadedImageCount(), 1);
		QVERIFY(!view.fetchResource(
			int(QTextDocument::ImageResource),
			second).isValid());
		QCOMPARE(view.loadedImageCount(), 1);
	}

	void imageCountBudgetIsEnforced() {
		Delta::FullMessageView view;
		for (auto i = 0; i < Delta::kFullMessageMaxImages; ++i) {
			const auto url = QUrl(
				u"data:image/png;n="_s
				+ QString::number(i)
				+ u";base64,"_s
				+ QString::fromLatin1(kPng));
			QVERIFY(view.fetchResource(
				int(QTextDocument::ImageResource),
				url).isValid());
		}
		QCOMPARE(view.loadedImageCount(), Delta::kFullMessageMaxImages);
		const auto extra = QUrl(
			u"data:image/png;n=extra;base64,"_s
			+ QString::fromLatin1(kPng));
		QVERIFY(!view.fetchResource(
			int(QTextDocument::ImageResource),
			extra).isValid());
		QCOMPARE(view.loadedImageCount(), Delta::kFullMessageMaxImages);
	}

	void cachedImageDoesNotRecountBudget() {
		Delta::FullMessageView view;
		const auto url = QUrl(DataPng());
		QVERIFY(view.fetchResource(
			int(QTextDocument::ImageResource),
			url).isValid());
		QVERIFY(view.fetchResource(
			int(QTextDocument::ImageResource),
			url).isValid());
		QCOMPARE(view.loadedImageCount(), 1);
	}

	void imageOnlyBlockedUsesFallback() {
		Delta::FullMessageView view;
		view.setBody(
			u"<img src='http://evil.example/x.png' alt=''>"_s,
			kFallback);
		QVERIFY(view.usedFallback());
		QCOMPARE(view.visibleText(), kFallback);
		QVERIFY(!view.visibleText().contains(QChar::ObjectReplacementCharacter));
		QCOMPARE(view.statusNote(), Delta::kFullMessagePlainNote);
	}

	void imageOnlyAllowedKeepsHtml() {
		Delta::FullMessageView view;
		view.setBody(
			u"<img src='"_s + DataPng() + u"'>"_s,
			kFallback);
		QVERIFY(!view.usedFallback());
		QVERIFY(view.loadedImageCount() >= 1);
		QCOMPARE(view.statusNote(), Delta::kFullMessageLimitedNote);
	}

	void setBodyResetsResourceBudget() {
		Delta::FullMessageView view;
		QVERIFY(view.fetchResource(
			int(QTextDocument::ImageResource),
			QUrl(DataPng())).isValid());
		QCOMPARE(view.loadedImageCount(), 1);
		view.setBody(kBody, kFallback);
		QCOMPARE(view.loadedImageCount(), 0);
		QVERIFY(view.deniedResources().isEmpty());
	}
};

QTEST_MAIN(FullMessageViewTests)
#include "full_message_view_tests.moc"
