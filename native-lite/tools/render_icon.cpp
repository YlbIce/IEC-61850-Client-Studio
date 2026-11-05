// Build-time only: generates PNG and multi-resolution Windows ICO from app.svg.
#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QSvgRenderer>
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2)
        return 1;
    QString dir = QString::fromLocal8Bit(argv[1]);
    QSvgRenderer renderer(dir + "/app.svg");
    if (!renderer.isValid())
        return 2;
    const QList<int> sizes{16, 24, 32, 48, 64, 128, 256};
    QList<QByteArray> frames;
    for (int size : sizes) {
        QImage image(size, size, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            renderer.render(&painter);
        }
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        frames << data;
        if (size == 256 && !image.save(dir + "/app.png"))
            return 3;
    }
    QFile file(dir + "/app.ico");
    if (!file.open(QIODevice::WriteOnly))
        return 4;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + sizes.size() * 16;
    for (int i = 0; i < sizes.size(); ++i) {
        stream << quint8(sizes[i] == 256 ? 0 : sizes[i]) << quint8(sizes[i] == 256 ? 0 : sizes[i])
               << quint8(0) << quint8(0) << quint16(1) << quint16(32) << quint32(frames[i].size()) << offset;
        offset += frames[i].size();
    }
    for (const auto& data : frames)
        stream.writeRawData(data.constData(), data.size());
    return stream.status() == QDataStream::Ok ? 0 : 5;
}
