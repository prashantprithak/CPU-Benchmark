#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include <random>
#include <iomanip>
#include <future>
#include <chrono>
#include <mutex>
#include <map>
#include <fstream>

using namespace std;
using namespace chrono;

// ================== ENHANCED METRICS ====================
struct DetailedMetrics {
    double total_wall_time;           // Total time
    double pure_computation_time;     // Pure computation time
    double overhead_time;             // Overhead time
    double theoretical_speedup;       // Theoretical speedup
    double actual_speedup;            // Actual speedup
    double efficiency;                // Processor efficiency
    int cores_used;                   // Number of cores used
    int sequential_portions;          // Sequential portions
    size_t array_size;
    string algorithm_name;

    void print() const {
        cout << fixed << setprecision(3);
        cout << algorithm_name << ":\n";
        cout << "  Wall Time: " << total_wall_time << "s\n";
        cout << "  Pure Computation: " << pure_computation_time << "s\n";
        cout << "  Overhead: " << overhead_time << "s ("
            << (overhead_time / total_wall_time) * 100 << "%)\n";
        cout << "  Cores Used: " << cores_used << "\n";
        cout << "  Theoretical Speedup: " << theoretical_speedup << "x\n";
        cout << "  Actual Speedup: " << actual_speedup << "x\n";
        cout << "  Efficiency: " << efficiency * 100 << "%\n";
        cout << "  Sequential Portions: " << sequential_portions << "\n\n";
    }
};

// ================== THREAD-SAFE TIMER ==================
class ThreadSafeTimer {
private:
    mutable mutex timer_mutex;
    map<thread::id, high_resolution_clock::time_point> thread_start_times;
    map<thread::id, double> thread_computation_times;
    atomic<double> total_overhead_time{ 0.0 };
    atomic<int> max_concurrent_threads{ 0 };
    atomic<int> current_active_threads{ 0 };
    atomic<int> sequential_operations{ 0 };

public:
    void start_thread_timer() {
        auto thread_id = this_thread::get_id();
        auto overhead_start = high_resolution_clock::now();

        {
            lock_guard<mutex> lock(timer_mutex);
            thread_start_times[thread_id] = high_resolution_clock::now();
        }

        // Update the number of active threads
        int current = current_active_threads.fetch_add(1) + 1;
        int max_so_far = max_concurrent_threads.load();
        while (current > max_so_far && !max_concurrent_threads.compare_exchange_weak(max_so_far, current)) {
            max_so_far = max_concurrent_threads.load();
        }

        auto overhead_end = high_resolution_clock::now();
        auto overhead = duration_cast<microseconds>(overhead_end - overhead_start).count() / 1000000.0;
        total_overhead_time.store(total_overhead_time.load() + overhead);
    }

    void end_thread_timer() {
        auto thread_id = this_thread::get_id();
        auto end_time = high_resolution_clock::now();

        {
            lock_guard<mutex> lock(timer_mutex);
            auto start_time = thread_start_times[thread_id];
            auto computation_time = duration_cast<microseconds>(end_time - start_time).count() / 1000000.0;
            thread_computation_times[thread_id] = computation_time;
        }
        current_active_threads--;
    }

    void record_sequential_operation() {
        sequential_operations++;
    }

    double get_total_computation_time() const {
        lock_guard<mutex> lock(timer_mutex);
        double total = 0.0;
        for (const auto& pair : thread_computation_times) {
            total += pair.second;
        }
        return total;
    }

    double get_overhead_time() const {
        return total_overhead_time.load();
    }

    int get_max_concurrent_threads() const {
        return max(1, max_concurrent_threads.load()); // At least 1
    }

    int get_sequential_operations() const {
        return sequential_operations.load();
    }

    void reset() {
        lock_guard<mutex> lock(timer_mutex);
        thread_start_times.clear();
        thread_computation_times.clear();
        total_overhead_time = 0.0;
        max_concurrent_threads = 0;
        current_active_threads = 0;
        sequential_operations = 0;
    }
};

// ================== CSV LOGGER ==================
class CSVLogger {
private:
    ofstream csv_file;
    string filename;

public:
    CSVLogger(const string& filename) : filename(filename) {
        csv_file.open(filename);
        if (csv_file.is_open()) {
            // Write the header
            csv_file << "Array_Size,Number_Threads,Time,Speedup" << endl;
        }
    }

    ~CSVLogger() {
        if (csv_file.is_open()) {
            csv_file.close();
        }
    }

    void log_result(size_t array_size, int num_threads, double time, double speedup) {
        if (csv_file.is_open()) {
            csv_file << array_size << "," << num_threads << ","
                << fixed << setprecision(6) << time << ","
                << fixed << setprecision(3) << speedup << endl;
            csv_file.flush(); // To save immediately
        }
    }

    void print_csv_location() {
        cout << "\n=== CSV FILE SAVED ===" << endl;
        cout << "Results saved to: " << filename << endl;
        cout << "Ready for Python analysis!" << endl;
    }
};

// ================== ENHANCED PARALLEL SORT ==================
class EnhancedParallelSort {
private:
    ThreadSafeTimer* timer;
    const int SMALL_THRESHOLD = 25000;
    const int PARALLEL_THRESHOLD = 50000;
    const int MAX_DEPTH = 6;

    int clean_partition(int* arr, int low, int high) {
        // Same partition code
        int mid = low + (high - low) / 2;
        if (arr[mid] < arr[low]) swap(arr[low], arr[mid]);
        if (arr[high] < arr[low]) swap(arr[low], arr[high]);
        if (arr[high] < arr[mid]) swap(arr[mid], arr[high]);
        swap(arr[mid], arr[high]);

        int pivot = arr[high];
        int i = low - 1;

        for (int j = low; j < high; j++) {
            if (arr[j] <= pivot) {
                ++i;
                if (i != j) swap(arr[i], arr[j]);
            }
        }
        swap(arr[i + 1], arr[high]);
        return i + 1;
    }

    void insertion_sort(int* arr, int left, int right) {
        timer->record_sequential_operation();

        for (int i = left + 1; i <= right; i++) {
            int key = arr[i];
            int j = i - 1;
            while (j >= left && arr[j] > key) {
                arr[j + 1] = arr[j];
                j--;
            }
            arr[j + 1] = key;
        }
    }

    void enhanced_parallel_quicksort(int* arr, int left, int right, int depth) {
        if (left >= right) return;

        int size = right - left + 1;

        if (size < SMALL_THRESHOLD) {
            timer->start_thread_timer();
            insertion_sort(arr, left, right);
            timer->end_thread_timer();
            return;
        }

        // Measure partition time (sequential)
        auto partition_start = high_resolution_clock::now();
        int pivot = clean_partition(arr, left, right);
        auto partition_end = high_resolution_clock::now();

        // Partitioning is always sequential
        timer->record_sequential_operation();

        int left_size = pivot - left;
        int right_size = right - pivot;

        bool worth_parallelizing = (depth < MAX_DEPTH) &&
            (size > PARALLEL_THRESHOLD) &&
            (max(left_size, right_size) > SMALL_THRESHOLD);

        if (worth_parallelizing) {
            if (left_size >= right_size && left_size > SMALL_THRESHOLD) {
                // Real parallel work
                auto future = async(launch::async, [=]() {
                    enhanced_parallel_quicksort(arr, left, pivot - 1, depth + 1);
                    });
                enhanced_parallel_quicksort(arr, pivot + 1, right, depth + 1);
                future.wait();
            }
            else if (right_size > SMALL_THRESHOLD) {
                auto future = async(launch::async, [=]() {
                    enhanced_parallel_quicksort(arr, pivot + 1, right, depth + 1);
                    });
                enhanced_parallel_quicksort(arr, left, pivot - 1, depth + 1);
                future.wait();
            }
            else {
                // Both are small - do sequential
                timer->record_sequential_operation();
                enhanced_parallel_quicksort(arr, left, pivot - 1, depth + 1);
                enhanced_parallel_quicksort(arr, pivot + 1, right, depth + 1);
            }
        }
        else {
            // Do sequential
            timer->record_sequential_operation();
            enhanced_parallel_quicksort(arr, left, pivot - 1, depth + 1);
            enhanced_parallel_quicksort(arr, pivot + 1, right, depth + 1);
        }
