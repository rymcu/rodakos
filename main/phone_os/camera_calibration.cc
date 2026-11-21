#include "phone_os/camera_calibration.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

namespace rodakos {
namespace {

constexpr int kDarkMean = 28;
constexpr int kBrightMean = 235;
// Below this contrast the region has no edge worth scoring.
constexpr int kMinContrast = 24;
// A change in region mean larger than this between frames means the scene moved.
constexpr int kMotionMeanDelta = 14;
constexpr int kMinChartRegion = 16;
// QR corners move a pixel or two between frames of a still chart; regions closer than this are
// treated as the same place.
constexpr int kRegionJitterPx = 4;
// Share of the region's pixels, strongest first, that the sharpness measure averages.
constexpr int kStrongestPercent = 10;
constexpr int kMinStrongestSamples = 8;

bool SimilarRegion(const CameraRegion& a, const CameraRegion& b) {
    return std::abs(a.x - b.x) <= kRegionJitterPx && std::abs(a.y - b.y) <= kRegionJitterPx &&
           std::abs(a.width - b.width) <= kRegionJitterPx &&
           std::abs(a.height - b.height) <= kRegionJitterPx;
}

double Distance(const CameraPoint& a, const CameraPoint& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

// Solves the 8x8 system for the homography mapping four source points to four destination points.
bool SolveHomography(const std::array<CameraPoint, 4>& src, const std::array<CameraPoint, 4>& dst,
                     std::array<double, 9>& h) {
    double a[8][9] = {};
    for (int i = 0; i < 4; ++i) {
        const double x = src[i].x, y = src[i].y, u = dst[i].x, v = dst[i].y;
        double* r0 = a[i * 2];
        double* r1 = a[i * 2 + 1];
        r0[0] = x; r0[1] = y; r0[2] = 1; r0[6] = -u * x; r0[7] = -u * y; r0[8] = u;
        r1[3] = x; r1[4] = y; r1[5] = 1; r1[6] = -v * x; r1[7] = -v * y; r1[8] = v;
    }
    for (int col = 0; col < 8; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 8; ++row) {
            if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) pivot = row;
        }
        if (std::fabs(a[pivot][col]) < 1e-9) return false;
        if (pivot != col) {
            for (int k = 0; k < 9; ++k) std::swap(a[pivot][k], a[col][k]);
        }
        for (int row = 0; row < 8; ++row) {
            if (row == col) continue;
            const double factor = a[row][col] / a[col][col];
            for (int k = col; k < 9; ++k) a[row][k] -= factor * a[col][k];
        }
    }
    for (int i = 0; i < 8; ++i) h[i] = a[i][8] / a[i][i];
    h[8] = 1.0;
    return true;
}

CameraPoint Apply(const std::array<double, 9>& h, double x, double y) {
    const double w = h[6] * x + h[7] * y + h[8];
    return {(h[0] * x + h[1] * y + h[2]) / w, (h[3] * x + h[4] * y + h[5]) / w};
}

}  // namespace

const CameraChartLayout* FindCameraChartLayout(const char* payload, size_t length) {
    for (const CameraChartLayout* layout : {&kCameraChartV1, &kCameraChartV2}) {
        if (length == std::strlen(layout->payload) && std::memcmp(payload, layout->payload, length) == 0) {
            return layout;
        }
    }
    return nullptr;
}

void CameraRgb565ToGray(const uint8_t* rgb565, int width, int height, int stride, uint8_t* gray) {
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = rgb565 + static_cast<size_t>(y) * stride;
        uint8_t* out = gray + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            const uint16_t pixel = static_cast<uint16_t>(row[x * 2]) |
                                   static_cast<uint16_t>(row[x * 2 + 1] << 8);
            const unsigned r = (pixel >> 11) & 0x1f;
            const unsigned g = (pixel >> 5) & 0x3f;
            const unsigned b = pixel & 0x1f;
            // Same weights as SetupQrDecoder: BT.601 luma with 5/6/5 expansion, white is 255.
            out[x] = static_cast<uint8_t>((r * 631u + g * 609u + b * 240u) >> 8);
        }
    }
}

void CameraCalibrationAnalyzer::Reset() {
    recent_.clear();
    best_ = -1;
    previous_mean_ = -1;
    previous_region_ = {};
    previous_on_chart_ = false;
    previous_layout_ = nullptr;
}

double CameraCalibrationAnalyzer::Sharpness(const uint8_t* gray, int stride,
                                            const CameraRegion& region, int* contrast,
                                            int* mean) {
    int lo = 255, hi = 0;
    uint64_t sum = 0;
    for (int y = region.y; y < region.y + region.height; ++y) {
        const uint8_t* row = gray + static_cast<size_t>(y) * stride;
        for (int x = region.x; x < region.x + region.width; ++x) {
            lo = std::min<int>(lo, row[x]);
            hi = std::max<int>(hi, row[x]);
            sum += row[x];
        }
    }
    const int count = region.width * region.height;
    if (contrast != nullptr) *contrast = hi - lo;
    if (mean != nullptr) *mean = count > 0 ? static_cast<int>(sum / count) : 0;
    if (region.width < 3 || region.height < 3 || hi <= lo) return 0.0;

    std::vector<uint32_t> energies;
    energies.reserve(static_cast<size_t>(region.width - 2) * (region.height - 2));
    for (int y = region.y + 1; y < region.y + region.height - 1; ++y) {
        const uint8_t* up = gray + static_cast<size_t>(y - 1) * stride;
        const uint8_t* mid = gray + static_cast<size_t>(y) * stride;
        const uint8_t* down = gray + static_cast<size_t>(y + 1) * stride;
        for (int x = region.x + 1; x < region.x + region.width - 1; ++x) {
            const int gx = (up[x + 1] + 2 * mid[x + 1] + down[x + 1]) -
                           (up[x - 1] + 2 * mid[x - 1] + down[x - 1]);
            const int gy = (down[x - 1] + 2 * down[x] + down[x + 1]) -
                           (up[x - 1] + 2 * up[x] + up[x + 1]);
            energies.push_back(static_cast<uint32_t>(gx * gx + gy * gy));
        }
    }
    // Average only the strongest gradients: the steepness of the edges, not their number.
    const size_t take = std::min(energies.size(),
                                 std::max<size_t>(kMinStrongestSamples,
                                                  energies.size() * kStrongestPercent / 100));
    std::nth_element(energies.begin(), energies.begin() + (take - 1), energies.end(),
                     std::greater<uint32_t>());
    double energy = 0.0;
    for (size_t i = 0; i < take; ++i) energy += energies[i];
    const double range = static_cast<double>(hi - lo);
    return energy / static_cast<double>(take) / (range * range);
}

int CameraCalibrationAnalyzer::ScoreFromSharpness(double sharpness) {
    // Measured on the rendered V2 chart (tests/camera_calibration), slanted edge / QR code area:
    //   ideal step edge 8.0 / 13.8, 3x3 box blur 4.7 / 6.3, 5x5 2.5 / 2.6, 7x7 1.3, 9x9 0.8.
    // Map 0.3..20 to 0..100 on a log scale: an ideal edge lands near 90, so a real camera does
    // not pin the bar at 100, and each step of blur moves it by a clearly visible amount.
    if (sharpness <= 0.0) return 0;
    constexpr double kLow = 0.3;
    constexpr double kHigh = 20.0;
    const double t = std::log(sharpness / kLow) / std::log(kHigh / kLow);
    return static_cast<int>(std::lround(std::clamp(t, 0.0, 1.0) * 100.0));
}

bool CameraCalibrationAnalyzer::ChartRegion(const CameraChartDetection& chart, int width,
                                            int height, CameraRegion& region) {
    if (!chart.found || chart.layout == nullptr) return false;
    const CameraChartLayout& layout = *chart.layout;
    // quirc corners follow the code's own grid. A mirrored image reverses their winding, so map
    // from the matching chart corners.
    const double half = layout.qr_size_mm / 2.0;
    std::array<CameraPoint, 4> sheet = {{{-half, -half}, {half, -half}, {half, half}, {-half, half}}};
    if (chart.mirrored) std::swap(sheet[1], sheet[3]);
    std::array<double, 9> h{};
    if (!SolveHomography(sheet, chart.corners, h)) return false;

    // The square is rotated 5 degrees; its left edge is the near-vertical slanted edge. Take a
    // band straddling the middle of that edge, which holds one dark-to-light transition.
    const double cx = layout.edge_center_x_mm;
    const double cy = layout.edge_center_y_mm;
    const double s = layout.edge_half_mm;
    const double band = s * 0.5;
    const CameraPoint corners_mm[4] = {
        {cx - s - band, cy - s * 0.6}, {cx - s + band, cy - s * 0.6},
        {cx - s + band, cy + s * 0.6}, {cx - s - band, cy + s * 0.6},
    };
    double min_x = 1e9, min_y = 1e9, max_x = -1e9, max_y = -1e9;
    for (const auto& p : corners_mm) {
        const CameraPoint q = Apply(h, p.x, p.y);
        if (!std::isfinite(q.x) || !std::isfinite(q.y)) return false;
        min_x = std::min(min_x, q.x); max_x = std::max(max_x, q.x);
        min_y = std::min(min_y, q.y); max_y = std::max(max_y, q.y);
    }
    region.x = static_cast<int>(std::floor(min_x));
    region.y = static_cast<int>(std::floor(min_y));
    region.width = static_cast<int>(std::ceil(max_x)) - region.x;
    region.height = static_cast<int>(std::ceil(max_y)) - region.y;
    if (region.width < kMinChartRegion || region.height < kMinChartRegion) return false;
    return region.x >= 0 && region.y >= 0 && region.x + region.width <= width &&
           region.y + region.height <= height;
}

CameraDistanceHint CameraCalibrationAnalyzer::DistanceHint(const CameraChartDetection& chart) {
    if (!chart.found || chart.layout == nullptr) return CameraDistanceHint::kUnknown;
    const double width = (Distance(chart.corners[0], chart.corners[1]) +
                          Distance(chart.corners[3], chart.corners[2])) / 2.0;
    if (width < chart.layout->good_min_qr_px) return CameraDistanceHint::kCloser;
    if (width > chart.layout->good_max_qr_px) return CameraDistanceHint::kFurther;
    return CameraDistanceHint::kGood;
}

CameraCalibrationResult CameraCalibrationAnalyzer::Analyze(const uint8_t* gray, int width,
                                                           int height, int stride,
                                                           const CameraChartDetection& chart,
                                                           uint32_t sequence) {
    CameraCalibrationResult result;
    result.sequence = sequence;
    result.chart = chart;
    result.distance = DistanceHint(chart);

    CameraRegion region;
    // A mirrored chart is reported to the user; its score would measure the wrong edge layout.
    result.on_chart = !chart.mirrored && ChartRegion(chart, width, height, region);
    if (!result.on_chart) {
        region.width = std::min(kCenterRegionWidth, width);
        region.height = std::min(kCenterRegionHeight, height);
        region.x = (width - region.width) / 2;
        region.y = (height - region.height) / 2;
    }
    result.region = region;

    int contrast = 0, mean = 0;
    const double sharpness = Sharpness(gray, stride, region, &contrast, &mean);
    const CameraChartLayout* layout = result.on_chart ? chart.layout : nullptr;
    const bool same_target = result.on_chart == previous_on_chart_ && layout == previous_layout_;
    const bool same_region = same_target && SimilarRegion(region, previous_region_);
    const bool moving = previous_mean_ >= 0 && same_region &&
                        std::abs(mean - previous_mean_) > kMotionMeanDelta;
    previous_mean_ = mean;
    previous_region_ = region;
    previous_on_chart_ = result.on_chart;
    previous_layout_ = layout;
    // A different target (frame centre vs. chart edge) is a different scale: start over, best too.
    if (!same_target) {
        recent_.clear();
        best_ = -1;
    } else if (!same_region) {
        // Same target, but the chart moved: the window restarts, the best value stays.
        recent_.clear();
    }

    if (mean < kDarkMean) result.issue = CameraFrameIssue::kTooDark;
    else if (mean > kBrightMean) result.issue = CameraFrameIssue::kOverexposed;
    // A region with no detail cannot be scored whatever else changed, so it wins over motion.
    else if (contrast < kMinContrast) result.issue = CameraFrameIssue::kFlat;
    else if (moving) result.issue = CameraFrameIssue::kMoving;
    if (chart.mirrored) result.issue = CameraFrameIssue::kNone;

    if (result.issue == CameraFrameIssue::kNone && !chart.mirrored) {
        recent_.push_back(ScoreFromSharpness(sharpness));
        while (recent_.size() > kMedianWindow) recent_.pop_front();
    }
    if (!recent_.empty() && !chart.mirrored) {
        std::vector<int> sorted(recent_.begin(), recent_.end());
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        result.score = sorted[sorted.size() / 2];
        // Only a full window counts toward the best value, so one lucky frame cannot set it.
        if (recent_.size() == kMedianWindow && result.issue == CameraFrameIssue::kNone) {
            best_ = std::max(best_, result.score);
        }
    }
    result.best = best_;
    return result;
}

}  // namespace rodakos
