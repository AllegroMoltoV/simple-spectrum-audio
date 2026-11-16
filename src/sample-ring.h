#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ssp {

// Passes the analyzed samples from the audio thread to the GUI thread without locks. The audio thread never waits:
// it overwrites the oldest samples, and the GUI thread reads only the newest ones. A read that the writer may have
// overwritten during the copy is discarded.
class SampleRing {
public:
	static constexpr size_t kCapacity = size_t(1) << 16;

	// Audio thread. Writes the average of the two channels.
	void writeMid(const float *left, const float *right, uint32_t frames)
	{
		uint64_t w = written_.load(std::memory_order_relaxed);
		for (uint32_t i = 0; i < frames; ++i, ++w)
			buffer_[size_t(w) & (kCapacity - 1)] = 0.5f * (left[i] + right[i]);
		written_.store(w, std::memory_order_release);
	}

	// GUI thread. Copies the samples written since cursor, at most maxSamples of the newest, and moves cursor.
	// Returns the number of samples copied. maxSamples must not exceed kCapacity / 2.
	size_t read(uint64_t &cursor, float *out, size_t maxSamples) const
	{
		const uint64_t w = written_.load(std::memory_order_acquire);
		if (w <= cursor) {
			cursor = w;
			return 0;
		}
		const uint64_t start = w - cursor > maxSamples ? w - maxSamples : cursor;
		const size_t count = size_t(w - start);
		for (size_t i = 0; i < count; ++i)
			out[i] = buffer_[size_t(start + i) & (kCapacity - 1)];
		cursor = w;
		// The writer may have wrapped around onto the start of the copy while it ran.
		if (written_.load(std::memory_order_acquire) - start > kCapacity)
			return 0;
		return count;
	}

	uint64_t written() const
	{
		return written_.load(std::memory_order_acquire);
	}

private:
	std::array<float, kCapacity> buffer_{};
	std::atomic<uint64_t> written_{0};
};

} // namespace ssp
