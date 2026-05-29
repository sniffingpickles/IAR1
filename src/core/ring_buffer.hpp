#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace iar {

// SampleRing is a bounded, lock-free single-producer / single-consumer ring
// buffer of interleaved float audio samples.
//
//   Producer = the OBS raw-audio callback thread. It only ever calls Write()
//   and never blocks or allocates, which keeps the real-time audio path safe.
//   On overflow it drops the *incoming* samples (and counts them) rather than
//   stalling OBS - by design the consumer almost always keeps up because Opus
//   encoding is cheap; network back-pressure is absorbed downstream in the
//   sender's bounded outbound queue (which drops *old* packets).
//
//   Consumer = the encoder/sender worker thread. It calls Read().
//
// Capacity is rounded up to a power of two so index wrapping is a mask. The
// usable capacity is (capacity - 1) samples.
class SampleRing {
public:
	explicit SampleRing(std::size_t min_capacity_samples) {
		std::size_t cap = 1;
		while (cap < min_capacity_samples + 1)
			cap <<= 1;
		buf_.resize(cap);
		mask_ = cap - 1;
	}

	std::size_t capacity() const { return mask_; }

	// available() is the number of readable samples (approximate under
	// concurrency, but monotonic-safe for the consumer's own decisions).
	std::size_t available() const {
		std::size_t w = write_.load(std::memory_order_acquire);
		std::size_t r = read_.load(std::memory_order_relaxed);
		return (w - r) & mask_;
	}

	std::size_t free_space() const { return mask_ - available(); }

	// Write copies `n` samples in. Returns the number actually written. If there
	// is not enough room the whole write is rejected (returns 0) and the dropped
	// sample count is incremented; this avoids tearing a frame across a wrap gap.
	// Called only by the producer (OBS audio thread).
	std::size_t Write(const float *src, std::size_t n) {
		std::size_t w = write_.load(std::memory_order_relaxed);
		std::size_t r = read_.load(std::memory_order_acquire);
		std::size_t freeSamples = mask_ - ((w - r) & mask_);
		if (n > freeSamples) {
			dropped_.fetch_add(n, std::memory_order_relaxed);
			return 0;
		}
		std::size_t cap = mask_ + 1;
		std::size_t first = cap - (w & mask_);
		if (first > n)
			first = n;
		std::copy(src, src + first, &buf_[w & mask_]);
		if (first < n)
			std::copy(src + first, src + n, &buf_[0]);
		write_.store(w + n, std::memory_order_release);
		return n;
	}

	// Read copies up to `n` samples out. Returns the number read. Called only by
	// the consumer (worker thread).
	std::size_t Read(float *dst, std::size_t n) {
		std::size_t r = read_.load(std::memory_order_relaxed);
		std::size_t w = write_.load(std::memory_order_acquire);
		std::size_t avail = (w - r) & mask_;
		if (n > avail)
			n = avail;
		std::size_t cap = mask_ + 1;
		std::size_t first = cap - (r & mask_);
		if (first > n)
			first = n;
		std::copy(&buf_[r & mask_], &buf_[r & mask_] + first, dst);
		if (first < n)
			std::copy(&buf_[0], &buf_[0] + (n - first), dst + first);
		read_.store(r + n, std::memory_order_release);
		return n;
	}

	std::uint64_t dropped_samples() const {
		return dropped_.load(std::memory_order_relaxed);
	}

	// fill_ratio is 0..1, for the UI ring-buffer level indicator.
	double fill_ratio() const {
		return static_cast<double>(available()) / static_cast<double>(mask_);
	}

	void reset() {
		read_.store(0, std::memory_order_relaxed);
		write_.store(0, std::memory_order_relaxed);
	}

private:
	std::vector<float> buf_;
	std::size_t mask_ = 0;
	std::atomic<std::size_t> read_{0};
	std::atomic<std::size_t> write_{0};
	std::atomic<std::uint64_t> dropped_{0};
};

} // namespace iar
