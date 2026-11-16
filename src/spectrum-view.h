#pragma once

#include <vector>

namespace visage {
class Canvas;
}

namespace ssp {

// The plot inside the window, leaving room for the dB labels on the left and the frequency labels at the bottom.
struct PlotArea {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

PlotArea plotArea(int width, int height);

// Draws the spectrum view. The plug-in window and the windowless rendering test call the same function, so the
// test checks the pixels the user sees. levels holds one value per column of the plot, from 0 (floorDb) to 1 (0 dB).
void drawSpectrum(visage::Canvas &canvas, int width, int height, const std::vector<float> &levels, double floorDb);

} // namespace ssp
