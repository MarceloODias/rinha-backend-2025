#pragma once

#include <string>
#include <vector>
#include <cstddef>

class RawHttpClient {
public:
    RawHttpClient(std::string host, int port,
                  std::string method, std::string path,
                  std::string extra_headers,
                  size_t fixed_content_length,
                  int connect_timeout_ms = 1000,
                  int io_timeout_ms = 1000,
                  size_t max_header_bytes = 64 * 1024);

    ~RawHttpClient();

    // Sends one request with a body of exactly body_len_ bytes; outputs HTTP status.
    // Returns 0 on success (status_out filled), or -1 on error (errno set where possible).
    int send_request(const char* body, size_t len, int& status_out);

private:
    std::string host_;
    int         port_ = 80;
    std::string method_, path_, extra_headers_;
    size_t      body_len_ = 0;

    int         connect_to_ms_ = 1000;
    int         io_to_ms_ = 1000;
    size_t      max_hdr_ = 64 * 1024;

    int         sock_ = -1;
    bool        sent_any_ = false;

    std::string req_prefix_;

    std::vector<std::vector<char>> addrs_;
    size_t next_addr_idx_ = 0;

    void close_socket();
    void build_request_prefix();
    bool resolve_all();
    int connect_any();
    int connect_one(const void* sa, size_t salen);
    int write_all(const char* body, size_t len);
    int read_status_and_drain(int& status_out);
};

