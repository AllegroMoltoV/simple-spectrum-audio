// Layer 1 of the GUI tests: the numbers behind the picture. Checks where a tone appears, how loud it reads, that
// pink noise looks flat with a 3 dB/oct tilt, the range and smoothing, the resolutions, a resized plot, and the
// sample ring between the audio and GUI threads.
#include "sample-ring.h"
#include "spectrum-analyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what, double value = 0.0)
{
	std::printf("%s %s (value %.6g)\n", ok ? "ok  " : "FAIL", what, value);
	if (!ok)
		++failures;
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

std::vector<float> sine(double hz, double amplitude, size_t frames)
{
	std::vector<float> x(frames);
	for (size_t i = 0; i < frames; ++i)
		x[i] = float(amplitude * std::sin(2.0 * kPi * hz * double(i) / kRate));
	return x;
}

// Pink noise from white noise with Paul Kellet's filter, with a fixed seed.
std::vector<float> pinkNoise(size_t frames, uint32_t seed)
{
	std::vector<float> x(frames);
	double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
	for (auto &v : x) {
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
		v = float((b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362) * 0.05);
		b6 = white * 0.115926;
	}
	return x;
}

// Feeds the input in blocks of 512 and updates after every block, as the GUI does once per frame.
void feed(ssp::SpectrumAnalyzer &analyzer, const std::vector<float> &input)
{
	for (size_t at = 0; at < input.size(); at += 512) {
		analyzer.push(input.data() + at, std::min<size_t>(512, input.size() - at));
		analyzer.update();
	}
}

int peakColumn(const std::vector<float> &levels)
{
	return int(std::max_element(levels.begin(), levels.end()) - levels.begin());
}

double columnOf(double hz, int columns)
{
	return std::log(hz / ssp::kMinFrequency) / std::log(ssp::kMaxFrequency / ssp::kMinFrequency) * (columns - 1);
}

ssp::DisplaySettings flat(int resolution = 4096)
{
	ssp::DisplaySettings s;
	s.resolution = resolution;
	s.tiltDbPerOct = 0.0;
	s.smoothing = 0.0;
	return s;
}

void testTonePosition()
{
	for (int resolution : {2048, 4096, 8192}) {
		for (double hz : {100.0, 1000.0, 10000.0}) {
			ssp::SpectrumAnalyzer analyzer;
			analyzer.setSampleRate(kRate);
			analyzer.setSettings(flat(resolution));
			feed(analyzer, sine(hz, 0.5, 16384));
			const int columns = 588;
			const int peak = peakColumn(analyzer.columns(columns));
			// The peak is in the column of the tone, within the width of a bin at low frequencies.
			const double binColumns = columnOf(hz + kRate / resolution, columns) - columnOf(hz, columns);
			const double allowed = std::max(1.0, binColumns);
			char what[96];
			std::snprintf(what, sizeof(what), "a %.0f Hz tone peaks at its column (%d points)", hz, resolution);
			check(std::abs(peak - columnOf(hz, columns)) <= allowed, what, peak - columnOf(hz, columns));
		}
	}
}

void testToneLevel()
{
	// A full-scale sine in the middle of a bin reads 0 dB, and a sine at -20 dBFS reads -20 dB.
	const double bin = std::round(1000.0 / (kRate / 4096.0));
	const double centred = bin * kRate / 4096.0;
	for (double dbfs : {0.0, -20.0}) {
		ssp::SpectrumAnalyzer analyzer;
		analyzer.setSampleRate(kRate);
		analyzer.setSettings(flat());
		feed(analyzer, sine(centred, std::pow(10.0, dbfs / 20.0), 8192));
		const double db = analyzer.binDb(int(bin));
		check(std::abs(db - dbfs) < 0.1, dbfs == 0.0 ? "a full-scale sine reads 0 dB" : "a -20 dBFS sine reads -20 dB",
		      db);
	}
}

void testPinkNoiseIsFlat()
{
	// Pink noise loses 3 dB per octave per bin. A tilt of 3 dB/oct makes it flat. Long smoothing averages out the
	// randomness of the noise.
	ssp::SpectrumAnalyzer analyzer;
	analyzer.setSampleRate(kRate);
	ssp::DisplaySettings s = flat();
	s.tiltDbPerOct = 3.0;
	s.smoothing = 1.0;
	analyzer.setSettings(s);
	feed(analyzer, pinkNoise(size_t(kRate * 8), 7));
	const int columns = 588;
	const auto levels = analyzer.columns(columns);
	double low = 1.0, high = 0.0;
	for (int c = int(columnOf(100.0, columns)); c <= int(columnOf(10000.0, columns)); ++c) {
		low = std::min(low, double(levels[size_t(c)]));
		high = std::max(high, double(levels[size_t(c)]));
	}
	const double spreadDb = (high - low) * 90.0;
	check(spreadDb < 6.0, "pink noise with a 3 dB/oct tilt is flat within 6 dB from 100 Hz to 10 kHz", spreadDb);

	// Without the tilt, the same noise falls by about 3 dB per octave: about 20 dB from 100 Hz to 10 kHz.
	s.tiltDbPerOct = 0.0;
	analyzer.setSettings(s);
	const auto untilted = analyzer.columns(columns);
	const double fallDb = (untilted[size_t(columnOf(100.0, columns))] - untilted[size_t(columnOf(10000.0, columns))]) * 90.0;
	check(fallDb > 14.0 && fallDb < 26.0, "pink noise without tilt falls about 20 dB over two decades", fallDb);
}

void testRange()
{
	// A -45 dBFS tone sits in the middle of a -90 dB range and at a quarter of a -60 dB range.
	const double centred = std::round(1000.0 / (kRate / 4096.0)) * kRate / 4096.0;
	const int columns = 588;
	for (double floorDb : {-60.0, -90.0, -120.0}) {
		ssp::SpectrumAnalyzer analyzer;
		analyzer.setSampleRate(kRate);
		ssp::DisplaySettings s = flat();
		s.floorDb = floorDb;
		analyzer.setSettings(s);
		feed(analyzer, sine(centred, std::pow(10.0, -45.0 / 20.0), 8192));
		const auto levels = analyzer.columns(columns);
		const double top = *std::max_element(levels.begin(), levels.end());
		const double expected = (-45.0 - floorDb) / -floorDb;
		check(std::abs(top - expected) < 0.02, "a -45 dB tone is at its height in the range", top - expected);
	}
}

void testSmoothing()
{
	// After the tone stops, the level falls with the smoothing time constant and not at once.
	const double centred = std::round(1000.0 / (kRate / 4096.0)) * kRate / 4096.0;
	const int bin = int(std::round(1000.0 / (kRate / 4096.0)));
	for (double smoothing : {0.0, 0.5, 1.0}) {
		ssp::SpectrumAnalyzer analyzer;
		analyzer.setSampleRate(kRate);
		ssp::DisplaySettings s = flat();
		s.smoothing = smoothing;
		analyzer.setSettings(s);
		feed(analyzer, sine(centred, 1.0, 8192));
		feed(analyzer, std::vector<float>(8192, 0.0f));
		const double db = analyzer.binDb(bin);
		// 8192 samples of silence after the window has left the tone: the remaining level is
		// 10 log10(exp(-samples / tau)) with tau = smoothing * 0.6 s.
		if (smoothing == 0.0) {
			check(db < -150.0, "without smoothing the level falls at once", db);
		} else {
			const double tau = smoothing * ssp::kMaxSmoothingSeconds * kRate;
			const double expected = 10.0 * std::log10(std::exp(-8192.0 / tau));
			check(db > expected - 6.0 && db < expected + 6.0, "the level falls with the smoothing time constant", db - expected);
		}
	}
}

void testSameInputSameLevels()
{
	ssp::SpectrumAnalyzer a, b;
	for (auto *analyzer : {&a, &b}) {
		analyzer->setSampleRate(kRate);
		analyzer->setSettings(ssp::DisplaySettings{});
		feed(*analyzer, pinkNoise(48000, 3));
	}
	check(a.columns(588) == b.columns(588), "the same input gives the same levels");
}

void testResize()
{
	ssp::SpectrumAnalyzer analyzer;
	analyzer.setSampleRate(kRate);
	analyzer.setSettings(flat());
	feed(analyzer, sine(1000.0, 0.5, 16384));
	for (int columns : {348, 588, 1200}) {
		const auto levels = analyzer.columns(columns);
		const int peak = peakColumn(levels);
		// A wider plot spreads a bin over more columns, so the allowance is the width of a bin, at least 1 column.
		const double allowed = std::max(1.0, columnOf(1000.0 + kRate / 4096, columns) - columnOf(1000.0, columns));
		check(int(levels.size()) == columns && std::abs(peak - columnOf(1000.0, columns)) <= allowed,
		      "a resized plot keeps the tone at its column", peak - columnOf(1000.0, columns));
	}
}

void testSampleRing()
{
	auto ring = std::make_unique<ssp::SampleRing>();
	std::vector<float> left(1000), right(1000), out(ssp::SampleRing::kCapacity / 2);
	for (size_t i = 0; i < left.size(); ++i) {
		left[i] = float(i);
		right[i] = float(i) + 2.0f;
	}
	uint64_t cursor = 0;
	ring->writeMid(left.data(), right.data(), 1000);
	size_t n = ring->read(cursor, out.data(), out.size());
	check(n == 1000 && out[0] == 1.0f && out[999] == 1000.0f, "the ring passes the mid of every sample", double(n));
	n = ring->read(cursor, out.data(), out.size());
	check(n == 0, "the ring gives nothing twice", double(n));
	// When the reader falls behind, it gets only the newest samples.
	for (int i = 0; i < 100; ++i)
		ring->writeMid(left.data(), right.data(), 1000);
	n = ring->read(cursor, out.data(), 4096);
	check(n == 4096 && out[4095] == 1000.0f, "a late reader gets the newest samples", double(n));
}

} // namespace

int main()
{
	testTonePosition();
	testToneLevel();
	testPinkNoiseIsFlat();
	testRange();
	testSmoothing();
	testSameInputSameLevels();
	testResize();
	testSampleRing();
	std::printf(failures == 0 ? "all analyzer tests passed\n" : "%d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
