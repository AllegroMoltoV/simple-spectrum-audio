#include "spectrum-view.h"

#include <embedded/fonts.h>
#include <visage_graphics/canvas.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "spectrum-analyzer.h"

namespace ssp {

namespace {

constexpr int kLeftMargin = 40;
constexpr int kBottomMargin = 20;
constexpr int kTopMargin = 8;
constexpr int kRightMargin = 12;
constexpr float kFontSize = 11.0f;

constexpr unsigned kBackground = 0xff101418;
constexpr unsigned kGrid = 0xff2a323a;
constexpr unsigned kLabel = 0xff8a96a3;
constexpr unsigned kLine = 0xff40c8ff;
constexpr unsigned kFill = 0x6040c8ff;

int frequencyX(const PlotArea &plot, double hz)
{
	const double position = std::log(hz / kMinFrequency) / std::log(kMaxFrequency / kMinFrequency);
	return plot.x + int(std::lround(position * (plot.width - 1)));
}

} // namespace

PlotArea plotArea(int width, int height)
{
	PlotArea plot;
	plot.x = kLeftMargin;
	plot.y = kTopMargin;
	plot.width = std::max(width - kLeftMargin - kRightMargin, 2);
	plot.height = std::max(height - kTopMargin - kBottomMargin, 2);
	return plot;
}

void drawSpectrum(visage::Canvas &canvas, int width, int height, const std::vector<float> &levels, double floorDb)
{
	const PlotArea plot = plotArea(width, height);
	const visage::Font font(kFontSize, visage::fonts::Lato_Regular_ttf);
	canvas.setColor(kBackground);
	canvas.fill(0, 0, width, height);

	// Horizontal lines every 10 dB from 0 dB down to the floor, labelled every 10 or 20 dB.
	const int labelStep = floorDb < -60.0 ? 20 : 10;
	for (int db = 0; db >= int(floorDb); db -= 10) {
		const int y = plot.y + int(std::lround(double(db) / floorDb * (plot.height - 1)));
		canvas.setColor(kGrid);
		canvas.fill(plot.x, y, plot.width, 1);
		if ((-db) % labelStep == 0) {
			canvas.setColor(kLabel);
			canvas.text(std::to_string(db), font, visage::Font::kRight, 0, y - 8, kLeftMargin - 6, 16);
		}
	}
	// Vertical lines at 2, 5 and 10 of every decade, labelled at 100 Hz, 1 kHz and 10 kHz.
	for (double decade : {10.0, 100.0, 1000.0, 10000.0}) {
		for (double step : {2.0, 5.0, 10.0}) {
			const double hz = decade * step;
			if (hz < kMinFrequency || hz > kMaxFrequency)
				continue;
			const int x = frequencyX(plot, hz);
			canvas.setColor(kGrid);
			canvas.fill(x, plot.y, 1, plot.height);
		}
	}
	canvas.setColor(kLabel);
	for (auto [hz, label] : {std::pair{100.0, "100"}, std::pair{1000.0, "1k"}, std::pair{10000.0, "10k"}})
		canvas.text(label, font, visage::Font::kCenter, frequencyX(plot, hz) - 20, plot.y + plot.height + 2, 40, 16);

	if (levels.size() < 2)
		return;
	visage::GraphData graph(int(levels.size()));
	for (size_t i = 0; i < levels.size(); ++i)
		graph[int(i)] = 1.0f - std::clamp(levels[i], 0.0f, 1.0f);
	canvas.setColor(kFill);
	canvas.graphFill(graph, plot.x, plot.y, plot.width, plot.height, 1.0f);
	canvas.setColor(kLine);
	canvas.graphLine(graph, plot.x, plot.y, plot.width, plot.height, 2.0f);
}

} // namespace ssp
