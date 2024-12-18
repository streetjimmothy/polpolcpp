#pragma once
#include "utilities.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>

class ThreadPool
{
public:
	// Delete copy constructor and assignment operator
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	// Static method to get the singleton instance
	static ThreadPool* getInstance() {
		static ThreadPool* instance;
		if (instance == nullptr) {
			instance = new ThreadPool();
		}
		return instance;
	}

	template<class F, typename T, class... Args>
	auto distribute(F&& f, std::vector<T>* v, Args&&... args) -> std::vector<typename std::invoke_result_t<F, std::vector<T>*, int, int, Args...>>;
	template<class F, class... Args>
	auto distribute(F&& f, igraph_t* g, Args&&... args) -> std::vector<typename std::invoke_result_t<F, igraph_t*, int, int, Args&&...>>;

	template<class F, class... Args>
	auto submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>>;

private:
	// Vector of worker threads
	std::vector<std::thread> workers;
	std::mutex queue_mutex;
	std::condition_variable condition;
	bool stop = false;
	// Task queue
	std::queue<std::function<void()>> tasks;

	ThreadPool();
	~ThreadPool();
};

template<class F, typename T, class... Args>
auto ThreadPool::distribute(F&& f, std::vector<T>* v, Args&&... args) -> std::vector<typename std::invoke_result_t<F, std::vector<T>*, int, int, Args...>> {
	using return_type = typename std::invoke_result_t<F, std::vector<T>*, int, int, Args...>;

	int divisions = workers.size() * 2;	//we want more divisions than workers so that if one finishes early, there's still work to do
	int chunk_size = v->size() / divisions;
	std::vector<std::future<return_type>> futures;
	for (int i = 0; i < divisions; i++) {
		int start = i * chunk_size;
		int end = (i == divisions - 1) ? v->size() : (i + 1) * chunk_size;
		futures.emplace_back(submit(forward<F>(f), v, start, end, forward<Args>(args)));
	}

	// Collect the results from all futures
	vector<return_type> results;
	for (auto& fut : futures) {
		results.push_back(fut.get()); // This will block until the future is ready
	}

	return results;
};

template<class F, class... Args>
auto ThreadPool::distribute(F&& f, igraph_t* g, Args&&... args) -> std::vector<typename std::invoke_result_t<F, igraph_t*, int, int, Args&&...>> {
	using return_type = typename std::invoke_result_t<F, igraph_t*, int, int, Args...>;

	if (g == NULL) {
		return {};
	}
	if (igraph_vcount(g) == 0) {
		cerr << "Graph has no vertices" << endl;
		return {};
	}
	//if the graph is too small to be worth threading...
	if (igraph_vcount(g) < workers.size() * 100) {
		return { f(g, 0, igraph_vcount(g), std::forward<Args>(args)...) };
	}
	int divisions = workers.size() * 2;	//we want more divisions than workers so that if one finishes early, there's still work to do
	int chunk_size = igraph_vcount(g) / divisions;
	std::vector<std::future<return_type>> futures;
	for (int i = 0; i < divisions; i++) {
		int start = i * chunk_size;
		int end = (i == divisions - 1) ? igraph_vcount(g) : (i + 1) * chunk_size;
		futures.emplace_back(submit(forward<F>(f), g, start, end, forward<Args>(args)...));
	}

	// Collect the results from all futures
	vector<return_type> results;
	for (auto& fut : futures) {
		results.push_back(fut.get()); // This will block until the future is ready
	}

	return results;
};

template<class F, class... Args>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>> {
	using return_type = typename std::invoke_result_t<F, Args...>;

	auto task = std::make_shared<std::packaged_task<return_type()>>(
		std::bind(std::forward<F>(f), std::forward<Args>(args)...)
	);

	std::future<return_type> res = task->get_future();
	{
		std::unique_lock<std::mutex> lock(queue_mutex);
		tasks.emplace([task]() { (*task)(); });
	}
	condition.notify_one();
	return res;
};