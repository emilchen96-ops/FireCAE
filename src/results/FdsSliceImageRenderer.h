#pragma once

#include "results/FdsSliceReader.h"

#include <QImage>
#include <QString>

struct FdsSliceImageOptions
{
    int frameIndex = 0;
    bool useGlobalRange = true;
    bool useManualRange = false;
    float minimum = 0.0F;
    float maximum = 1.0F;
    int opacity = 255;
};

struct FdsSliceImageResult
{
    QImage image;
    float minimum = 0.0F;
    float maximum = 1.0F;
    QString horizontalAxis;
    QString verticalAxis;
    QString errorMessage;

    bool success() const { return errorMessage.isEmpty() && !image.isNull(); }
};

class FdsSliceImageRenderer final
{
public:
    static FdsSliceImageResult render(const FdsSliceData& data,
                                      const FdsSliceImageOptions& options = {});
};
