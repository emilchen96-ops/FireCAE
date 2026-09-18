#include "results/FdsSliceReader.h"
#include "results/FdsSliceImageRenderer.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <algorithm>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("FireCAESliceInspector"));
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Inspect classic uncompressed FDS SLCF binary files."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("slice-file"),
                                 QStringLiteral("FDS .sf file to inspect."));
    const QCommandLineOption frameOption(
        {QStringLiteral("f"), QStringLiteral("frame")},
        QStringLiteral("Frame index used for PNG export."),
        QStringLiteral("index"), QStringLiteral("0"));
    const QCommandLineOption imageOption(
        {QStringLiteral("o"), QStringLiteral("output-image")},
        QStringLiteral("Optional native PNG color-map output."),
        QStringLiteral("file"));
    const QCommandLineOption scaleOption(
        QStringLiteral("scale"),
        QStringLiteral("Integer PNG pixel scale (nearest-neighbour)."),
        QStringLiteral("factor"), QStringLiteral("1"));
    parser.addOptions({frameOption, imageOption, scaleOption});
    parser.process(application);
    QTextStream output(stdout);
    QTextStream errors(stderr);
    const QStringList arguments = parser.positionalArguments();
    if (arguments.size() != 1) {
        parser.showHelp(2);
    }
    const FdsSliceData data = FdsSliceReader::read(arguments.constFirst());
    if (!data.success()) {
        errors << data.errorMessage << '\n';
        return 3;
    }
    QJsonObject root;
    root.insert(QStringLiteral("file"), data.filePath);
    root.insert(QStringLiteral("format"),
                QStringLiteral("classic-uncompressed-fortran-record"));
    root.insert(QStringLiteral("byteOrder"),
                data.littleEndian ? QStringLiteral("little-endian")
                                  : QStringLiteral("big-endian"));
    root.insert(QStringLiteral("longLabel"), data.longLabel);
    root.insert(QStringLiteral("shortLabel"), data.shortLabel);
    root.insert(QStringLiteral("unit"), data.unit);
    root.insert(QStringLiteral("nx"), data.nx);
    root.insert(QStringLiteral("ny"), data.ny);
    root.insert(QStringLiteral("nz"), data.nz);
    root.insert(QStringLiteral("planar"), data.isPlanar());
    root.insert(QStringLiteral("frameCount"), data.frames.size());
    if (!data.frames.isEmpty()) {
        root.insert(QStringLiteral("startTime"), data.frames.constFirst().time);
        root.insert(QStringLiteral("endTime"), data.frames.constLast().time);
        float minimum = data.frames.constFirst().minimum;
        float maximum = data.frames.constFirst().maximum;
        for (const FdsSliceFrame& frame : data.frames) {
            minimum = std::min(minimum, frame.minimum);
            maximum = std::max(maximum, frame.maximum);
        }
        root.insert(QStringLiteral("minimum"), minimum);
        root.insert(QStringLiteral("maximum"), maximum);
    }
    root.insert(QStringLiteral("warnings"),
                QJsonArray::fromStringList(data.warnings));
    if (parser.isSet(imageOption)) {
        bool frameOk = false;
        const int frameIndex = parser.value(frameOption).toInt(&frameOk);
        if (!frameOk) {
            errors << "Frame index is not an integer.\n";
            return 4;
        }
        FdsSliceImageOptions options;
        options.frameIndex = frameIndex;
        const FdsSliceImageResult image =
            FdsSliceImageRenderer::render(data, options);
        if (!image.success()) {
            errors << image.errorMessage << '\n';
            return 5;
        }
        bool scaleOk = false;
        const int scale = parser.value(scaleOption).toInt(&scaleOk);
        if (!scaleOk || scale < 1 || scale > 100) {
            errors << "PNG scale must be an integer from 1 to 100.\n";
            return 6;
        }
        const QImage outputImage = scale == 1
            ? image.image
            : image.image.scaled(image.image.width() * scale,
                                 image.image.height() * scale,
                                 Qt::IgnoreAspectRatio,
                                 Qt::FastTransformation);
        if (!outputImage.save(parser.value(imageOption), "PNG")) {
            errors << "PNG image could not be written.\n";
            return 7;
        }
        root.insert(QStringLiteral("outputImage"), parser.value(imageOption));
        root.insert(QStringLiteral("horizontalAxis"), image.horizontalAxis);
        root.insert(QStringLiteral("verticalAxis"), image.verticalAxis);
        root.insert(QStringLiteral("renderMinimum"), image.minimum);
        root.insert(QStringLiteral("renderMaximum"), image.maximum);
    }
    output << QJsonDocument(root).toJson(QJsonDocument::Indented);
    return 0;
}
