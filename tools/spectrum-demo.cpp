// Pictures for the write-up, drawn with the same analyzer and view as the plug-in.
//
//   spectrum-demo frames <directory>   Draws 48 windowless frames while a tone sweeps from 50 Hz to 15 kHz over
//                                      pink noise, as frame-NN.png.
//   spectrum-demo window <seconds>     Opens a top-level window that shows the same sweep live, and closes it
//                                      after the given number of seconds. Used to photograph the window on Xvfb.
#include <visage/app.h>
#include <visage_graphics/canvas.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "spectrum-analyzer.h"
#include "spectrum-view.h"

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 320;
constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// Generates the sweep block by block: a -12 dBFS tone gliding exponentially, over quiet pink noise.
class Sweep {
public:
	explicit Sweep(double seconds) : total_(seconds * kRate) {}

	void next(float *out, size_t count)
	{
		for (size_t i = 0; i < count; ++i, ++n_) {
			const double position = std::fmod(double(n_) / total_, 1.0);
			const double hz = 50.0 * std::pow(15000.0 / 50.0, position);
			phase_ = std::fmod(phase_ + 2.0 * kPi * hz / kRate, 2.0 * kPi);
			seed_ ^= seed_ << 13;
			seed_ ^= seed_ >> 17;
			seed_ ^= seed_ << 5;
			const double white = double(seed_ >> 8) / double(1 << 24) * 2.0 - 1.0;
			b0_ = 0.99886 * b0_ + white * 0.0555179;
			b1_ = 0.99332 * b1_ + white * 0.0750759;
			b2_ = 0.96900 * b2_ + white * 0.1538520;
			b3_ = 0.86650 * b3_ + white * 0.3104856;
			b4_ = 0.55000 * b4_ + white * 0.5329522;
			b5_ = -0.7616 * b5_ - white * 0.0168980;
			const double pink = (b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362) * 0.01;
			b6_ = white * 0.115926;
			out[i] = float(pink + 0.25 * std::sin(phase_));
		}
	}

private:
	double total_;
	uint64_t n_ = 0;
	double phase_ = 0.0;
	uint32_t seed_ = 5;
	double b0_ = 0, b1_ = 0, b2_ = 0, b3_ = 0, b4_ = 0, b5_ = 0, b6_ = 0;
};

int frames(const std::string &directory)
{
	ssp::SpectrumAnalyzer analyzer;
	analyzer.setSampleRate(kRate);
	ssp::DisplaySettings settings;
	settings.smoothing = 0.2;
	analyzer.setSettings(settings);
	Sweep sweep(4.0);
	std::vector<float> block(800);
	// Warm up for half a second, then one frame every 4 blocks (15 frames per second of input) for 4 seconds.
	for (int i = 0; i < 30; ++i) {
		sweep.next(block.data(), block.size());
		analyzer.push(block.data(), block.size());
		analyzer.update();
	}
	for (int f = 0; f < 60; ++f) {
		for (int i = 0; i < 4; ++i) {
			sweep.next(block.data(), block.size());
			analyzer.push(block.data(), block.size());
			analyzer.update();
		}
		visage::Canvas canvas;
		canvas.setWindowless(kWidth, kHeight);
		ssp::drawSpectrum(canvas, kWidth, kHeight, analyzer.columns(ssp::plotArea(kWidth, kHeight).width),
				  settings.floorDb);
		canvas.submit();
		char name[32];
		std::snprintf(name, sizeof(name), "/frame-%02d.png", f);
		canvas.takeScreenshot().save(directory + name);
	}
	std::printf("wrote 60 frames to %s\n", directory.c_str());
	return 0;
}

int window(double seconds)
{
	ssp::SpectrumAnalyzer analyzer;
	analyzer.setSampleRate(kRate);
	ssp::DisplaySettings settings;
	settings.smoothing = 0.2;
	analyzer.setSettings(settings);
	Sweep sweep(4.0);
	const auto start = std::chrono::steady_clock::now();
	uint64_t generated = 0;
	std::vector<float> block;

	visage::ApplicationWindow app;
	app.setTitle("Simple Spectrum");
	app.onDraw() = [&](visage::Canvas &canvas) {
		// Generates the input at the pace of a real stream, so the window shows what a host would show.
		const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		const uint64_t due = uint64_t(elapsed * kRate);
		block.resize(size_t(due - generated));
		sweep.next(block.data(), block.size());
		analyzer.push(block.data(), block.size());
		generated = due;
		analyzer.update();
		const int width = int(std::lround(app.width()));
		const int height = int(std::lround(app.height()));
		ssp::drawSpectrum(canvas, width, height, analyzer.columns(ssp::plotArea(width, height).width), settings.floorDb);
		app.redraw();
	};
	std::thread closer([seconds] {
		std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
		// The event loop has no other way to stop from outside; the process ends here after the picture is taken.
		std::_Exit(0);
	});
	closer.detach();
	app.show(visage::Dimension::nativePixels(kWidth), visage::Dimension::nativePixels(kHeight));
	app.runEventLoop();
	return 0;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc >= 3 && std::string(argv[1]) == "frames")
		return frames(argv[2]);
	if (argc >= 3 && std::string(argv[1]) == "window")
		return window(std::atof(argv[2]));
	std::fprintf(stderr, "usage: spectrum-demo frames <directory> | window <seconds>\n");
	return 2;
}
