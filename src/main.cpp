#define BUILD_IPC 1


#include <restbed>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <array>
#include <cstring>
#include <curl/curl.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <map>
#include <memory>
#include <cstdlib>
#include <future>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <unordered_set>
#include <vector>
#include <condition_variable>

using namespace restbed;
using namespace std;
using namespace rapidjson;

constexpr bool const_performance_metrics_enabled = false;

enum class ProcessorResult {
    Default,
    Fallback,
    Discard,
    TryAgain
};

struct Payment {
    string correlationId;
    double amount{};
};

struct RawPayment {
    std::array<uint8_t, 70> data{};
    size_t size{};
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
            curl_easy_setopt(handle, CURLOPT_TCP_KEEPIDLE, 300L);
            curl_easy_setopt(handle, CURLOPT_TCP_KEEPINTVL, 15L);
            curl_easy_setopt(handle, CURLOPT_MAXLIFETIME_CONN, 300L);
            curl_easy_setopt(handle, CURLOPT_MAXAGE_CONN, 300L);
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

struct SummaryPair {
    Summary def;
    Summary fb;
};

// ==== PROFILER ====

unordered_map<string, std::atomic<long>> performance_data;
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

void reset_profiler()
{
    for (const std::string& key : {
        "parsing", "put-queue", "fetch-queue", "process_payment", "create_processor_payload", "send_to_processor",
        "store_processed", "evaluate_switch", "handle_response"
    }) {
        performance_data.at(key).store(0);
        count_perf_data.at(key).store(0);
    }
}

string get_local_time()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
    localtime_r(&t, &local_tm);

    // Get milliseconds part
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setw(3) << std::setfill('0') << ms.count();

    return ss.str();
}

static uint64_t parse_timestamp_ms(const string& ts)
{
    std::tm tm{};
    int ms = 0;
    if (sscanf(ts.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.%3dZ",
               &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &ms) != 7) {
        return 0;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    time_t seconds = timegm(&tm);
    return static_cast<uint64_t>(seconds) * 1000ULL + static_cast<uint64_t>(ms);
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

    reset_profiler();

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

class PaymentService {
public:
    static constexpr size_t QUEUE_CAPACITY = 10'000;

    PaymentService() {
        std::cout << "Initializing PaymentService v1.0..." << std::endl;
        const char* workerCount = getenv("WORKER_COUNT");
        int workerCountInt = workerCount ? atoi(workerCount) : 1;

        queues.reserve(workerCountInt);
        for (int i = 0; i < workerCountInt; ++i) {
            auto q = std::make_unique<WorkerQueue>();
            q->queue.resize(QUEUE_CAPACITY);
            queues.emplace_back(std::move(q));
        }
        std::cout << "WORKER_COUNT=" << workerCountInt << std::endl;

        const char* interIntervalCount = getenv("INTER_INTERVAL");
        inter_interval = interIntervalCount ? atoi(interIntervalCount) : 0;

        const char* def = getenv("PROCESSOR_URL");
        const char* fb = getenv("FALLBACK_PROCESSOR_URL");
        default_processor = def ? string(def) : "http://localhost:8001";
        fallback_processor = fb ? string(fb) : default_processor;
        main_url = default_processor;
        test_url = fallback_processor;

        const char* fee = getenv("FEE_DIFFERENCE");
        fee_difference = fee ? atof(fee) : 3;

        const char* pace = getenv("FALLBACK_POOL_INTERVAL_MS");
        fallback_interval_ms = pace ? atoi(pace) : 1000;
        std::cout << "FALLBACK_POOL_INTERVAL_MS=" << fallback_interval_ms << std::endl;

        const char* fallbackEnabledStr = getenv("FALLBACK_ENABLED");
        fallback_enabled = fallbackEnabledStr ? atoi(fallbackEnabledStr) : true;
        if (!fallback_enabled)
        {
            fallback_down = true;
        }
        std::cout << "FALLBACK_ENABLED=" << fallback_enabled << std::endl;

        std::cout << "Configurations read" << std::endl;
        for (int i = 0; i < workerCountInt; ++i) {
            workers.emplace_back([this, i]{ worker_loop(i); });
        }

        if (const_performance_metrics_enabled)
        {
            // workers.emplace_back([this]{ profiler_loop(); });
        }
        std::cout << "Threads started" << std::endl;

        started = get_now();
    }

    ~PaymentService() {
        running = false;
        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }
    }

    void enqueue(size_t queue_idx, const RawPayment& r) {
        //const auto start = get_now();
        WorkerQueue& q = *queues[queue_idx % queues.size()];
        {
            size_t pos = q.tail++;
            q.queue[pos % q.queue.size()] = r;
        }

        //record_profiler_value("put-queue", start);
    }

    size_t queue_count() const {
        return queues.size();
    }

    SummaryPair query(const string& from, const string& to) const
    {
        SummaryPair result;

        uint64_t from_ms = parse_timestamp_ms(from);
        uint64_t to_ms = parse_timestamp_ms(to);
        if (to_ms < from_ms) {
            return result;
        }

        const uint64_t from_sec = ((from_ms + 1000) / 1000) * 1000;
        const uint64_t to_sec = (to_ms / 1000) * 1000;

        for (uint64_t ts = from_sec; ts <= to_sec; ts += 1000) {
            if (auto it = processed_default_map.find(ts); it != processed_default_map.end()) {
                result.def.totalRequests += it->second.totalRequests;
                result.def.totalAmount += it->second.totalAmount;
            }
            if (auto it = processed_fallback_map.find(ts); it != processed_fallback_map.end()) {
                result.fb.totalRequests += it->second.totalRequests;
                result.fb.totalAmount += it->second.totalAmount;
            }
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
    void profiler_loop() const
    {
        while (running)
        {
            const string response_str = get_profiler_result();
            std::cout << response_str << std::endl;
            this_thread::sleep_for(chrono::seconds(5));
        }
    }

    void worker_loop(size_t worker_id) {
        thread_local bool pinned = false;
        if (!pinned) {
            static std::atomic<int> next_cpu{0};
            int total_cores = std::thread::hardware_concurrency();
            int cpu_id = (next_cpu++ % (total_cores / 2)) + (total_cores / 2);
            // pin_thread_to_core(cpu_id, "worker");
            pinned = true;
        }

        last_fallback = get_now();

        while (running) {
            RawPayment r;
            bool has = fetch_next(worker_id, r);
            if (!has) {
                this_thread::sleep_for(chrono::milliseconds(1));
                continue;
            }

            bool isFallbackPool = false;

            if (fallback_enabled)
            {
                const auto now = get_now();
                int elapsed = chrono::duration_cast<chrono::milliseconds>(now - last_fallback).count();
                if (!fallback_is_running && elapsed >= fallback_interval_ms) {
                    fallback_is_running = true;
                    isFallbackPool = true;
                }
            }

            //const auto start_parse = get_now();
            const auto json = reinterpret_cast<const char*>(r.data.data());
            auto json_str = string(json, r.size);
            //record_profiler_value("parsing", start_parse);

            auto [processor, ts] = process_payment(json_str, isFallbackPool);
            if (processor == ProcessorResult::TryAgain) {
                enqueue(worker_id, r);
            }
            else
            {
                store_processed(json_str, processor, ts);
            }

            if (isFallbackPool)
            {
                fallback_is_running = false;
                last_fallback = get_now();
            }

            if (inter_interval > 0)
            {
                this_thread::sleep_for(chrono::microseconds(inter_interval));
            }
        }
    }

    bool fetch_next(size_t worker_id, RawPayment& r) {
        //const auto start = get_now();

        WorkerQueue& q = *queues[worker_id];
        if (!running || q.head >= q.tail) {
            return false;
        }
        size_t idx = q.head++;
        r = q.queue[idx % q.queue.size()];

        //record_profiler_value("fetch-queue", start);

        return true;
    }

    pair<ProcessorResult, uint64_t> process_payment(string& json, bool isFallbackPool)
    {
        //const auto start = get_now();

        auto [payload, ts] = create_processor_payload(json);
        const string primary = isFallbackPool ? test_url : main_url;
        const string secondary = isFallbackPool ? main_url : test_url;

        /* preciso melhorar isso */
        //ProcessorResult primary_label = (primary == default_processor ? ProcessorResult::Default : ProcessorResult::Fallback);
        //ProcessorResult secondary_label = (secondary == default_processor ? ProcessorResult::Default : ProcessorResult::Fallback);

        auto primary_label = ProcessorResult::Default;
        auto secondary_label = ProcessorResult::Fallback;

        double elapsed = 0.0; long code = 0;
        bool ok = send_to_processor(primary, payload, elapsed, code);
        if (isFallbackPool)
        {
            fallback_evaluated = false;
        }

        handle_response(primary, elapsed, code);
        if (ok) {

            //record_profiler_value("process_payment", start);
            return {primary_label, ts};
        }
        if (code == 422)
        {
            //record_profiler_value("process_payment", start);
            return {ProcessorResult::Discard, ts};
        }

        if (!fallback_enabled)
        {
            return {ProcessorResult::TryAgain, ts};
        }

        double elapsed2 = 0.0; long code2 = 0;
        ok = send_to_processor(secondary, payload, elapsed2, code2);
        handle_response(secondary, elapsed2, code2);
        if (ok) {
            //record_profiler_value("process_payment", start);
            return {secondary_label, ts};
        }
        if (code == 422)
        {
           // record_profiler_value("process_payment", start);
            return {ProcessorResult::Discard, ts};
        }
        //record_profiler_value("process_payment", start);
        return {ProcessorResult::TryAgain, ts};
    }

    void handle_response(const string& url, const double elapsed, const long code)
    {
        //const auto start = get_now();
        if (fallback_enabled)
        {
            const bool using_default_as_main = (main_url == default_processor);
            const bool default_called = (url == default_processor);

            if (code == 500)
            {
                if (default_called) default_down = true;
                else fallback_down = true;

                if (default_down && fallback_down)
                {
                    print_log("Both is down, waiting to recover... " + get_local_time());
                    this_thread::sleep_for(chrono::milliseconds(fallback_interval_ms / 2));
                }
                else if (default_down && !fallback_down && using_default_as_main)
                {
                    trigger_switch("Main down");
                }
                else if (!default_down && fallback_down && !using_default_as_main)
                {
                    trigger_switch("Fallback down");
                }
                return;
            }

            if (default_called && default_down)
            {
                default_down = false;
                if (fallback_down)
                {
                    trigger_switch("Default recovered");
                }
            }
            else if (!default_called && fallback_down)
            {
                fallback_down = false;
                if (default_down)
                {
                    trigger_switch("Fallback recovered");
                }
            }

            update_time(url, elapsed);
            evaluate_switch();
        }

        //record_profiler_value("handle_response", start);
    }

    void update_time(const string& url, double elapsed)
    {
        if (url == default_processor) {
            default_time_ms = elapsed;
        } else if (url == fallback_processor) {
            fallback_time_ms = elapsed;
        }
    }

    void trigger_switch(const string& reason)
    {
        if (!fallback_enabled)
        {
            return;
        }

        swap(main_url, test_url);

        if (!const_performance_metrics_enabled)
        {
            return;
        }
        const auto now = get_local_time();
        std::cout << reason << " - switching to " << main_url << " at " << now << std::endl;
    }

    void evaluate_switch()
    {
        if (!fallback_enabled || fallback_evaluated) {
            return;
        }
        fallback_evaluated = true;

        //const auto start = get_now();

        const double def = default_time_ms;
        const double fb = fallback_time_ms;
        if (def == 0 || fb == 0) return;
        const double improvement = def / fb; // def= 6ms, fb= 2ms, def / fb= 3
        const bool using_default = (main_url == default_processor);
        if (fb < def && improvement >= fee_difference) {
            if (using_default)
            {
                std::cout << "def: " << def << " fb: " << fb << std::endl;
                trigger_switch("Fallback better");
            }
        } else {
            if (!using_default)
            {
                std::cout << "def: " << def << " fb: " << fb << std::endl;
                trigger_switch("Main better");
            }
        }

        //record_profiler_value("evaluate_switch", start);
    }

    static pair<string, uint64_t> create_processor_payload(string& json) {
        //const auto start = get_now();

        // ---- Fast timestamp formatting ----
        char requestedAt[32];
        auto now = chrono::system_clock::now();
        long long ms_since_epoch = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count();
        ms_since_epoch = (ms_since_epoch / 1000) * 1000; // truncate to seconds
        time_t t = ms_since_epoch / 1000;
        tm tm{};
        gmtime_r(&t, &tm);

        snprintf(requestedAt, sizeof(requestedAt),
                 "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec);

        // remove lastcharacter
        json.pop_back();

        // add requestedAt field
        json += R"(,"requestedAt":")" + string(requestedAt) + R"("})";

        //record_profiler_value("create_processor_payload", start);

        return {json, static_cast<uint64_t>(ms_since_epoch)};
    }

    static bool send_to_processor(const string& base, const string& payload, double& elapsed, long& code) {
        //const auto start_method = get_now();

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
        const CURLcode res = curl_easy_perform(curl);
        const auto end = chrono::steady_clock::now();

        elapsed = chrono::duration<double, milli>(end - start).count();
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

        /*
        if constexpr (const_performance_metrics_enabled)
        {
            print_log(url + " - " + to_string(code) + " - " + to_string(elapsed) + "ms" + " " + get_local_time());
        }
        */

        //record_profiler_value("send_to_processor", start_method);

        return (res == CURLE_OK && code == 200);
    }

    void store_processed(const string& json, const ProcessorResult processor, const uint64_t timestamp) const
    {
        //const auto start = get_now();

        // extract amount from JSON using string/char manipulation
        double amount = 0.0;
        size_t amount_pos = json.find("\"amount\":");
        if (amount_pos != string::npos) {
            const size_t start_pos = amount_pos + 9; // length of "\"amount\":"
            size_t end_pos = json.find_first_of(",}", start_pos);
            if (end_pos != string::npos) {
                const string amount_str = json.substr(start_pos, end_pos - start_pos);
                amount = stod(amount_str);
            }
        }

        auto& map = fallback_enabled ?
                    (processor == ProcessorResult::Fallback ? processed_fallback_map : processed_default_map)
                    : processed_default_map;

        Summary& s = map[timestamp];
        s.totalRequests++;
        s.totalAmount += amount;

        //record_profiler_value("store_processed", start);
    }

    struct ProcessedSlot {
        int count;
        double amount;
    };

    mutable unordered_map<uint64_t, Summary> processed_default_map;
    mutable unordered_map<uint64_t, Summary> processed_fallback_map;

    struct WorkerQueue {
        vector<RawPayment> queue;
        size_t head{0};
        size_t tail{0};
        std::condition_variable cv;
    };

    vector<unique_ptr<WorkerQueue>> queues;
    vector<thread> workers;
    string default_processor;
    string fallback_processor;
    string main_url;
    string test_url;
    double default_time_ms{0.0};
    double fallback_time_ms{0.0};
    bool fallback_down{false};
    bool default_down{false};
    double fee_difference{0.0};
    int fallback_interval_ms{1000};
    atomic<bool> running{true};
    std::chrono::high_resolution_clock::time_point last_fallback;
    std::chrono::high_resolution_clock::time_point started;
    bool fallback_is_running{false};
    bool fallback_enabled{true};
    bool fallback_evaluated{true};
    int inter_interval{0};
};

static shared_ptr<PaymentService> service;

void post_payment_handler(const shared_ptr<Session>& session) {
    const auto request = session->get_request();
    constexpr size_t length = 70; // Fixed length for simplicity, can be adjusted based on expected payload size
    //size_t length = request->get_header("Content-Length", 0);
    static atomic<size_t> queue_index = {0};

    session->fetch(length, [&](const shared_ptr<Session>& session, const Bytes& body) {

        // std::thread([body]{
            RawPayment r;
            r.size = body.size();
            std::memcpy(r.data.data(), body.data(), body.size());

            const size_t idx = queue_index.fetch_add(1, std::memory_order_relaxed);
            service->enqueue(idx % service->queue_count(), r);
        // }).detach();

        static const Bytes response;
        static const multimap<string, string> headers = {
            {"Content-Length", "0"}
        };

        session->yield(202, response, headers);
    });
}

SummaryPair call_other_instance(const string& other, const string& from, const string& to) {
    SummaryPair result;
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
            result.def.totalRequests = def["totalRequests"].GetUint64();
            result.def.totalAmount = def["totalAmount"].GetDouble();
        }
        if (d.HasMember("fallback")) {
            auto& fb = d["fallback"];
            result.fb.totalRequests = fb["totalRequests"].GetUint64();
            result.fb.totalAmount = fb["totalAmount"].GetDouble();
        }
    }
    return result;
}

void payments_summary_handler(const shared_ptr<Session>& session) {
    const auto request = session->get_request();
    string from = request->get_query_parameter("from");
    string to = request->get_query_parameter("to");
    string internal = request->get_query_parameter("internal", "false");

    const char* other_url = nullptr;
    if (internal != "true")
    {
        other_url = getenv("OTHER_INSTANCE_URL");
    }

    std::future<SummaryPair> other_future;
    if (other_url != nullptr) {
        other_future = std::async(std::launch::async, [other_url, from, to] {
            return call_other_instance(other_url, from, to);
        });
    }

    auto res = service->query(from, to);
    if (other_url != nullptr) {
        auto otherRes = other_future.get();
        res.def.totalRequests += otherRes.def.totalRequests;
        res.def.totalAmount += otherRes.def.totalAmount;
        res.fb.totalRequests += otherRes.fb.totalRequests;
        res.fb.totalAmount += otherRes.fb.totalAmount;
    }

    // === JSON serialization with reusable RapidJSON components ===
    thread_local rapidjson::MemoryPoolAllocator<> allocator;
    allocator.Clear();

    thread_local rapidjson::Document d(&allocator);
    d.SetObject();
    auto& a = d.GetAllocator();

    // Build default object
    rapidjson::Value def(rapidjson::kObjectType);
    def.AddMember("totalRequests", res.def.totalRequests, a);
    def.AddMember("totalAmount", res.def.totalAmount, a);

    // Build fallback object
    rapidjson::Value fb(rapidjson::kObjectType);
    fb.AddMember("totalRequests", res.fb.totalRequests, a);
    fb.AddMember("totalAmount", res.fb.totalAmount, a);

    d.AddMember("default", def, a);
    d.AddMember("fallback", fb, a);

    thread_local rapidjson::StringBuffer sb;
    sb.Clear();

    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    d.Accept(writer);

    session->close(200, sb.GetString(), {
        { "Content-Type", "application/json" },
        { "Content-Length", to_string(sb.GetSize())},
        {"Connection", "keep-alive"}
    });
}

int main(const int, char**) {
    std::cout << "Starting Payment Service..." << std::endl;
    init_profiler();

    const auto socket = std::getenv("SOCKET");

    // remove stale socket from previous run (important for UDS)
    ::unlink(socket);  // safe if it doesn't exist
    ::mkdir("/var/tmp/sockets", 0777); // best-effort
    ::umask(0000);

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

    std::cout << "CONCURRENCY=" << concurrency << std::endl;

    const auto settings = make_shared<Settings>();

    settings->set_port(8080);
    settings->set_ipc_path(socket);

    settings->set_worker_limit(concurrency);
    settings->set_keep_alive(true);
    settings->set_default_header("Connection", "keep-alive");
    settings->set_connection_timeout(std::chrono::seconds(300));

    Service rest_service;
    rest_service.publish(payments);
    rest_service.publish(summary);
    rest_service.publish(profiler);
    rest_service.start(settings);

    curl_global_cleanup();
    return EXIT_SUCCESS;
}

