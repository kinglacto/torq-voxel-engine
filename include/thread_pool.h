#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

#include "thread_safe_queue.h"

class ThreadPool {
public:
	explicit ThreadPool(std::size_t numThreads);
	~ThreadPool();

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	bool submit(std::function<void()> job);

private:
	void worker_loop();

	std::atomic<bool> accepting_jobs{true};
	std::vector<std::thread> workers;
	ThreadSafeQueue<std::function<void()>> job_queue;
};
