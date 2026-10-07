#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QBuffer>
#include <QDebug>
#include <cstdio>
int main(int argc, char **argv) {
 QCoreApplication app(argc, argv);
 if (argc != 2) { qCritical() << "Usage: test-avatar-codecs PLUGIN_DIRECTORY"; return 3; }
 app.setLibraryPaths({QString::fromLocal8Bit(argv[1])});
 std::printf("JPEG read: %d, write: %d\n",
     QImageReader::supportedImageFormats().contains("jpeg"),
     QImageWriter::supportedImageFormats().contains("jpeg"));
 QImage input(64,64,QImage::Format_RGB32); input.fill(Qt::red);
 QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly);
 if (!input.save(&output,"JPG",90)) { std::puts("FAIL: avatar JPEG save"); return 1; }
 QImage decoded = QImage::fromData(bytes,"JPG");
 if (decoded.isNull() || decoded.size()!=input.size()) { std::puts("FAIL: avatar JPEG read"); return 2; }
 std::puts("PASS: packaged runtime avatar JPEG save and read");
}
