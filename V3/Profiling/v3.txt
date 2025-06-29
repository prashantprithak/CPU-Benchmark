// main.cpp
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#include <random>
#include <queue>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <mutex>

using namespace std;

const int PARALLEL_THRESHOLD = 10000; // Threshold for parallel execution

// Utility to generate random data
vector<int> generate_data(size_t size) {
    vector<int> data(size);
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(1, size);
    for (auto &x : data) x = dis(gen);
    return data;
}

// Sequential quicksort
void quicksort_seq(vector<int>& arr, int left, int right) {
    if (left >= right) return;
    int pivot = arr[right];
    int i = left;
    for (int j = left; j < right; ++j) {
        if (arr[j] < pivot) {
            swap(arr[i], arr[j]);
            ++i;
        }
    }
    swap(arr[i], arr[right]);
    quicksort_seq(arr, left, i - 1);
    quicksort_seq(arr, i + 1, right);
}

// Thread pool implementation
class ThreadPool {
public:
    ThreadPool(size_t num_threads) : stop(false) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    function<void()> task;
                    {
                        unique_lock<mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });
                        if (this->stop && this->tasks.empty()) return;
                        task = move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template<class F>
    void enqueue(F&& f) {
        {
            unique_lock<mutex> lock(queue_mutex);
            tasks.emplace(forward<F>(f));
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            unique_lock<mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (thread &worker : workers) worker.join();
    }

private:
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queue_mutex;
    condition_variable condition;
    bool stop;
};

// Parallel quicksort using thread pool with cutoff threshold
void quicksort_parallel(vector<int>& arr, int left, int right, ThreadPool& pool, atomic<int>& task_counter) {
    if (left >= right) {
        task_counter--;
        return;
    }

    if (right - left < PARALLEL_THRESHOLD) {
        quicksort_seq(arr, left, right);
        task_counter--;
        return;
    }

    int pivot = arr[right];
    int i = left;
    for (int j = left; j < right; ++j) {
        if (arr[j] < pivot) {
            swap(arr[i], arr[j]);
            ++i;
        }
    }
    swap(arr[i], arr[right]);

    task_counter += 2;
    pool.enqueue([&, left, i] {
        quicksort_parallel(arr, left, i - 1, pool, task_counter);
    });
    pool.enqueue([&, i, right] {
        quicksort_parallel(arr, i + 1, right, pool, task_counter);
    });
    task_counter--;
}

// Benchmarking function
void benchmark(size_t size, const string& csv_file) {
    vector<int> original = generate_data(size);

    ofstream csv(csv_file);
    csv << "Threads,ExecutionTime(ms),Speedup,Throughput(elements/sec)\n";

    // Sequential
    vector<int> data = original;
    auto start = chrono::high_resolution_clock::now();
    quicksort_seq(data, 0, data.size() - 1);
    auto end = chrono::high_resolution_clock::now();
    double base_time = chrono::duration<double, milli>(end - start).count();
    csv << "Sequential," << base_time << ",1.0," << (size / (base_time / 1000)) << "\n";

    // Parallel (1 to 8 threads)
    for (int num_threads = 1; num_threads <= 8; ++num_threads) {
        data = original;
        ThreadPool pool(num_threads);
        atomic<int> task_counter(1);
        start = chrono::high_resolution_clock::now();
        pool.enqueue([&] {
            quicksort_parallel(data, 0, data.size() - 1, pool, task_counter);
        });
        while (task_counter > 0) this_thread::yield();
        end = chrono::high_resolution_clock::now();
        double time_taken = chrono::duration<double, milli>(end - start).count();
        double speedup = base_time / time_taken;
        double throughput = size / (time_taken / 1000);
        csv << num_threads << "," << time_taken << "," << speedup << "," << throughput << "\n";
    }
    csv.close();
}

int main() {
    benchmark(1'000'000, "light_load.csv");
    benchmark(10'000'000, "medium_load.csv");
    benchmark(50'000'000, "heavy_load.csv");
    cout << "Benchmarking completed. Check CSV files." << endl;
    return 0;
}
