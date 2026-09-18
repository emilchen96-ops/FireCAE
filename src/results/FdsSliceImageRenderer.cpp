#include "results/FdsSliceImageRenderer.h"

#include <QColor>

#include <algorithm>
#include <cmath>
#include <limits>
#include <iterator>

namespace
{
QColor fieldColor(float fraction, int opacity)
{
    const float value = std::clamp(fraction, 0.0F, 1.0F);
    struct Stop { float position; int red; int green; int blue; };
    static constexpr Stop stops[] = {
        {0.00F, 13, 8, 135}, {0.18F, 38, 65, 178},
        {0.36F, 21, 135, 191}, {0.52F, 35, 190, 140},
        {0.68F, 143, 213, 69}, {0.84F, 245, 202, 39},
        {1.00F, 190, 24, 29}};
    for (std::size_t index = 1; index < std::size(stops); ++index) {
        if (value > stops[index].position) continue;
        const Stop& left = stops[index - 1];
        const Stop& right = stops[index];
        const float span = right.position - left.position;
        const float t = span > 0.0F ? (value - left.position) / span : 0.0F;
        const auto mix = [t](int a, int b) {
            return static_cast<int>(std::lround(a + t * (b - a)));
        };
        return QColor(mix(left.red, right.red),
                      mix(left.green, right.green),
                      mix(left.blue, right.blue),
                      std::clamp(opacity, 0, 255));
    }
    return QColor(stops[std::size(stops) - 1].red,
                  stops[std::size(stops) - 1].green,
                  stops[std::size(stops) - 1].blue,
                  std::clamp(opacity, 0, 255));
}
}

FdsSliceImageResult FdsSliceImageRenderer::render(
    const FdsSliceData& data, const FdsSliceImageOptions& options)
{
    FdsSliceImageResult result;
    if (!data.success()) {
        result.errorMessage = QStringLiteral("Slice data is invalid: %1")
                                  .arg(data.errorMessage);
        return result;
    }
    if (!data.isPlanar()) {
        result.errorMessage = QStringLiteral(
            "Only planar SLCF data can be rendered as a 2D image.");
        return result;
    }
    if (options.frameIndex < 0 || options.frameIndex >= data.frames.size()) {
        result.errorMessage = QStringLiteral("Slice frame index is outside the available range.");
        return result;
    }
    const FdsSliceFrame& frame = data.frames.at(options.frameIndex);
    result.minimum = frame.minimum;
    result.maximum = frame.maximum;
    if (options.useGlobalRange) {
        result.minimum = std::numeric_limits<float>::max();
        result.maximum = std::numeric_limits<float>::lowest();
        for (const FdsSliceFrame& candidate : data.frames) {
            result.minimum = std::min(result.minimum, candidate.minimum);
            result.maximum = std::max(result.maximum, candidate.maximum);
        }
    }
    if (options.useManualRange) {
        result.minimum = options.minimum;
        result.maximum = options.maximum;
    }
    if (!std::isfinite(result.minimum) || !std::isfinite(result.maximum) ||
        result.maximum <= result.minimum) {
        const float center = std::isfinite(result.minimum) ? result.minimum : 0.0F;
        result.minimum = center - 0.5F;
        result.maximum = center + 0.5F;
    }

    int width = 0;
    int height = 0;
    enum class Plane { X, Y, Z } plane = Plane::Z;
    if (data.nx == 1) {
        plane = Plane::X;
        width = data.ny;
        height = data.nz;
        result.horizontalAxis = QStringLiteral("Y");
        result.verticalAxis = QStringLiteral("Z");
    } else if (data.ny == 1) {
        plane = Plane::Y;
        width = data.nx;
        height = data.nz;
        result.horizontalAxis = QStringLiteral("X");
        result.verticalAxis = QStringLiteral("Z");
    } else {
        plane = Plane::Z;
        width = data.nx;
        height = data.ny;
        result.horizontalAxis = QStringLiteral("X");
        result.verticalAxis = QStringLiteral("Y");
    }
    if (width <= 0 || height <= 0) {
        result.errorMessage = QStringLiteral("Slice image dimensions are invalid.");
        return result;
    }
    result.image = QImage(width, height, QImage::Format_ARGB32);
    const float span = result.maximum - result.minimum;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float fieldValue = 0.0F;
            switch (plane) {
            case Plane::X:
                fieldValue = data.value(options.frameIndex, 0, x, height - 1 - y);
                break;
            case Plane::Y:
                fieldValue = data.value(options.frameIndex, x, 0, height - 1 - y);
                break;
            case Plane::Z:
                fieldValue = data.value(options.frameIndex, x, height - 1 - y, 0);
                break;
            }
            const float fraction = (fieldValue - result.minimum) / span;
            result.image.setPixelColor(x, y, fieldColor(fraction, options.opacity));
        }
    }
    return result;
}
