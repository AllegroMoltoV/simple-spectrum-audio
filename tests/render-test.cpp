// Stage A check of windowless rendering: draws the spectrum view twice from the same input without a window,
// compares the pixels, and saves the image. Usage: render-test <output.png>
#include <visage_graphics/canvas.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "spectrum-view.h"

namespace {

visage::Screenshot render(int width, int height, float peak)
{
	visage::Canvas canvas;
	canvas.setWindowless(width, height);
	ssp::drawSpectrum(canvas, width, height, ssp::demoLevels(width, peak));
	canvas.submit();
	return canvas.takeScreenshot();
}

} // namespace

int main(int argc, char **argv)
{
	constexpr int kWidth = 640;
	constexpr int kHeight = 320;
	const visage::Screenshot first = render(kWidth, kHeight, 0.55f);
	const visage::Screenshot second = render(kWidth, kHeight, 0.55f);
	const visage::Screenshot moved = render(kWidth, kHeight, 0.30f);
	const size_t bytes = size_t(kWidth) * kHeight * 4;

	int failures = 0;
	const bool sized = first.width() == kWidth && first.height() == kHeight && second.width() == kWidth &&
			   second.height() == kHeight;
	std::printf("%s screenshot size %dx%d\n", sized ? "ok  " : "FAIL", first.width(), first.height());
	failures += sized ? 0 : 1;
	if (sized) {
		size_t differing = 0;
		for (size_t i = 0; i < bytes; ++i)
			differing += first.data()[i] != second.data()[i];
		std::printf("%s same input gives the same pixels (%zu differing bytes)\n", differing == 0 ? "ok  " : "FAIL",
			    differing);
		failures += differing == 0 ? 0 : 1;
		size_t changed = 0;
		for (size_t i = 0; i < bytes; ++i)
			changed += first.data()[i] != moved.data()[i];
		std::printf("%s a moved peak changes the pixels (%zu differing bytes)\n", changed > 0 ? "ok  " : "FAIL",
			    changed);
		failures += changed > 0 ? 0 : 1;
	}
	if (argc > 1) {
		first.save(argv[1]);
		std::printf("saved %s\n", argv[1]);
	}
	if (argc > 2)
		moved.save(argv[2]);
	return failures == 0 ? 0 : 1;
}
