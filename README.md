# Rinha Backend 2025

A high-performance C++ backend for payment processing, designed for the Rinha de Backend challenge.  
Implements a REST API for payment submission and summary, with an in-memory store for the last 50k processed requests, cURL for HTTP, and RapidJSON for fast JSON handling.

## Features

- **REST API** using [restbed](https://github.com/Corvusoft/restbed)
- **In-memory storage** for the most recent 50k processed payments
- **High concurrency**: lock-free queue, multi-threaded workers
- **Processor failover**: automatic fallback and switch logic
- **Performance profiling** (optional)
- **Millisecond-precision timestamps** in ISO 8601 format

## Endpoints

- `POST /payments`  
  Accepts a payment JSON:
  ```json
  {
    "correlationId": "string",
    "amount": 123.45
  }
  ```
  Returns:  
  `{"status": "Accepted"}`

- `GET /payments-summary?from=...&to=...`  
  Returns total requests and amounts for each processor between two timestamps.

- `GET /profiler`  
  (If enabled) Returns performance metrics.

## Implementation Details

### Core Components

- **PaymentService**:
    - Manages a lock-free ring buffer queue for incoming payments.
      - Worker threads process payments, send to processor(s), and store results in an internal ring buffer (50k entries).
    - Automatic failover: if the main processor fails, switches to fallback, and vice versa.
    - Uses atomic variables for thread safety.

- **Timestamps**:
    - All timestamps use UTC and include milliseconds:  
      Example: `2024-06-07T15:23:45.123Z`
    - Formatting uses `std::put_time` and manual millisecond extraction.

- **Storage**:
    - In-memory ring buffer stores the latest 50k processed payments.
    - Each record includes timestamp, amount, and processor used.

- **HTTP/JSON**:
    - [restbed](https://github.com/Corvusoft/restbed) for REST API.
    - [RapidJSON](https://github.com/Tencent/rapidjson) for fast, zero-copy JSON parsing and serialization.
    - [cURL](https://curl.se/libcurl/) for outgoing HTTP requests to processors and other instances.

- **Performance Profiling**:
    - Optional, controlled by `const_performance_metrics_enabled`.
    - Tracks microsecond timings for key operations.

### Environment Variables

- `PROCESSOR_URL`: Main processor URL (default: `http://localhost:8001`)
- `FALLBACK_PROCESSOR_URL`: Fallback processor URL (default: same as main)
- `FEE_DIFFERENCE`: Minimum improvement ratio to switch processors (default: `0.11`)
- `FALLBACK_POOL_INTERVAL_MS`: Interval for fallback pool in ms (default: `1000`)
- `WORKER_COUNT`: Number of worker threads (default: `5`)
- `OTHER_INSTANCE_URL`: (Optional) URL of another instance for distributed summary
- `CONCURRENCY`: Number of REST worker threads (default: `hardware_concurrency * 2`)

### Build & Run

**Dependencies:**
- C++17 or newer
- [restbed](https://github.com/Corvusoft/restbed)
- [RapidJSON](https://github.com/Tencent/rapidjson)
- [libcurl](https://curl.se/libcurl/)

**Example (on macOS/Linux):**
```sh
# Install dependencies (example for Ubuntu)
sudo apt-get install librestbed-dev rapidjson-dev libcurl4-openssl-dev

# Build
mkdir build && cd build
cmake ..
make

# Run
./rinha-backend-2025
```

## Notes

- Thread pinning is only supported on Linux.
- Millisecond-precision timestamps are used throughout for accurate event tracking.
- The queue and processed buffer are lock-free for high throughput.

---

**Author:** MarceloODias  
**License:** MIT (see `LICENSE`)