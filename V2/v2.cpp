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
#include <queue>

using namespace std;
using namespace chrono;

// ================== ENHANCED METRICS ==================
struct DetailedMetrics {
    double total_wall_time;           // الوقت الإجمالي
    double pure_computation_time;     // وقت الحوسبة الفعلي
    double overhead_time;             // وقت الـ overhead
    double theoretical_speedup;       // الـ speedup النظري
    double actual_speedup;           // الـ speedup الفعلي
    double efficiency;               // كفاءة استخدام المعالجات
    int cores_used;                  // عدد المعالجات المستخدمة
    int sequential_portions;         // الأجزاء اللي ما اتوازتش
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

        // تحديث عدد المعالجات النشطة
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
        return max(1, max_concurrent_threads.load()); // على الأقل 1
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
            // كتابة الـ header
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
            csv_file.flush(); // علشان يحفظ فوراً
        }
    }

    void print_csv_location() {
        cout << "\n=== CSV FILE SAVED ===" << endl;
        cout << "Results saved to: " << filename << endl;
        cout << "Ready for Python analysis!" << endl;
    }
};

// ================== THREAD POOL ==================
class ThreadPool {
private:
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queue_mutex;
    condition_variable condition;
    bool stop;
    const int num_threads;

public:
    ThreadPool(int threads = 4) : num_threads(threads), stop(false) {
        for (int i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    function<void()> task;
                    {
                        unique_lock<mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
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
    auto enqueue(F&& f) -> future<typename result_of<F()>::type> {
        using return_type = typename result_of<F()>::type;
        auto task = make_shared<packaged_task<return_type()>>(forward<F>(f));
        future<return_type> res = task->get_future();
        {
            unique_lock<mutex> lock(queue_mutex);
            if (stop) throw runtime_error("enqueue on stopped ThreadPool");
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    int get_thread_count() const { return num_threads; }

    ~ThreadPool() {
        {
            unique_lock<mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (thread& worker : workers) worker.join();
    }
};

// ================== IMPROVED PARALLEL SORT ==================
class ImprovedParallelSort {
private:
    ThreadSafeTimer* timer;
    ThreadPool* thread_pool;
    const int SMALL_THRESHOLD = 500000;    // حد أكبر بكتير
    const int PARALLEL_THRESHOLD = 2000000; // حد أكبر للتوازي
    const int MAX_DEPTH = 2;               // عمق قليل جداً
    atomic<int> active_tasks{ 0 };

    int clean_partition(int* arr, int low, int high) {
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

    void parallel_quicksort(int* arr, int left, int right, int depth) {
        if (left >= right) return;
        int size = right - left + 1;

        // استخدام std::sort للمصفوفات الصغيرة
        if (size < SMALL_THRESHOLD) {
            timer->start_thread_timer();
            std::sort(arr + left, arr + right + 1);
            timer->end_thread_timer();
            timer->record_sequential_operation();
            return;
        }

        int pivot = clean_partition(arr, left, right);
        int left_size = pivot - left;
        int right_size = right - pivot;

        // شروط صارمة جداً للتوازي
        bool should_parallelize = (depth < MAX_DEPTH) &&
            (size > PARALLEL_THRESHOLD) &&
            (min(left_size, right_size) > SMALL_THRESHOLD) &&
            (active_tasks.load() < 2); // مش أكتر من 2 tasks

        if (should_parallelize && thread_pool) {
            active_tasks++;
            auto future = thread_pool->enqueue([=]() {
                parallel_quicksort(arr, left, pivot - 1, depth + 1);
                });

            parallel_quicksort(arr, pivot + 1, right, depth + 1);
            future.wait();
            active_tasks--;
        }
        else {
            // sequential execution
            timer->record_sequential_operation();
            parallel_quicksort(arr, left, pivot - 1, depth + 1);
            parallel_quicksort(arr, pivot + 1, right, depth + 1);
        }
    }

public:
    void sort(int* array, size_t size, ThreadSafeTimer* t, ThreadPool* pool = nullptr) {
        timer = t;
        thread_pool = pool;
        active_tasks = 0;

        if (size <= 1) return;

        // للمصفوفات الصغيرة، استخدام sequential دائماً
        if (size < PARALLEL_THRESHOLD) {
            timer->start_thread_timer();
            std::sort(array, array + size);
            timer->end_thread_timer();
            timer->record_sequential_operation();
            return;
        }

        parallel_quicksort(array, 0, static_cast<int>(size - 1), 0);
    }
};

// ================== ACCURATE BENCHMARK ==================
class AccurateBenchmark {
private:
    ThreadSafeTimer timer;
    CSVLogger* csv_logger;
    ThreadPool thread_pool;

    vector<int> generate_test_data(size_t size) {
        vector<int> data;
        data.reserve(size);

        mt19937 gen(42);
        uniform_int_distribution<int> dis(1, static_cast<int>(size));

        for (size_t i = 0; i < size; i++) {
            data.push_back(dis(gen));
        }
        return data;
    }

    bool verify_sorted(const vector<int>& data) {
        return is_sorted(data.begin(), data.end());
    }

    DetailedMetrics calculate_detailed_metrics(
        const string& algorithm_name,
        size_t array_size,
        double wall_time,
        double baseline_wall_time = 0.0) {

        DetailedMetrics metrics;
        metrics.algorithm_name = algorithm_name;
        metrics.array_size = array_size;
        metrics.total_wall_time = wall_time;
        metrics.pure_computation_time = timer.get_total_computation_time();
        metrics.overhead_time = timer.get_overhead_time();

        // إصلاح حساب عدد المعالجات
        if (algorithm_name.find("Sequential") != string::npos) {
            metrics.cores_used = 1; // Sequential دائماً معالج واحد
        }
        else {
            metrics.cores_used = thread_pool.get_thread_count();
        }

        metrics.sequential_portions = timer.get_sequential_operations();

        // حساب الـ efficiency
        int hardware_threads = thread::hardware_concurrency();
        metrics.theoretical_speedup = min(static_cast<double>(hardware_threads),
            static_cast<double>(array_size / 25000));

        if (baseline_wall_time > 0 && wall_time > 0) {
            metrics.actual_speedup = baseline_wall_time / wall_time;
            // تأكد إن الـ speedup منطقي
            if (metrics.actual_speedup < 0.1 || metrics.actual_speedup > 20.0) {
                cout << "WARNING: Unrealistic speedup detected: " << metrics.actual_speedup << endl;
                cout << "Baseline: " << baseline_wall_time << "s, Current: " << wall_time << "s" << endl;
            }
        }
        else {
            metrics.actual_speedup = 1.0;
        }

        metrics.efficiency = (metrics.cores_used > 0) ?
            metrics.actual_speedup / metrics.cores_used : 0.0;

        return metrics;
    }

public:
    AccurateBenchmark() : thread_pool(4) { // 4 threads only
        csv_logger = new CSVLogger("parallel_sort_results.csv");
    }

    ~AccurateBenchmark() {
        delete csv_logger;
    }

    void run_accurate_test() {
        vector<size_t> test_sizes = { 5000000, 1000000, 10000000 }; // أحجام كبيرة جداً

        cout << "=== IMPROVED PARALLEL SORT WITH THREAD POOL ===" << endl;
        cout << "Using fixed thread pool (4 threads)" << endl;
        cout << "Testing very large arrays only" << endl;
        cout << "Minimal thread creation overhead" << endl;
        cout << "=============================================" << endl;

        for (auto size : test_sizes) {
            cout << "\n--- Testing Array Size: " << size << " ---" << endl;
            test_size_with_detailed_analysis(size);
        }

        csv_logger->print_csv_location();
    }

    void test_size_with_detailed_analysis(size_t array_size) {
        auto original_data = generate_test_data(array_size);
        double baseline_time = 0.0;

        // Test 1: Sequential std::sort
        {
            auto test_data = original_data;
            timer.reset();

            cout << "Running sequential sort..." << endl;
            auto wall_start = high_resolution_clock::now();
            timer.start_thread_timer();
            std::sort(test_data.data(), test_data.data() + test_data.size());
            timer.end_thread_timer();
            auto wall_end = high_resolution_clock::now();

            double wall_time = duration_cast<microseconds>(wall_end - wall_start).count() / 1000000.0;
            baseline_time = wall_time;

            auto metrics = calculate_detailed_metrics("Sequential std::sort", array_size, wall_time);
            metrics.print();

            csv_logger->log_result(array_size, 1, wall_time, 1.0);

            if (!verify_sorted(test_data)) {
                cout << "ERROR: Sequential sort failed!\n";
                return;
            }

            cout << "Sequential baseline established: " << fixed << setprecision(6) << baseline_time << "s\n";
        }

        // Test 2: Improved Parallel Sort with Thread Pool
        {
            auto test_data = original_data;
            timer.reset();
            ImprovedParallelSort sorter;

            cout << "Running parallel sort with thread pool..." << endl;
            auto wall_start = high_resolution_clock::now();
            sorter.sort(test_data.data(), test_data.size(), &timer, &thread_pool);
            auto wall_end = high_resolution_clock::now();

            double wall_time = duration_cast<microseconds>(wall_end - wall_start).count() / 1000000.0;

            cout << "Parallel sort completed in: " << fixed << setprecision(6) << wall_time << "s\n";

            // حساب الـ speedup الحقيقي
            double actual_speedup = (baseline_time > 0 && wall_time > 0) ? baseline_time / wall_time : 1.0;
            cout << "ACTUAL Speedup: " << fixed << setprecision(3) << actual_speedup << "x\n";

            auto metrics = calculate_detailed_metrics("Improved Parallel Sort", array_size, wall_time, baseline_time);
            metrics.print();

            // حفظ الـ speedup الحقيقي
            csv_logger->log_result(array_size, 4, wall_time, actual_speedup);

            if (!verify_sorted(test_data)) {
                cout << "ERROR: Parallel sort failed!\n";
                return;
            }

            // تحليل النتائج
            if (actual_speedup > 1.1) {
                cout << "[SUCCESS] Parallel sort is faster!\n";
            }
            else if (actual_speedup > 0.9) {
                cout << "[NEUTRAL] Parallel sort is comparable to sequential\n";
            }
            else {
                cout << "[FAILURE] Parallel sort is significantly slower\n";
                cout << "Overhead ratio: " << (1.0 / actual_speedup - 1.0) * 100 << "% slower\n";
            }

            analyze_performance(metrics, baseline_time);
        }
    }

    void analyze_performance(const DetailedMetrics& metrics, double baseline_time) {
        cout << "=== PERFORMANCE ANALYSIS ===" << endl;

        // هل الـ parallelization فعال؟
        if (metrics.actual_speedup > 1.1) {
            cout << "[OK] Parallelization is EFFECTIVE" << endl;
            cout << "  Speedup: " << fixed << setprecision(2) << metrics.actual_speedup << "x" << endl;
        }
        else {
            cout << "[FAIL] Parallelization is NOT effective" << endl;
            cout << "  Speedup: " << fixed << setprecision(2) << metrics.actual_speedup << "x (< 1.1x)" << endl;
        }

        // تحليل الـ overhead
        double overhead_percentage = (metrics.overhead_time / metrics.total_wall_time) * 100;
        if (overhead_percentage > 20) {
            cout << "[WARNING] HIGH OVERHEAD: " << fixed << setprecision(1) << overhead_percentage << "%" << endl;
            cout << "  Consider increasing PARALLEL_THRESHOLD" << endl;
        }
        else {
            cout << "[OK] Acceptable overhead: " << fixed << setprecision(1) << overhead_percentage << "%" << endl;
        }

        // تحليل الكفاءة
        if (metrics.efficiency > 0.7) {
            cout << "[OK] Good efficiency: " << fixed << setprecision(1) << metrics.efficiency * 100 << "%" << endl;
        }
        else {
            cout << "[WARNING] Low efficiency: " << fixed << setprecision(1) << metrics.efficiency * 100 << "%" << endl;
            cout << "  Cores are not being used optimally" << endl;
        }

        // Amdahl's Law Analysis
        double parallel_fraction = 1.0 - (static_cast<double>(metrics.sequential_portions) / 100.0);
        double amdahl_limit = 1.0 / (1.0 - parallel_fraction);
        cout << "Amdahl's Law Limit: " << fixed << setprecision(2) << amdahl_limit << "x" << endl;

        if (metrics.actual_speedup > amdahl_limit * 0.8) {
            cout << "[OK] Close to theoretical maximum" << endl;
        }
        else {
            cout << "[WARNING] Room for improvement (theoretical max: " << amdahl_limit << "x)" << endl;
        }

        cout << endl;
    }
};

// ================== MAIN FUNCTION ==================
int main() {
    cout << "=== ENHANCED PARALLEL SORT WITH ACCURATE SPEEDUP ===" << endl;
    cout << "Measuring:" << endl;
    cout << "- Pure computation time (excluding overhead)" << endl;
    cout << "- Thread creation and synchronization costs" << endl;
    cout << "- Real efficiency with Amdahl's Law" << endl;
    cout << "- Sequential vs parallel portions analysis" << endl;
    cout << "=================================================" << endl;

    AccurateBenchmark benchmark;
    benchmark.run_accurate_test();

    cout << "\n=== KEY INSIGHTS ===" << endl;
    cout << "- Actual Speedup = Sequential Time / Parallel Time" << endl;
    cout << "- Efficiency = Actual Speedup / Cores Used" << endl;
    cout << "- Overhead = Thread management + synchronization costs" << endl;
    cout << "- Amdahl's Law shows theoretical speedup limits" << endl;

    return 0;
}