#include "spectrum-analyzer.h"

#include <pffft.h>

#include <algorithm>
#include <cmath>

namespace ssp {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Power of a bin that is treated as silence: -200 dB.
constexpr double kSilence = 1e-20;

int validResolution(int resolution)
{
	return resolution <= 2048 ? 2048 : resolution <= 4096 ? 4096 : 8192;
}

} // namespace

double columnFrequency(double column, int columns)
{
	const double position = columns > 1 ? column / double(columns - 1) : 0.0;
	return kMinFrequency * std::pow(kMaxFrequency / kMinFrequency, position);
}

SpectrumAnalyzer::SpectrumAnalyzer() : history_(size_t(kMaxResolution), 0.0f)
{
	prepare();
}

SpectrumAnalyzer::~SpectrumAnalyzer()
{
	if (setup_)
		pffft_destroy_setup(setup_);
	pffft_aligned_free(input_);
	pffft_aligned_free(output_);
	pffft_aligned_free(work_);
}

void SpectrumAnalyzer::setSampleRate(double sampleRate)
{
	if (sampleRate > 0.0 && sampleRate != sampleRate_) {
		sampleRate_ = sampleRate;
		ready_ = false;
	}
}

void SpectrumAnalyzer::setSettings(const DisplaySettings &settings)
{
	const int resolution = validResolution(settings.resolution);
	settings_ = settings;
	settings_.resolution = resolution;
	settings_.smoothing = std::clamp(settings.smoothing, 0.0, 1.0);
	if (resolution != setupSize_)
		prepare();
}

void SpectrumAnalyzer::prepare()
{
	const int n = validResolution(settings_.resolution);
	if (setup_)
		pffft_destroy_setup(setup_);
	pffft_aligned_free(input_);
	pffft_aligned_free(output_);
	pffft_aligned_free(work_);
	setup_ = pffft_new_setup(n, PFFFT_REAL);
	input_ = static_cast<float *>(pffft_aligned_malloc(sizeof(float) * size_t(n)));
	output_ = static_cast<float *>(pffft_aligned_malloc(sizeof(float) * size_t(n)));
	work_ = static_cast<float *>(pffft_aligned_malloc(sizeof(float) * size_t(n)));
	setupSize_ = n;
	window_.resize(size_t(n));
	double sum = 0.0;
	for (int i = 0; i < n; ++i) {
		window_[size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * kPi * i / n));
		sum += window_[size_t(i)];
	}
	// A full-scale sine in the middle of a bin gives an amplitude of windowGain_ * 1.
	windowGain_ = sum / 2.0;
	power_.assign(size_t(n / 2 + 1), kSilence);
	ready_ = false;
}

void SpectrumAnalyzer::push(const float *samples, size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		history_[historyPos_] = samples[i];
		historyPos_ = (historyPos_ + 1) % history_.size();
	}
	filled_ = std::min(history_.size(), filled_ + count);
	pending_ += count;
}

void SpectrumAnalyzer::update()
{
	const int n = setupSize_;
	if (filled_ < size_t(n) || pending_ == 0)
		return;
	const size_t size = history_.size();
	const size_t start = (historyPos_ + size - size_t(n)) % size;
	for (int i = 0; i < n; ++i)
		input_[i] = history_[(start + size_t(i)) % size] * window_[size_t(i)];
	pffft_transform_ordered(setup_, input_, output_, work_, PFFFT_FORWARD);

	// The time constant is in seconds of input, so the average depends only on the samples pushed.
	const double tau = settings_.smoothing * kMaxSmoothingSeconds * sampleRate_;
	const double alpha = ready_ && tau > 0.0 ? 1.0 - std::exp(-double(pending_) / tau) : 1.0;
	pending_ = 0;
	const double scale = 1.0 / (windowGain_ * windowGain_);
	for (int k = 1; k < n / 2; ++k) {
		const double re = output_[2 * k];
		const double im = output_[2 * k + 1];
		const double p = std::max((re * re + im * im) * scale, kSilence);
		power_[size_t(k)] += alpha * (p - power_[size_t(k)]);
	}
	// pffft packs the DC and Nyquist bins, which are real, into the first complex value.
	power_[0] += alpha * (std::max(double(output_[0]) * output_[0] * scale, kSilence) - power_[0]);
	power_[size_t(n / 2)] += alpha * (std::max(double(output_[1]) * output_[1] * scale, kSilence) - power_[size_t(n / 2)]);
	ready_ = true;
}

double SpectrumAnalyzer::binDb(int bin) const
{
	if (bin < 0 || size_t(bin) >= power_.size())
		return -200.0;
	return 10.0 * std::log10(power_[size_t(bin)]);
}

double SpectrumAnalyzer::displayDb(int column, int count) const
{
	const double binHz = sampleRate_ / setupSize_;
	const double lastBin = double(setupSize_ / 2);
	const double low = std::clamp(columnFrequency(column - 0.5, count) / binHz, 0.0, lastBin);
	const double high = std::clamp(columnFrequency(column + 0.5, count) / binHz, 0.0, lastBin);
	const double centre = std::clamp(columnFrequency(column, count) / binHz, 0.0, lastBin);
	double db;
	const int first = int(std::ceil(low));
	const int last = int(std::floor(high));
	if (last >= first) {
		// Several bins fall in the column: show the loudest, so that a narrow peak keeps its height.
		double p = kSilence;
		for (int k = first; k <= last; ++k)
			p = std::max(p, power_[size_t(k)]);
		db = 10.0 * std::log10(p);
	} else {
		// The column is narrower than a bin: interpolate between the neighbouring bins.
		const int k = std::min(int(centre), setupSize_ / 2 - 1);
		const double t = centre - k;
		db = (1.0 - t) * binDb(k) + t * binDb(k + 1);
	}
	return db + settings_.tiltDbPerOct * std::log2(columnFrequency(column, count) / 1000.0);
}

std::vector<float> SpectrumAnalyzer::columns(int count) const
{
	std::vector<float> levels(size_t(std::max(count, 0)), 0.0f);
	if (!ready_ || settings_.floorDb >= 0.0)
		return levels;
	for (int c = 0; c < count; ++c) {
		const double db = displayDb(c, count);
		levels[size_t(c)] = float(std::clamp((db - settings_.floorDb) / -settings_.floorDb, 0.0, 1.0));
	}
	return levels;
}

} // namespace ssp
