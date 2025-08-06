#include <restbed>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <curl/curl.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <memory>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <limits>

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

struct CurlHandle {
    CURL* handle;
    CurlHandle() : handle(curl_easy_init()) {}
    ~CurlHandle() { if (handle) curl_easy_cleanup(handle); }
};

struct Summary {
    uint64_t totalRequests = 0;
    double totalAmount = 0.0;
};

static size_t write_callback(void* contents, const size_t size, const size_t nmemb, void* userp) {
    static_cast<string*>(userp)->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

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
        "parsing", "put-queue", "fetch-queue", "delete", "process_payment", "create_processor_payload", "send_to_processor",
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

// ==== PROFILER ====

class PaymentService {
public:
    PaymentService() {
        std::cout << "Initializing PaymentService..." << std::endl;

        const char* db_folder = getenv("DATABASE_PATH");
        string queue_db_path = db_folder ? string(db_folder) : "data_";
        string processed_db_path = db_folder ? string(db_folder) : "data_";

        Options options;
        options.create_if_missing = true;
        options.IncreaseParallelism();
        options.OptimizeLevelStyleCompaction();
        DB* qdb;
        DB* pdb;

        std::cout << "Opening RocksDB databases..." << std::endl;
        const Status q_status = DB::Open(options, queue_db_path.append("queue_db_file"), &qdb);
        const Status p_status = DB::Open(options, processed_db_path.append("processed_db_file"), &pdb);

        std::cout << "Databases opened. Checking the status..." << std::endl;

        if (!q_status.ok() || !p_status.ok()) {
            std::cerr << "Failed to open DBs:\n"
                      << "queue_db: " << q_status.ToString() << "\n"
                      << "processed_db: " << p_status.ToString() << std::endl;
            throw runtime_error("Failed to open RocksDB databases");
        }

        std::cout << "Starting database..." << std::endl;

        queue_db.reset(qdb);
        processed_db.reset(pdb);

        std::cout << "Database started..." << std::endl;

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

        std::cout << "Configurations read" << std::endl;
        for (int i = 0; i < 5; ++i) {
            workers.emplace_back([this]{ worker_loop(false); });
        }
        workers.emplace_back([this]{ worker_loop(true); });
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

        uint64_t id = counter.fetch_add(1);
        Document d;
        d.SetObject();
        auto& a = d.GetAllocator();
        d.AddMember("correlationId", StringRef(p.correlationId.c_str()), a);
        d.AddMember("amount", p.amount, a);
        StringBuffer sb; Writer<StringBuffer> w(sb); d.Accept(w);
        queue_db->Put(rocksdb::WriteOptions(), to_string(id), sb.GetString());

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
    void worker_loop(bool isFallbackPool) {
        while (running) {
            Payment p; bool has = fetch_next(p);
            if (!has) {
                this_thread::sleep_for(chrono::milliseconds(100));
                continue;
            }
            auto [processor, ts] = process_payment(p, isFallbackPool);
            if (processor == "try_again") {
                enqueue(p);
            }
            else
            {
                store_processed(p, processor, ts);
            }
            if (isFallbackPool) {
                this_thread::sleep_for(chrono::milliseconds(fallback_interval_ms));
            }
        }
    }

    bool fetch_next(Payment& p) {
        const auto start = get_now();

        lock_guard<mutex> lock(queue_mutex);
        rocksdb::ReadOptions opts;
        opts.fill_cache = false;
        opts.verify_checksums = false;
        unique_ptr<rocksdb::Iterator> it(queue_db->NewIterator(opts));
        it->SeekToFirst();
        if (!it->Valid()) return false;
        const string key = it->key().ToString();
        const string value = it->value().ToString();

        const auto start_delete = get_now();
        queue_db->Delete(rocksdb::WriteOptions(), key);
        record_profiler_value("delete", start);

        Document d; d.Parse(value.c_str());
        p.correlationId = d["correlationId"].GetString();
        p.amount = d["amount"].GetDouble();

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
            trigger_switch();
        }
        else if (from_main_pool && code == 500 && fallback_down.load())
        {
            std::cout << "Primary processor is down, waiting for fallback to recover..." << std::endl;
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

    void trigger_switch()
    {
        const auto now = get_local_time();
        swap(main_url, test_url);

        std::cout << "Switching to " << main_url << " at " << now << std::endl;
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
            if (using_default) trigger_switch();
        } else {
            if (!using_default) trigger_switch();
        }

        record_profiler_value("evaluate_switch", start);
    }

    static pair<string, string> create_processor_payload(const Payment& p) {
        const auto start = get_now();

        auto now = chrono::system_clock::now();
        time_t t = chrono::system_clock::to_time_t(now);
        tm tm{}; gmtime_r(&t, &tm);
        auto ms = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()) % 1000;
        stringstream ss;
        ss << put_time(&tm, "%Y-%m-%dT%H:%M:%S");
        ss << '.' << setw(3) << setfill('0') << ms.count() << 'Z';
        const string requestedAt = ss.str();
        Document d; d.SetObject();
        auto& a = d.GetAllocator();
        d.AddMember("correlationId", StringRef(p.correlationId.c_str()), a);
        d.AddMember("amount", p.amount, a);
        d.AddMember("requestedAt", StringRef(requestedAt.c_str()), a);
        StringBuffer sb; Writer<StringBuffer> w(sb); d.Accept(w);

        record_profiler_value("create_processor_payload", start);

        return {sb.GetString(), requestedAt};
    }

    static bool send_to_processor(const string& base, const string& payload, double& elapsed, long& code) {
        const auto start_method = get_now();

        const string url = base + "/payments";
        string response;
        thread_local CurlHandle curl_wrapper;
        CURL* curl = curl_wrapper.handle;
        if (!curl) { elapsed = 0; code = 0; return false; }
        curl_easy_reset(curl);
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        const auto start = chrono::steady_clock::now();
        const CURLcode res = curl_easy_perform(curl);
        const auto end = chrono::steady_clock::now();
        elapsed = chrono::duration<double, milli>(end - start).count();
        code = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        curl_slist_free_all(headers);

        record_profiler_value("send_to_processor", start_method);

        const auto now = get_local_time();
        std::cout << code << " " << elapsed << " " << url << " at " << now << std::endl;

        return (res == CURLE_OK && code == 200);
    }

    void store_processed(const Payment& p, const string& processor, const string& timestamp) const
    {
        const auto start = get_now();

        Document d; d.SetObject();
        auto& a = d.GetAllocator();
        d.AddMember("timestamp", StringRef(timestamp.c_str()), a);
        d.AddMember("amount", p.amount, a);
        d.AddMember("processor", StringRef(processor.c_str()), a);
        StringBuffer sb; Writer<StringBuffer> w(sb); d.Accept(w);
        const string key = timestamp + "|" + p.correlationId;
        processed_db->Put(rocksdb::WriteOptions(), key, sb.GetString());

        record_profiler_value("store_processed", start);
    }

    unique_ptr<rocksdb::DB> queue_db;
    unique_ptr<rocksdb::DB> processed_db;
    mutex queue_mutex;
    vector<thread> workers;
    atomic<uint64_t> counter{0};
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
    const auto request = session->get_request();
    const int length = request->get_header("Content-Length", 0);
    session->fetch(length, [](const shared_ptr<Session>& session, const Bytes& body)
    {
        const auto start_parse = get_now();
        Document d; d.Parse(reinterpret_cast<const char*>(body.data()), body.size());

        Payment p;
        p.correlationId = d["correlationId"].GetString();
        p.amount = d["amount"].GetDouble();
        record_profiler_value("parsing", start_parse);

        service->enqueue(p);

        const string response = R"({"status": "Accepted"})";
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
    Document d; d.SetObject();
    auto& a = d.GetAllocator();
    Value def(kObjectType); def.AddMember("totalRequests", res["default"].totalRequests, a); def.AddMember("totalAmount", res["default"].totalAmount, a);
    Value fb(kObjectType); fb.AddMember("totalRequests", res["fallback"].totalRequests, a); fb.AddMember("totalAmount", res["fallback"].totalAmount, a);
    d.AddMember("default", def, a);
    d.AddMember("fallback", fb, a);
    StringBuffer sb; Writer<StringBuffer> w(sb); d.Accept(w);
    session->close(200, sb.GetString(), { { "Content-Type", "application/json" } });
}

int main(const int, char**) {
    std::cout << "Starting Payment Service..." << std::endl;
    init_profiler();


    curl_global_init(CURL_GLOBAL_DEFAULT);
    service = make_shared<PaymentService>();

    std::cout << "Initializing the paths..." << std::endl;

    auto payments = make_shared<Resource>();
    payments->set_path("/payments");
    payments->set_method_handler("POST", post_payment_handler);

    auto summary = make_shared<Resource>();
    summary->set_path("/payments-summary");
    summary->set_method_handler("GET", payments_summary_handler);

    auto profiler = make_shared<Resource>();
    profiler->set_path("/profiler");
    profiler->set_method_handler("GET", profiler_handler);

    auto settings = make_shared<Settings>();
    settings->set_port(8080);
    Service rest_service;
    rest_service.publish(payments);
    rest_service.publish(summary);
    rest_service.publish(profiler);
    rest_service.start(settings);
    curl_global_cleanup();
    return EXIT_SUCCESS;
}

