#include "ThreadPool.h"

using namespace std;

ThreadPool::ThreadPool() {
	unsigned int numCores = thread::hardware_concurrency();
	if (numCores == 0) {
		cerr << "Unable to determine the number of CPU cores." << endl;
		numCores = 1; // Default to 1 core if unable to determine
	}

	// Create worker threads
	for (unsigned int i = 0; i < numCores; ++i) {
		workers.emplace_back(
			[this] {
				function<void()> task;
				{
					unique_lock<mutex> lock(this->queue_mutex);
					this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
					if (this->stop && this->tasks.empty()) {
						return;
					}
					task = move(this->tasks.front());
					this->tasks.pop();
				}
				task();
			}
		);
	}
};

ThreadPool::~ThreadPool() {
	{
		unique_lock<mutex> lock(queue_mutex);
		stop = true;
	}
	condition.notify_all();
	for (thread& worker : workers) {
		worker.join();
	}
};