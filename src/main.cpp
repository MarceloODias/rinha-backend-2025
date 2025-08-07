#include <restbed>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <curl/curl.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <map>
#include <memory>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <limits>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

using namespace restbed;
using namespace std;
using namespace rapidjson;

using ROCKSDB_NAMESPACE::DB;
using ROCKSDB_NAMESPACE::Options;
using ROCKSDB_NAMESPACE::Status;

constexpr bool const_performance_metrics_enabled = true;

struct Payment {
    string correlationId;
    double amount{};
};

static size_t write_callback(void* contents, const size_t size, const size_t nmemb, void* userp) {
    auto* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

struct CurlHandle {
    CURL* handle = nullptr;
    struct curl_slist* headers = nullptr;
    std::string response_buffer;

    CurlHandle() {
        handle = curl_easy_init();
        if (handle) {
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(handle, CURLOPT_POST, 1L);
            curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response_buffer);

            // Optional: set timeouts or connection reuse options
            curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
            curl_easy_setopt(handle, CURLOPT_TCP_KEEPIDLE, 30L);
            curl_easy_setopt(handle, CURLOPT_TCP_KEEPINTVL, 15L);
        }
    }

    ~CurlHandle() {
        if (headers) curl_slist_free_all(headers);
        if (handle) curl_easy_cleanup(handle);
    }

    void clear_response() {
        response_buffer.clear();
    }

    const std::string& response() const {
        return response_buffer;
    }

    void set_payload(const std::string& url, const std::string& body) {
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
    }
};

struct Summary {
    uint64_t totalRequests = 0;
    double totalAmount = 0.0;
};

// ==== PROFILER ====

std::unordered_map<string, std::atomic<long>> performance_data;
std::unordered_map<string, std::atomic<long>> count_perf_data;

std::chrono::high_resolution_clock::time_point get_now()
{
    return std::chrono::high_resolution_clock::now();
}

void init_profiler()
{
    for (const std::string& key : {
        "parsing", "put-queue", "fetch-queue", "process_payment", "create_processor_payload", "send_to_processor",
        "store_processed", "evaluate_switch", "handle_response"
    }) {
        performance_data.emplace(key, 0);
        count_perf_data.emplace(key, 0);
    }
}

string get_local_time()
{
    // Get local date and time
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
    localtime_r(&t, &local_tm);
    std::stringstream ss;
    ss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");

    return ss.str();
}

string get_profiler_result()
{
    stringstream response;
    response << "# Performance by features in total micros)\n";
    response << "# Generated at: " << get_local_time() << "\n\n";

    // Output bucket metrics
    for (const auto & [fst, snd] : performance_data) {
        if (const long count = count_perf_data[fst].load(std::memory_order_relaxed); count > 0)
        {
            const double avg = static_cast<double>(performance_data[fst].load(std::memory_order_relaxed)) / count;
            response << "\"" << fst << "\"("<< count_perf_data[fst].load(std::memory_order_relaxed) <<"): "
                << avg << " - total: "
                << performance_data[fst].load(std::memory_order_relaxed) << "\n";
        }
    }

    const string response_str = response.str();
    return response_str;
}

void record_profiler_value(const string& featureName, const std::chrono::high_resolution_clock::time_point& start) {
    if constexpr (!const_performance_metrics_enabled) {
        return;
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    performance_data.at(featureName).fetch_add(duration.count(), std::memory_order_relaxed);
    count_perf_data.at(featureName).fetch_add(1, std::memory_order_relaxed);
}

void profiler_handler(const shared_ptr<Session>& session) {
    const string response_str = get_profiler_result();
    session->close(OK, response_str, {
        {"Content-Type", "text/plain"},
        {"Content-Length", to_string(response_str.size())},
        {"Connection", "close" }});
}

void print_log(const string& message)
{
    if constexpr (!const_performance_metrics_enabled) {
        return;
    }
    std::cout << message << std::endl;
}

// ==== PROFILER ====

void pin_thread_to_core(int core_id, string group) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    int rc = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "Failed to set thread affinity. " << std::endl;
    } else {
        std::cout << group << " thread pinned to core " << core_id << std::endl;
    }
#else
    // On macOS or unsupported systems, just print a message.
    std::cout << group << " thread pinning is not supported on this platform - cpu: " << core_id << std::endl;
#endif
}

class PaymentService {
public:
    static constexpr size_t QUEUE_CAPACITY = 10'000;

    PaymentService() {
        std::cout << "Initializing PaymentService v1.0..." << std::endl;

        const char* db_folder = getenv("DATABASE_PATH");
        string processed_db_path = db_folder ? string(db_folder) : "data_";

        Options options;
        options.create_if_missing = true;
        options.IncreaseParallelism();
        options.OptimizeLevelStyleCompaction();
        DB* pdb;

        std::cout << "Opening RocksDB database..." << std::endl;
        const Status p_status = DB::Open(options, processed_db_path.append("processed_db_file"), &pdb);

        std::cout << "Database opened. Checking the status..." << std::endl;

        if (!p_status.ok()) {
            std::cerr << "Failed to open processed_db: " << p_status.ToString() << std::endl;
            throw runtime_error("Failed to open RocksDB database");
        }

        processed_db.reset(pdb);

        std::cout << "Database started..." << std::endl;

        queue.resize(QUEUE_CAPACITY);

        const char* def = getenv("PROCESSOR_URL");
        const char* fb = getenv("FALLBACK_PROCESSOR_URL");
        default_processor = def ? string(def) : "http://localhost:8001";
        fallback_processor = fb ? string(fb) : default_processor;
        main_url = default_processor;
        test_url = fallback_processor;
        const char* fee = getenv("FEE_DIFFERENCE");
        fee_difference = fee ? atof(fee) : 0.11;
        const char* pace = getenv("FALLBACK_POOL_INTERVAL_MS");
        fallback_interval_ms = pace ? atoi(pace) : 1000;

        const char* workerCount = getenv("WORKER_COUNT");
        int workerCountInt = workerCount ? atoi(workerCount) : 5;

        std::cout << "Configurations read" << std::endl;
        for (int i = 0; i < workerCountInt; ++i) {
            workers.emplace_back([this]{ worker_loop(); });
        }
        // workers.emplace_back([this]{ worker_loop(true); });

        workers.emplace_back([this]{ profiler_loop(); });
        std::cout << "Threads started" << std::endl;
    }

    ~PaymentService() {
        running = false;
        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }
    }

    void enqueue(const Payment& p) {
        const auto start = get_now();
        const size_t idx = tail.fetch_add(1, std::memory_order_acq_rel);
        queue[idx % queue.size()] = p;

        record_profiler_value("put-queue", start);
    }

    map<string, Summary> query(const string& from, const string& to) const
    {
        map<string, Summary> result;
        result["default"] = Summary();
        result["fallback"] = Summary();
        string start = from + "|";
        string end = to + "~";
        unique_ptr<rocksdb::Iterator> it(processed_db->NewIterator(rocksdb::ReadOptions()));
        for (it->Seek(start); it->Valid() && it->key().ToString() <= end; it->Next()) {
            string value = it->value().ToString();
            Document d; d.Parse(value.c_str());
            string processor = d["processor"].GetString();
            double amount = d["amount"].GetDouble();
            auto& s = result[processor];
            s.totalRequests++;
            s.totalAmount += amount;
        }
        return result;
    }

    static Document service_health(const string& base_url) {
        Document result; result.SetObject();
        string url = base_url + "/payments/service-health";
        string body;
        CURL* curl = curl_easy_init();
        if (!curl) return result;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            result.Parse(body.c_str());
        }
        curl_easy_cleanup(curl);
        return result;
    }

private:
    void profiler_loop()
    {
        while (running)
        {
            const string response_str = get_profiler_result();
            std::cout << response_str << std::endl;
            this_thread::sleep_for(chrono::seconds(10));
        }
    }

    void worker_loop() {
        thread_local bool pinned = false;
        if (!pinned) {
            static std::atomic<int> next_cpu{0};
            int total_cores = std::thread::hardware_concurrency();
            int cpu_id = (next_cpu++ % (total_cores / 2)) + (total_cores / 2);
            pin_thread_to_core(cpu_id, "worker");
            pinned = true;
        }


        auto last_fallback = get_now();

        while (running) {
            Payment p; bool has = fetch_next(p);
            if (!has) {
                this_thread::sleep_for(chrono::milliseconds(100));
                continue;
            }

            bool isFallbackPool = false;
            const auto now = get_now();
            int elapsed = chrono::duration_cast<chrono::milliseconds>(now - last_fallback).count();
            if (elapsed >= fallback_interval_ms) {
                isFallbackPool = true;
                last_fallback = now;
            }

            auto [processor, ts] = process_payment(p, isFallbackPool);
            if (processor == "try_again") {
                enqueue(p);
            }
            else
            {
                store_processed(p, processor, ts);
            }
        }
    }

    bool fetch_next(Payment& p) {
        const auto start = get_now();

        size_t idx;
        while (true) {
            size_t current_head = head.load(std::memory_order_acquire);
            size_t current_tail = tail.load(std::memory_order_acquire);
            if (current_head >= current_tail) {
                return false;
            }
            if (head.compare_exchange_weak(current_head, current_head + 1, std::memory_order_acq_rel)) {
                idx = current_head;
                break;
            }
        }

        p = queue[idx % queue.size()];

        record_profiler_value("fetch-queue", start);

        return true;
    }

    pair<string, string> process_payment(const Payment& p, bool isFallbackPool)
    {
        const auto start = get_now();

        auto [payload, ts] = create_processor_payload(p);
        string primary = isFallbackPool ? test_url : main_url;
        string secondary = isFallbackPool ? main_url : test_url;
        string primary_label = (primary == default_processor ? "default" : "fallback");
        string secondary_label = (secondary == default_processor ? "default" : "fallback");

        double elapsed = 0.0; long code = 0;
        bool ok = send_to_processor(primary, payload, elapsed, code);
        handle_response(primary, elapsed, code, !isFallbackPool);
        if (ok) {

            record_profiler_value("process_payment", start);
            return {primary_label, ts};
        }
        if (code == 422)
        {
            record_profiler_value("process_payment", start);
            return {"discard", ts};
        }

        double elapsed2 = 0.0; long code2 = 0;
        ok = send_to_processor(secondary, payload, elapsed2, code2);
        handle_response(secondary, elapsed2, code2, false);
        if (ok) {
            record_profiler_value("process_payment", start);
            return {secondary_label, ts};
        }
        if (code == 422)
        {
            record_profiler_value("process_payment", start);
            return {"discard", ts};
        }
        record_profiler_value("process_payment", start);
        return {"try_again", ts};
    }

    void handle_response(const string& url, double elapsed, long code, bool from_main_pool)
    {
        const auto start = get_now();
        if (!from_main_pool) {
            if (code == 500) fallback_down.store(true);
            else fallback_down.store(false);
        }

        if (code == 500) {
            elapsed = numeric_limits<double>::max();
        }
        update_time(url, elapsed);
        if (from_main_pool && code == 500 && !fallback_down.load()) {
            trigger_switch("Main down");
        }
        else if (from_main_pool && code == 500 && fallback_down.load())
        {
            print_log("Primary processor is down, waiting for fallback to recover...");
            this_thread::sleep_for(chrono::milliseconds(500));
        }
        else
        {
            evaluate_switch();
        }

        record_profiler_value("handle_response", start);
    }

    void update_time(const string& url, double elapsed)
    {
        if (url == default_processor) {
            default_time_ms.store(elapsed);
        } else if (url == fallback_processor) {
            fallback_time_ms.store(elapsed);
        }
    }

    void trigger_switch(const string& reason)
    {
        const auto now = get_local_time();
        swap(main_url, test_url);

        std::cout << reason << " - switching to " << main_url << " at " << now << std::endl;
    }

    void evaluate_switch()
    {
        const auto start = get_now();

        const double def = default_time_ms.load();
        const double fb = fallback_time_ms.load();
        if (def == 0 || fb == 0) return;
        const double improvement = (def - fb) / def;
        const bool using_default = (main_url == default_processor);
        if (fb < def && improvement >= fee_difference) {
            if (using_default) trigger_switch("Fallback better");
        } else {
            if (!using_default) trigger_switch("Main better");
        }

        record_profiler_value("evaluate_switch", start);
    }

    static pair<string, string> create_processor_payload(const Payment& p) {
        const auto start = get_now();

        // ---- Timestamp formatting ----
        auto const now = chrono::system_clock::now();
        time_t t = chrono::system_clock::to_time_t(now);
        tm tm{};
        gmtime_r(&t, &tm);
        auto const ms = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()) % 1000;

        // Reuse stringstream per thread
        thread_local stringstream timestamp_ss;
        timestamp_ss.str("");
        timestamp_ss.clear();

        timestamp_ss << put_time(&tm, "%Y-%m-%dT%H:%M:%S");
        timestamp_ss << '.' << setw(3) << setfill('0') << ms.count() << 'Z';
        const string requestedAt = timestamp_ss.str();

        // ---- JSON Building ----
        thread_local Document d;
        d.SetObject();  // Reset for reuse
        auto& a = d.GetAllocator();

        d.AddMember("correlationId", StringRef(p.correlationId.c_str()), a);
        d.AddMember("amount", p.amount, a);
        d.AddMember("requestedAt", StringRef(requestedAt.c_str()), a);

        thread_local StringBuffer sb;
        sb.Clear();  // Reuse buffer
        Writer<StringBuffer> w(sb);
        d.Accept(w);

        record_profiler_value("create_processor_payload", start);

        return {sb.GetString(), requestedAt};
    }

    static bool send_to_processor(const string& base, const string& payload, double& elapsed, long& code) {
        const auto start_method = get_now();

        const string url = base + "/payments";
        thread_local CurlHandle curl_wrapper;

        CURL* curl = curl_wrapper.handle;
        if (!curl) {
            elapsed = 0;
            code = 0;
            return false;
        }

        curl_wrapper.clear_response(); // reuse buffer

        curl_wrapper.set_payload(url, payload); // dynamic update only

        const auto start = chrono::steady_clock::now();
        CURLcode res = curl_easy_perform(curl);
        const auto end = chrono::steady_clock::now();

        elapsed = chrono::duration<double, milli>(end - start).count();
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

        record_profiler_value("send_to_processor", start_method);

        const auto now = get_local_time();

        if constexpr (const_performance_metrics_enabled)
        {
            std::cout << code << " " << elapsed << " " << url << " at " << now << std::endl;
        }

        return (res == CURLE_OK && code == 200);
    }

    void store_processed(const Payment& p, const string& processor, const string& timestamp) const
    {
        const auto start = get_now();

        // Reuse Document and buffer per thread
        thread_local Document d;
        d.SetObject();  // Clear any previous content
        auto& a = d.GetAllocator();

        d.AddMember("timestamp", StringRef(timestamp.c_str()), a);
        d.AddMember("amount", p.amount, a);
        d.AddMember("processor", StringRef(processor.c_str()), a);

        thread_local StringBuffer sb;
        sb.Clear();  // Important: clear buffer before reuse
        Writer<StringBuffer> w(sb);
        d.Accept(w);

        // Key construction: thread-local stringstream to avoid reallocations
        thread_local stringstream key_builder;
        key_builder.str("");
        key_builder.clear();
        key_builder << timestamp << '|' << p.correlationId;
        const string key = key_builder.str();

        processed_db->Put(rocksdb::WriteOptions(), key, sb.GetString());

        record_profiler_value("store_processed", start);
    }

    unique_ptr<rocksdb::DB> processed_db;
    vector<Payment> queue;
    atomic<size_t> head{0};
    atomic<size_t> tail{0};
    vector<thread> workers;
    string default_processor;
    string fallback_processor;
    string main_url;
    string test_url;
    atomic<double> default_time_ms{0.0};
    atomic<double> fallback_time_ms{0.0};
    atomic<bool> fallback_down{false};
    double fee_difference{0.0};
    int fallback_interval_ms{1000};
    atomic<bool> running{true};
};

static shared_ptr<PaymentService> service;

void post_payment_handler(const shared_ptr<Session>& session) {
    thread_local bool pinned = false;
    if (!pinned) {
        static std::atomic<int> next_cpu{0};
        int cpu_id = next_cpu++ % (std::thread::hardware_concurrency() / 2); // Pin to half of available cores
        pin_thread_to_core(cpu_id, "rest");
        pinned = true;
    }

    const auto request = session->get_request();
    const int length = request->get_header("Content-Length", 0);

    session->fetch(length, [](const shared_ptr<Session>& session, const Bytes& body) {
        const auto start_parse = get_now();

        // Reuse allocator and Document per thread
        thread_local rapidjson::MemoryPoolAllocator<> allocator;
        allocator.Clear();  // Ensure clean allocator per request

        thread_local rapidjson::Document d(&allocator);
        d.SetObject();  // Clear previous JSON document

        // Safe to parse non-null-terminated buffer using length
        d.Parse<rapidjson::kParseDefaultFlags>(reinterpret_cast<const char*>(body.data()), body.size());

        Payment p;
        p.correlationId = d["correlationId"].GetString();
        p.amount = d["amount"].GetDouble();

        record_profiler_value("parsing", start_parse);

        service->enqueue(p);

        static const string response = R"({"status": "Accepted"})";
        session->yield(202, response, {
            {"Content-Type", "application/json"},
            {"Content-Length", to_string(response.size())},
            {"Connection", "keep-alive"}
        });
    });
}

map<string, Summary> call_other_instance(const string& other, const string& from, const string& to) {
    map<string, Summary> result; result["default"] = Summary(); result["fallback"] = Summary();
    string url = other + "/payments-summary?from=" + from + "&to=" + to + "&internal=true";
    string body;
    CURL* curl = curl_easy_init();
    if (!curl) return result;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    CURLcode res = curl_easy_perform(curl);
    long code = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    if (res == CURLE_OK && code == 200) {
        Document d; d.Parse(body.c_str());
        if (d.HasMember("default")) {
            auto& def = d["default"];
            result["default"].totalRequests = def["totalRequests"].GetUint64();
            result["default"].totalAmount = def["totalAmount"].GetDouble();
        }
        if (d.HasMember("fallback")) {
            auto& fb = d["fallback"];
            result["fallback"].totalRequests = fb["totalRequests"].GetUint64();
            result["fallback"].totalAmount = fb["totalAmount"].GetDouble();
        }
    }
    return result;
}

void payments_summary_handler(const shared_ptr<Session>& session) {
    const auto request = session->get_request();
    string from = request->get_query_parameter("from");
    string to = request->get_query_parameter("to");
    string internal = request->get_query_parameter("internal", "false");

    auto res = service->query(from, to);

    if (internal != "true") {
        const char* other = getenv("OTHER_INSTANCE_URL");
        if (other != nullptr) {
            auto otherRes = call_other_instance(other, from, to);
            res["default"].totalRequests += otherRes["default"].totalRequests;
            res["default"].totalAmount += otherRes["default"].totalAmount;
            res["fallback"].totalRequests += otherRes["fallback"].totalRequests;
            res["fallback"].totalAmount += otherRes["fallback"].totalAmount;
        }
    }

    // === JSON serialization with reusable RapidJSON components ===
    thread_local rapidjson::MemoryPoolAllocator<> allocator;
    allocator.Clear();

    thread_local rapidjson::Document d(&allocator);
    d.SetObject();
    auto& a = d.GetAllocator();

    // Build default object
    rapidjson::Value def(rapidjson::kObjectType);
    def.AddMember("totalRequests", res["default"].totalRequests, a);
    def.AddMember("totalAmount", res["default"].totalAmount, a);

    // Build fallback object
    rapidjson::Value fb(rapidjson::kObjectType);
    fb.AddMember("totalRequests", res["fallback"].totalRequests, a);
    fb.AddMember("totalAmount", res["fallback"].totalAmount, a);

    d.AddMember("default", def, a);
    d.AddMember("fallback", fb, a);

    thread_local rapidjson::StringBuffer sb;
    sb.Clear();

    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    d.Accept(writer);

    session->yield(200, sb.GetString(), {
        { "Content-Type", "application/json" },
        { "Content-Length", to_string(sb.GetSize())},
        {"Connection", "keep-alive"}
    });
}

int main(const int, char**) {
    std::cout << "Starting Payment Service..." << std::endl;
    init_profiler();

    curl_global_init(CURL_GLOBAL_DEFAULT);
    service = make_shared<PaymentService>();

    std::cout << "Initializing the paths..." << std::endl;

    const auto payments = make_shared<Resource>();
    payments->set_path("/payments");
    payments->set_method_handler("POST", post_payment_handler);

    const auto summary = make_shared<Resource>();
    summary->set_path("/payments-summary");
    summary->set_method_handler("GET", payments_summary_handler);

    const auto profiler = make_shared<Resource>();
    profiler->set_path("/profiler");
    profiler->set_method_handler("GET", profiler_handler);

    auto concurrency = std::thread::hardware_concurrency() * 2;
    const auto env_concurrency = std::getenv("CONCURRENCY");
    if (env_concurrency != nullptr) {
        concurrency = std::stoi(env_concurrency);
    }

    const auto settings = make_shared<Settings>();
    settings->set_port(8080);
    settings->set_worker_limit(concurrency);
    settings->set_keep_alive(true);
    settings->set_default_header("Connection", "keep-alive");
    settings->set_connection_timeout(std::chrono::seconds(60));

    Service rest_service;
    rest_service.publish(payments);
    rest_service.publish(summary);
    rest_service.publish(profiler);
    rest_service.start(settings);
    curl_global_cleanup();
    return EXIT_SUCCESS;
}

