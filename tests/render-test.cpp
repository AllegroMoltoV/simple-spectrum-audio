// Layer 2 of the GUI tests: the picture itself. Feeds a fixed input to the analyzer, draws the view without a window,
// and compares the pixels with the golden image. Two deliberately broken pictures, a tone at the wrong frequency and
// a missing curve, must fail the same comparison.
//
// Usage: render-test <golden.png> <output directory>
// The output directory receives the drawn image, the broken images and the difference images, for review.
#include <visage_graphics/canvas.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "spectrum-analyzer.h"
#include "spectrum-view.h"

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 320;
constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;
// A pixel differs when one of its channels differs by more than this. Different GPUs and drivers drew the stage A
// prototype within 1 level of each other.
constexpr int kChannelTolerance = 4;
// The comparison fails when more than this share of the pixels differ.
constexpr double kMaxDifferingShare = 0.002;

int failures = 0;

void check(bool ok, const char *what, double value = 0.0)
{
	std::printf("%s %s (value %.6g)\n", ok ? "ok  " : "FAIL", what, value);
	if (!ok)
		++failures;
}

// The fixed input: a -12 dBFS tone and pink noise with a fixed seed, two seconds long.
std::vector<float> input(double toneHz)
{
	std::vector<float> x(size_t(kRate * 2));
	uint32_t seed = 11;
	double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
	for (size_t i = 0; i < x.size(); ++i) {
		seed ^= seed << 13;
		seed ^= seed >> 17;
		seed ^= seed << 5;
		const double white = double(seed >> 8) / double(1 << 24) * 2.0 - 1.0;
		b0 = 0.99886 * b0 + white * 0.0555179;
		b1 = 0.99332 * b1 + white * 0.0750759;
		b2 = 0.96900 * b2 + white * 0.1538520;
		b3 = 0.86650 * b3 + white * 0.3104856;
		b4 = 0.55000 * b4 + white * 0.5329522;
		b5 = -0.7616 * b5 - white * 0.0168980;
		const double pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362) * 0.02;
		b6 = white * 0.115926;
		x[i] = float(pink + 0.25 * std::sin(2.0 * kPi * toneHz * double(i) / kRate));
	}
	return x;
}

std::vector<float> levels(double toneHz)
{
	ssp::SpectrumAnalyzer analyzer;
	analyzer.setSampleRate(kRate);
	analyzer.setSettings(ssp::DisplaySettings{});
	const auto x = input(toneHz);
	// One update per 800 samples, as a 60 Hz display would at 48 kHz.
	for (size_t at = 0; at < x.size(); at += 800) {
		analyzer.push(x.data() + at, std::min<size_t>(800, x.size() - at));
		analyzer.update();
	}
	return analyzer.columns(ssp::plotArea(kWidth, kHeight).width);
}

visage::Screenshot render(const std::vector<float> &values)
{
	visage::Canvas canvas;
	canvas.setWindowless(kWidth, kHeight);
	ssp::drawSpectrum(canvas, kWidth, kHeight, values, ssp::DisplaySettings{}.floorDb);
	canvas.submit();
	return canvas.takeScreenshot();
}

struct Comparison {
	size_t differing = 0;
	int maxChannel = 0;
	visage::Screenshot difference;
};

// Pixels that differ are painted red on a dimmed copy of the expected image.
Comparison compare(const visage::Screenshot &actual, const uint8_t *expected)
{
	Comparison result;
	result.difference = actual;
	uint8_t *diff = result.difference.data();
	const uint8_t *a = actual.data();
	for (size_t p = 0; p < size_t(kWidth) * kHeight; ++p) {
		int worst = 0;
		for (int c = 0; c < 4; ++c)
			worst = std::max(worst, std::abs(int(a[4 * p + c]) - int(expected[4 * p + c])));
		result.maxChannel = std::max(result.maxChannel, worst);
		if (worst > kChannelTolerance) {
			++result.differing;
			diff[4 * p] = 255;
			diff[4 * p + 1] = 40;
			diff[4 * p + 2] = 40;
		} else {
			for (int c = 0; c < 3; ++c)
				diff[4 * p + c] = uint8_t(expected[4 * p + c] / 3);
		}
		diff[4 * p + 3] = 255;
	}
	return result;
}

bool passes(const Comparison &c)
{
	return double(c.differing) <= kMaxDifferingShare * kWidth * kHeight;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 3) {
		std::fprintf(stderr, "usage: render-test <golden.png> <output directory>\n");
		return 2;
	}
	const std::string out = argv[2];
	const visage::Screenshot drawn = render(levels(1000.0));
	const visage::Screenshot again = render(levels(1000.0));
	drawn.save(out + "/spectrum.png");

	size_t repeat = 0;
	for (size_t i = 0; i < size_t(kWidth) * kHeight * 4; ++i)
		repeat += drawn.data()[i] != again.data()[i];
	check(repeat == 0, "the same input draws the same pixels", double(repeat));

	int w = 0, h = 0, channels = 0;
	uint8_t *golden = stbi_load(argv[1], &w, &h, &channels, 4);
	if (!golden || w != kWidth || h != kHeight) {
		check(false, "the golden image exists and has the size of the view");
		return 1;
	}

	const Comparison same = compare(drawn, golden);
	same.difference.save(out + "/difference.png");
	std::printf("     %zu of %d pixels differ by more than %d levels, the largest difference is %d levels\n",
		    same.differing, kWidth * kHeight, kChannelTolerance, same.maxChannel);
	check(passes(same), "the view matches the golden image", double(same.differing));

	// A tone drawn at 2 kHz instead of 1 kHz, as a wrong frequency axis would draw it.
	const visage::Screenshot moved = render(levels(2000.0));
	moved.save(out + "/broken-moved-peak.png");
	const Comparison movedResult = compare(moved, golden);
	movedResult.difference.save(out + "/difference-moved-peak.png");
	check(!passes(movedResult), "a peak at the wrong frequency fails the comparison", double(movedResult.differing));

	// No curve at all, as a broken analyzer would draw it.
	const visage::Screenshot empty = render({});
	empty.save(out + "/broken-no-curve.png");
	const Comparison emptyResult = compare(empty, golden);
	emptyResult.difference.save(out + "/difference-no-curve.png");
	check(!passes(emptyResult), "a missing curve fails the comparison", double(emptyResult.differing));

	stbi_image_free(golden);
	std::printf(failures == 0 ? "all render tests passed\n" : "%d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
