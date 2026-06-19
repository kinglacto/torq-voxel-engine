#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <span>
#include <utility>

template<typename T>
class ThreadSafeQueue {
private:
	std::queue<T> internal_queue;
	mutable std::mutex mtx;
	std::condition_variable cv;
	bool stop_flag{false};

public:
	ThreadSafeQueue() = default;

	ThreadSafeQueue(const ThreadSafeQueue&) = delete;
	ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

	bool push(T item) {
		{
			std::lock_guard<std::mutex> lock(mtx);
			if (stop_flag) {
				return false;
			}
			internal_queue.push(std::move(item));
		}
		cv.notify_one();
		return true;
	}

	bool wait_pop(T& item) {
		std::unique_lock<std::mutex> lock(mtx);
		cv.wait(lock, [this] { return !internal_queue.empty() || stop_flag; });
		if (internal_queue.empty()) {
			return false;
		}
		item = std::move(internal_queue.front());
		internal_queue.pop();
		return true;
	}

	bool try_pop(T& item) {
		std::lock_guard<std::mutex> lock(mtx);
		if (internal_queue.empty()) {
			return false;
		}
		item = std::move(internal_queue.front());
		internal_queue.pop();
		return true;
	}

	bool empty() const {
		std::lock_guard<std::mutex> lock(mtx);
		return internal_queue.empty();
	}

	void stop() {
		{
			std::lock_guard<std::mutex> lock(mtx);
			stop_flag = true;
		}
		cv.notify_all();
	}
};

template<typename T, std::size_t Capacity>
class BoundedMpmcQueue {
	static_assert(Capacity >= 2);

private:
	struct Cell {
		std::atomic<std::size_t> sequence{0};
		T value{};
	};

	std::array<Cell, Capacity> buffer{};
	std::atomic<std::size_t> enqueue_pos{0};
	std::atomic<std::size_t> dequeue_pos{0};
	std::atomic<bool> closed{false};

public:
	BoundedMpmcQueue() {
		for (std::size_t i = 0; i < Capacity; i++) {
			buffer[i].sequence.store(i, std::memory_order_relaxed);
		}
	}

	BoundedMpmcQueue(const BoundedMpmcQueue&) = delete;
	BoundedMpmcQueue& operator=(const BoundedMpmcQueue&) = delete;

	[[nodiscard]] bool try_push(T&& item) {
		if (closed.load(std::memory_order_acquire)) {
			return false;
		}

		Cell* cell = nullptr;
		std::size_t pos = enqueue_pos.load(std::memory_order_relaxed);

		for (;;) {
			cell = &buffer[pos % Capacity];
			const std::size_t sequence = cell->sequence.load(std::memory_order_acquire);
			const auto diff = static_cast<std::intptr_t>(sequence) -
			                  static_cast<std::intptr_t>(pos);

			if (diff == 0) {
				if (enqueue_pos.compare_exchange_weak(pos, pos + 1,
					std::memory_order_relaxed, std::memory_order_relaxed)) {
					break;
				}
			} else if (diff < 0) {
				return false;
			} else {
				pos = enqueue_pos.load(std::memory_order_relaxed);
			}
		}

		cell->value = std::move(item);
		cell->sequence.store(pos + 1, std::memory_order_release);
		return true;
	}

	[[nodiscard]] bool try_pop(T& out) {
		Cell* cell = nullptr;
		std::size_t pos = dequeue_pos.load(std::memory_order_relaxed);

		for (;;) {
			cell = &buffer[pos % Capacity];
			const std::size_t sequence = cell->sequence.load(std::memory_order_acquire);
			const auto diff = static_cast<std::intptr_t>(sequence) -
			                  static_cast<std::intptr_t>(pos + 1);

			if (diff == 0) {
				if (dequeue_pos.compare_exchange_weak(pos, pos + 1,
					std::memory_order_relaxed, std::memory_order_relaxed)) {
					break;
				}
			} else if (diff < 0) {
				return false;
			} else {
				pos = dequeue_pos.load(std::memory_order_relaxed);
			}
		}

		out = std::move(cell->value);
		cell->sequence.store(pos + Capacity, std::memory_order_release);
		return true;
	}

	std::size_t try_pop_many(std::span<T> out) {
		std::size_t count = 0;
		for (T& item : out) {
			if (!try_pop(item)) {
				break;
			}
			count++;
		}
		return count;
	}

	[[nodiscard]] bool has_items() const noexcept {
		const std::size_t enqueued = enqueue_pos.load(std::memory_order_acquire);
		const std::size_t dequeued = dequeue_pos.load(std::memory_order_acquire);
		return enqueued != dequeued;
	}

	void close() noexcept {
		closed.store(true, std::memory_order_release);
	}

	[[nodiscard]] bool is_closed() const noexcept {
		return closed.load(std::memory_order_acquire);
	}
};
