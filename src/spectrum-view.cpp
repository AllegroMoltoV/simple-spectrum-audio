#include "spectrum-view.h"

#include <visage_graphics/canvas.h>

#include <algorithm>
#include <cmath>

namespace ssp {

void drawSpectrum(visage::Canvas &canvas, int width, int height, const std::vector<float> &levels)
{
	canvas.setColor(0xff101418);
	canvas.fill(0, 0, width, height);

	// Grid: 10 horizontal lines and the decades of a 20 Hz to 20 kHz logarithmic axis.
	canvas.setColor(0xff2a323a);
	for (int i = 1; i < 10; ++i)
		canvas.fill(0, height * i / 10, width, 1);
	for (double hz : {100.0, 1000.0, 10000.0}) {
		const int x = int(std::lround(width * std::log10(hz / 20.0) / 3.0));
		canvas.fill(x, 0, 1, height);
	}

	if (levels.size() < 2)
		return;
	visage::GraphData graph(int(levels.size()));
	for (size_t i = 0; i < levels.size(); ++i)
		graph[int(i)] = 1.0f - std::clamp(levels[i], 0.0f, 1.0f);
	canvas.setColor(0x6040c8ff);
	canvas.graphFill(graph, 0, 0, width, height, 1.0f);
	canvas.setColor(0xff40c8ff);
	canvas.graphLine(graph, 0, 0, width, height, 2.0f);
}

std::vector<float> demoLevels(int columns, float peakPosition)
{
	std::vector<float> levels(size_t(std::max(columns, 0)));
	for (int i = 0; i < columns; ++i) {
		const float x = columns > 1 ? float(i) / float(columns - 1) : 0.0f;
		const float slope = 0.7f - 0.35f * x;
		const float d = (x - peakPosition) / 0.02f;
		levels[size_t(i)] = slope + 0.25f * std::exp(-d * d);
	}
	return levels;
}

} // namespace ssp
