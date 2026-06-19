#include "thread_pool.h"

#include <exception>
#include <iostream>
#include <utility>

ThreadPool::ThreadPool(const std::size_t numThreads) {
	const std::size_t workerCount = numThreads == 0 ? 1 : numThreads;
	workers.reserve(workerCount);
	for (std::size_t i = 0; i < workerCount; ++i) {
		workers.emplace_back(&ThreadPool::worker_loop, this);
	}
}

ThreadPool::~ThreadPool() {
	accepting_jobs.store(false);
	job_queue.stop();
	for (auto& worker : workers) {
		if (worker.joinable()) {
			worker.join();
		}
	}
}

void ThreadPool::worker_loop() {
	std::function<void()> job;
	while (job_queue.wait_pop(job)) {
		try {
			job();
		} catch (const std::exception& e) {
			std::cerr << "ThreadPool job failed: " << e.what() << std::endl;
		} catch (...) {
			std::cerr << "ThreadPool job failed with an unknown exception" << std::endl;
		}
		job = nullptr;
	}
}

bool ThreadPool::submit(std::function<void()> job) {
	if (!job || !accepting_jobs.load()) {
		return false;
	}
	return job_queue.push(std::move(job));
}
