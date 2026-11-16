#pragma once

#include <cstddef>
#include <vector>

struct PFFFT_Setup;

namespace ssp {

struct DisplaySettings {
	int resolution = 4096;	   // FFT size: 2048, 4096 or 8192.
	double floorDb = -90.0;	   // Bottom of the display. The top is 0 dBFS.
	double tiltDbPerOct = 4.5; // Added per octave above 1 kHz, subtracted below it.
	double smoothing = 0.5;	   // 0 to 1. Time constant of the average of each bin, 0 to kMaxSmoothingSeconds.
};

constexpr double kMinFrequency = 20.0;
constexpr double kMaxFrequency = 20000.0;
constexpr double kMaxSmoothingSeconds = 0.6;

// Frequency at the centre of a column of a logarithmic axis from kMinFrequency to kMaxFrequency.
double columnFrequency(double column, int columns);

// Runs on the GUI thread. Keeps the newest samples, computes the power spectrum of the newest window with a Hann
// window, averages each bin over time, and maps the bins to the columns of the display. The display state advances
// by the number of samples pushed, not by time, so the same input always gives the same picture.
class SpectrumAnalyzer {
public:
	static constexpr int kMaxResolution = 8192;

	SpectrumAnalyzer();
	~SpectrumAnalyzer();
	SpectrumAnalyzer(const SpectrumAnalyzer &) = delete;
	SpectrumAnalyzer &operator=(const SpectrumAnalyzer &) = delete;

	void setSampleRate(double sampleRate);
	// A change of the resolution or the sample rate restarts the time average.
	void setSettings(const DisplaySettings &settings);
	const DisplaySettings &settings() const
	{
		return settings_;
	}

	void push(const float *samples, size_t count);
	// Computes the spectrum of the newest window and updates the time average with the samples pushed since the
	// previous update. Does nothing until a full window of samples has arrived.
	void update();

	// Level of the bin in dB relative to a full-scale sine, before the tilt.
	double binDb(int bin) const;
	// Level of each column from 0 (floorDb or below) to 1 (0 dB or above), after the tilt.
	std::vector<float> columns(int count) const;
	bool ready() const
	{
		return ready_;
	}

private:
	void prepare();
	double displayDb(int column, int count) const;

	DisplaySettings settings_;
	double sampleRate_ = 48000.0;
	PFFFT_Setup *setup_ = nullptr;
	int setupSize_ = 0;
	float *input_ = nullptr;
	float *output_ = nullptr;
	float *work_ = nullptr;
	std::vector<float> window_;
	double windowGain_ = 1.0;

	std::vector<float> history_;
	size_t historyPos_ = 0;
	size_t filled_ = 0;
	size_t pending_ = 0;

	std::vector<double> power_; // Time-averaged power of each bin, relative to a full-scale sine.
	bool ready_ = false;
};

} // namespace ssp
