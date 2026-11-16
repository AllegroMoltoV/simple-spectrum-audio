#pragma once

#include <vector>

namespace visage {
class Canvas;
}

namespace ssp {

// Draws the spectrum view. The plug-in window and the windowless rendering test call the same function, so the
// test checks the pixels the user sees. levels holds one value per column, from 0 (bottom) to 1 (top).
void drawSpectrum(visage::Canvas &canvas, int width, int height, const std::vector<float> &levels);

// A fixed curve for the stage A prototype: pink-noise-like slope with a peak, in display coordinates.
std::vector<float> demoLevels(int columns, float peakPosition);

} // namespace ssp
