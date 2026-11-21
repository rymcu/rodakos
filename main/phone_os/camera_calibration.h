#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace rodakos {

// Camera calibration measurement, free of ESP-IDF and LVGL so host tests feed it fixtures.
//
// The GC0308 on BigSmart has a fixed-focus lens, so this measures; it does not focus. It scores
// how sharp a region of the frame is, finds the RodakOS calibration chart by its QR code, tells
// whether the image is mirrored, and suggests moving closer or further away.
//
// Sharpness is the mean of the strongest 10% squared Sobel gradients in the region, divided by the
// squared contrast (max - min luma) of that region. Using only the strongest gradients measures how
// steep the edges are rather than how many there are, so dense text or a QR code does not saturate
// the score. Dividing by contrast squared cancels a change in exposure or gain, which scales both.
// The score is only meaningful compared with other frames of the same target; switching between
// the frame centre and the chart's edge starts the best value again.

// A printed calibration chart (tools/camera_calibration_chart.py). Units are millimetres on the
// sheet, measured from the centre of the QR code body; +x is right, +y is down the page. The dark
// square is rotated 5 degrees; its left edge is the slanted edge that is measured.
struct CameraChartLayout {
    const char* payload;
    double qr_size_mm;
    double edge_center_x_mm;
    double edge_center_y_mm;
    double edge_half_mm;
    // QR code widths (pixels in a 320x240 frame) between which the distance is reported as good:
    // the code decodes reliably and the whole chart fits in the frame.
    double good_min_qr_px;
    double good_max_qr_px;
};

// V1: A4 portrait, 90 mm code with the square below it. V2: A4 landscape, 100 mm code with the
// square to its right, which matches the 4:3 frame better and gives a larger code.
inline constexpr CameraChartLayout kCameraChartV1 = {"RODAKCAL:V1", 90.0, 0.0, 102.0, 32.0, 60.0, 130.0};
inline constexpr CameraChartLayout kCameraChartV2 = {"RODAKCAL:V2", 100.0, 130.0, 0.0, 35.0, 64.0, 112.0};

// The layout a decoded QR payload names, or nullptr when it is not a calibration chart.
const CameraChartLayout* FindCameraChartLayout(const char* payload, size_t length);

struct CameraPoint {
    double x = 0;
    double y = 0;
};

// One located calibration chart: the QR corners from quirc (top-left, top-right, bottom-right,
// bottom-left in the code's own frame) and whether it only decoded after a flip.
struct CameraChartDetection {
    bool found = false;
    bool mirrored = false;
    // Which chart was read; set whenever `found` is true.
    const CameraChartLayout* layout = nullptr;
    std::array<CameraPoint, 4> corners{};
};

struct CameraRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

enum class CameraFrameIssue : uint8_t {
    kNone,
    kTooDark,
    kOverexposed,
    kMoving,
    kFlat,  // Not enough detail in the region to score.
};

enum class CameraDistanceHint : uint8_t {
    kUnknown,
    kCloser,
    kFurther,
    kGood,
};

struct CameraCalibrationResult {
    // Median of the recent valid scores; negative while there is none yet.
    int score = -1;
    // Best median seen since the last Reset(); negative until the first valid score.
    int best = -1;
    CameraFrameIssue issue = CameraFrameIssue::kNone;
    CameraRegion region;
    // True when the region was placed on the chart's slanted edge instead of the frame centre.
    bool on_chart = false;
    CameraChartDetection chart;
    CameraDistanceHint distance = CameraDistanceHint::kUnknown;
    uint32_t sequence = 0;
};

class CameraCalibrationAnalyzer {
public:
    static constexpr int kCenterRegionWidth = 80;
    static constexpr int kCenterRegionHeight = 60;
    static constexpr size_t kMedianWindow = 5;
    void Reset();

    // `gray` is 8-bit luma, `stride` in bytes. `chart` comes from the QR decode of the same frame
    // (found=false when no chart was read). Bad frames do not change the score or the best value.
    CameraCalibrationResult Analyze(const uint8_t* gray, int width, int height, int stride,
                                    const CameraChartDetection& chart, uint32_t sequence);

    // Exposed for tests.
    static double Sharpness(const uint8_t* gray, int stride, const CameraRegion& region,
                            int* contrast = nullptr, int* mean = nullptr);
    static bool ChartRegion(const CameraChartDetection& chart, int width, int height,
                            CameraRegion& region);
    static CameraDistanceHint DistanceHint(const CameraChartDetection& chart);
    // Maps the normalised gradient energy to 0..100 on a log scale.
    static int ScoreFromSharpness(double sharpness);

private:
    std::deque<int> recent_;
    int best_ = -1;
    // What is being measured: the frame centre (nullptr) or a chart edge (its layout). The window
    // and the best value only compare frames of the same target.
    bool previous_on_chart_ = false;
    const CameraChartLayout* previous_layout_ = nullptr;
    // Mean luma of the previous region, for motion detection; negative when unknown.
    int previous_mean_ = -1;
    CameraRegion previous_region_;
};

// Converts little-endian RGB565 (as CameraService delivers it) to 8-bit luma.
void CameraRgb565ToGray(const uint8_t* rgb565, int width, int height, int stride, uint8_t* gray);

}  // namespace rodakos
