#include "test_framework.h"

#include "phone_os/camera_calibration.h"
#include "phone_os/setup_qr_decoder.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using rodakos::CameraCalibrationAnalyzer;
using rodakos::CameraChartDetection;
using rodakos::CameraDistanceHint;
using rodakos::CameraFrameIssue;
using rodakos::SetupQrDecoder;

struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

GrayImage LoadPgm(const std::string& name) {
    const std::string path = std::string(RODAK_CALIBRATION_FIXTURES) + "/" + name;
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("missing fixture " + path);
    std::string magic;
    int max_value = 0;
    GrayImage image;
    file >> magic >> image.width >> image.height >> max_value;
    file.get();
    if (magic != "P5" || max_value != 255) throw std::runtime_error("unsupported fixture " + path);
    image.pixels.resize(static_cast<size_t>(image.width) * image.height);
    file.read(reinterpret_cast<char*>(image.pixels.data()), static_cast<std::streamsize>(image.pixels.size()));
    if (!file) throw std::runtime_error("truncated fixture " + path);
    return image;
}

// The same path the calibration task takes: decode the QR code, then hand its corners over.
CameraChartDetection Detect(SetupQrDecoder& decoder, const GrayImage& image) {
    CameraChartDetection detection;
    std::string payload;
    SetupQrDecoder::Location location;
    if (decoder.DecodeGray(image.pixels.data(), image.width, image.height, image.width, payload,
                           location) &&
        rodakos::FindCameraChartLayout(payload.data(), payload.size()) != nullptr) {
        detection.found = true;
        detection.layout = rodakos::FindCameraChartLayout(payload.data(), payload.size());
        detection.mirrored = location.mirrored;
        for (int i = 0; i < 4; ++i) {
            detection.corners[i] = {static_cast<double>(location.corner_x[i]),
                                    static_cast<double>(location.corner_y[i])};
        }
    }
    return detection;
}

rodakos::CameraCalibrationResult SettledResult(CameraCalibrationAnalyzer& analyzer, const GrayImage& image,
                                               const CameraChartDetection& chart) {
    rodakos::CameraCalibrationResult result;
    for (uint32_t frame = 1; frame <= CameraCalibrationAnalyzer::kMedianWindow; ++frame) {
        result = analyzer.Analyze(image.pixels.data(), image.width, image.height, image.width, chart, frame);
    }
    return result;
}

}  // namespace

RODAK_TEST("Calibration finds the landscape chart and measures on its slanted edge") {
    const GrayImage image = LoadPgm("chart_v2_sharp_320x240.pgm");
    SetupQrDecoder decoder;
    const CameraChartDetection chart = Detect(decoder, image);
    RODAK_CHECK(chart.found);
    RODAK_CHECK_FALSE(chart.mirrored);
    RODAK_CHECK(chart.layout == &rodakos::kCameraChartV2);

    CameraCalibrationAnalyzer analyzer;
    const auto result = SettledResult(analyzer, image, chart);
    RODAK_CHECK(result.on_chart);
    RODAK_CHECK(result.issue == CameraFrameIssue::kNone);
    // The region sits right of the QR code, on the square's left edge, at the code's height.
    RODAK_CHECK(result.region.x > static_cast<int>(chart.corners[1].x));
    RODAK_CHECK(result.region.y > static_cast<int>(chart.corners[1].y));
    RODAK_CHECK(result.region.y + result.region.height < static_cast<int>(chart.corners[2].y));
    RODAK_CHECK(result.distance == CameraDistanceHint::kGood);
    RODAK_CHECK(result.score > 0);
    RODAK_CHECK_EQ(result.best, result.score);
}

RODAK_TEST("Calibration still reads the portrait chart printed earlier") {
    const GrayImage image = LoadPgm("chart_v1_sharp_320x240.pgm");
    SetupQrDecoder decoder;
    const CameraChartDetection chart = Detect(decoder, image);
    RODAK_CHECK(chart.found);
    RODAK_CHECK(chart.layout == &rodakos::kCameraChartV1);

    CameraCalibrationAnalyzer analyzer;
    const auto result = SettledResult(analyzer, image, chart);
    RODAK_CHECK(result.on_chart);
    // V1 has the square below the QR code.
    RODAK_CHECK(result.region.y > static_cast<int>(chart.corners[3].y));
    RODAK_CHECK(result.region.x < static_cast<int>(chart.corners[3].x));
    RODAK_CHECK(result.distance == CameraDistanceHint::kGood);
    RODAK_CHECK(result.score > 0);
}

RODAK_TEST("Calibration scores a sharp chart above a blurred one") {
    const GrayImage sharp = LoadPgm("chart_v2_sharp_320x240.pgm");
    const GrayImage blur = LoadPgm("chart_v2_blur_320x240.pgm");
    SetupQrDecoder decoder;
    const CameraChartDetection chart = Detect(decoder, sharp);
    RODAK_CHECK(chart.found);

    CameraCalibrationAnalyzer sharp_analyzer;
    CameraCalibrationAnalyzer blur_analyzer;
    // The same placement for both, as a user holding still while the focus changes.
    const int sharp_score = SettledResult(sharp_analyzer, sharp, chart).score;
    const int blur_score = SettledResult(blur_analyzer, blur, chart).score;
    RODAK_CHECK(sharp_score - blur_score >= 20);
}

RODAK_TEST("Calibration reports a mirrored image and does not score it") {
    const GrayImage image = LoadPgm("chart_v2_mirrored_320x240.pgm");
    SetupQrDecoder decoder;
    const CameraChartDetection chart = Detect(decoder, image);
    RODAK_CHECK(chart.found);
    RODAK_CHECK(chart.mirrored);

    CameraCalibrationAnalyzer analyzer;
    const auto result = SettledResult(analyzer, image, chart);
    RODAK_CHECK(result.chart.mirrored);
    RODAK_CHECK_FALSE(result.on_chart);
    RODAK_CHECK_EQ(result.score, -1);
    RODAK_CHECK_EQ(result.best, -1);
}

RODAK_TEST("Calibration without a chart measures the frame centre") {
    const GrayImage image = LoadPgm("chart_v2_sharp_320x240.pgm");
    CameraCalibrationAnalyzer analyzer;
    const auto result = SettledResult(analyzer, image, CameraChartDetection{});
    RODAK_CHECK_FALSE(result.on_chart);
    RODAK_CHECK_EQ(result.region.width, CameraCalibrationAnalyzer::kCenterRegionWidth);
    RODAK_CHECK_EQ(result.region.x, (320 - CameraCalibrationAnalyzer::kCenterRegionWidth) / 2);
    RODAK_CHECK(result.distance == CameraDistanceHint::kUnknown);
    RODAK_CHECK(result.score >= 0);
}

RODAK_TEST("Calibration rejects dark, overexposed, flat and moving frames") {
    CameraCalibrationAnalyzer analyzer;
    const CameraChartDetection none;
    std::vector<uint8_t> dark(320 * 240, 8);
    auto result = analyzer.Analyze(dark.data(), 320, 240, 320, none, 1);
    RODAK_CHECK(result.issue == CameraFrameIssue::kTooDark);
    RODAK_CHECK_EQ(result.score, -1);

    std::vector<uint8_t> bright(320 * 240, 250);
    result = analyzer.Analyze(bright.data(), 320, 240, 320, none, 2);
    RODAK_CHECK(result.issue == CameraFrameIssue::kOverexposed);

    std::vector<uint8_t> flat(320 * 240, 128);
    result = analyzer.Analyze(flat.data(), 320, 240, 320, none, 3);
    RODAK_CHECK(result.issue == CameraFrameIssue::kFlat);
    RODAK_CHECK_EQ(result.score, -1);

    const GrayImage image = LoadPgm("chart_v2_sharp_320x240.pgm");
    analyzer.Reset();
    analyzer.Analyze(image.pixels.data(), 320, 240, 320, none, 4);
    std::vector<uint8_t> shifted = image.pixels;
    for (auto& value : shifted) value = static_cast<uint8_t>(std::min(255, value + 40));
    result = analyzer.Analyze(shifted.data(), 320, 240, 320, none, 5);
    RODAK_CHECK(result.issue == CameraFrameIssue::kMoving);
}

RODAK_TEST("Calibration best value needs a full window and survives a bad frame") {
    const GrayImage image = LoadPgm("chart_v2_sharp_320x240.pgm");
    CameraCalibrationAnalyzer analyzer;
    const CameraChartDetection none;
    auto result = analyzer.Analyze(image.pixels.data(), 320, 240, 320, none, 1);
    RODAK_CHECK(result.score >= 0);
    RODAK_CHECK_EQ(result.best, -1);
    result = SettledResult(analyzer, image, none);
    const int best = result.best;
    RODAK_CHECK(best >= 0);
    std::vector<uint8_t> dark(320 * 240, 8);
    result = analyzer.Analyze(dark.data(), 320, 240, 320, none, 99);
    RODAK_CHECK_EQ(result.best, best);
    analyzer.Reset();
    result = analyzer.Analyze(image.pixels.data(), 320, 240, 320, none, 100);
    RODAK_CHECK_EQ(result.best, -1);
}

RODAK_TEST("Calibration distance hint follows the QR code size") {
    CameraChartDetection chart;
    chart.found = true;
    chart.layout = &rodakos::kCameraChartV2;
    auto square = [&](double size) {
        chart.corners = {{{100, 50}, {100 + size, 50}, {100 + size, 50 + size}, {100, 50 + size}}};
        return CameraCalibrationAnalyzer::DistanceHint(chart);
    };
    RODAK_CHECK(square(40) == CameraDistanceHint::kCloser);
    RODAK_CHECK(square(88) == CameraDistanceHint::kGood);
    RODAK_CHECK(square(150) == CameraDistanceHint::kFurther);
}

RODAK_TEST("Calibration score scale is monotonic and clamped") {
    RODAK_CHECK_EQ(CameraCalibrationAnalyzer::ScoreFromSharpness(0.0), 0);
    RODAK_CHECK_EQ(CameraCalibrationAnalyzer::ScoreFromSharpness(40.0), 100);
    RODAK_CHECK(CameraCalibrationAnalyzer::ScoreFromSharpness(1.0) <
                CameraCalibrationAnalyzer::ScoreFromSharpness(5.0));
}

RODAK_TEST("Calibration recognises only the known chart payloads") {
    const std::string v1 = "RODAKCAL:V1";
    const std::string v2 = "RODAKCAL:V2";
    const std::string other = "RODAKCAL:V9";
    RODAK_CHECK(rodakos::FindCameraChartLayout(v1.data(), v1.size()) == &rodakos::kCameraChartV1);
    RODAK_CHECK(rodakos::FindCameraChartLayout(v2.data(), v2.size()) == &rodakos::kCameraChartV2);
    RODAK_CHECK(rodakos::FindCameraChartLayout(other.data(), other.size()) == nullptr);
    RODAK_CHECK(rodakos::FindCameraChartLayout(v2.data(), v2.size() - 1) == nullptr);
}

RODAK_TEST("Calibration best value starts over when the measured target changes") {
    const GrayImage image = LoadPgm("chart_v2_sharp_320x240.pgm");
    SetupQrDecoder decoder;
    const CameraChartDetection chart = Detect(decoder, image);
    RODAK_CHECK(chart.found);

    CameraCalibrationAnalyzer analyzer;
    // The QR code sits in the frame centre: a dense, high-contrast patch that scores high.
    const int centre_best = SettledResult(analyzer, image, CameraChartDetection{}).best;
    RODAK_CHECK(centre_best >= 0);
    // Then the chart is recognised and the region moves to the slanted edge. The best value must
    // describe the edge only, not carry the centre's score over.
    auto result = analyzer.Analyze(image.pixels.data(), 320, 240, 320, chart, 100);
    RODAK_CHECK(result.on_chart);
    RODAK_CHECK_EQ(result.best, -1);
    result = SettledResult(analyzer, image, chart);
    RODAK_CHECK_EQ(result.best, result.score);
}

RODAK_TEST("Calibration keeps its window while QR corners jitter by a pixel") {
    const GrayImage image = LoadPgm("chart_v2_sharp_320x240.pgm");
    SetupQrDecoder decoder;
    CameraChartDetection chart = Detect(decoder, image);
    RODAK_CHECK(chart.found);
    CameraCalibrationAnalyzer analyzer;
    rodakos::CameraCalibrationResult result;
    for (uint32_t frame = 1; frame <= 6; ++frame) {
        CameraChartDetection jittered = chart;
        for (auto& corner : jittered.corners) corner.x += (frame % 2 == 0) ? 1.0 : 0.0;
        result = analyzer.Analyze(image.pixels.data(), 320, 240, 320, jittered, frame);
    }
    RODAK_CHECK(result.best >= 0);
    RODAK_CHECK_EQ(result.best, result.score);
}

RODAK_TEST("Calibration does not saturate on dense detail") {
    const GrayImage image = LoadPgm("chart_v2_sharp_320x240.pgm");
    CameraCalibrationAnalyzer analyzer;
    // Even the QR code, the densest patch on the chart, stays below the top of the scale.
    const auto result = SettledResult(analyzer, image, CameraChartDetection{});
    RODAK_CHECK(result.score < 100);
}
