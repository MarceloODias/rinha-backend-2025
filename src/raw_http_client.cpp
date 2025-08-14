#include "raw_http_client.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <cctype>

#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/tcp.h>

static inline int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static inline int poll_wait(int fd, short events, int timeout_ms) {
    struct pollfd p{fd, events, 0};
    for (;;) {
        int r = poll(&p, 1, timeout_ms);
        if (r >= 0) return r;
        if (errno == EINTR) continue;
        return -1;
    }
}

static inline int check_connect_error(int fd) {
    int soerr = 0; socklen_t len = sizeof(soerr);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) < 0) return errno ? errno : -1;
    return soerr;
}

static inline ssize_t sendv_nosig(int fd, struct iovec* iov, int iovcnt) {
#ifdef MSG_NOSIGNAL
    struct msghdr msg{};
    msg.msg_iov = iov;
    msg.msg_iovlen = (size_t)iovcnt;
    return sendmsg(fd, &msg, MSG_NOSIGNAL);
#else
    return writev(fd, iov, iovcnt);
#endif
}

RawHttpClient::RawHttpClient(std::string host, int port,
                             std::string method, std::string path,
                             std::string extra_headers,
                             size_t fixed_content_length,
                             int connect_timeout_ms,
                             int io_timeout_ms,
                             size_t max_header_bytes)
    : host_(std::move(host)), port_(port), method_(std::move(method)), path_(std::move(path)),
      extra_headers_(std::move(extra_headers)), body_len_(fixed_content_length),
      connect_to_ms_(connect_timeout_ms), io_to_ms_(io_timeout_ms), max_hdr_(max_header_bytes) {
    if (!resolve_all()) throw std::runtime_error("DNS resolution failed");
    build_request_prefix();
}

RawHttpClient::~RawHttpClient() { close_socket(); }

int RawHttpClient::send_request(const char* body, size_t len, int& status_out) {
    if (len != body_len_) { errno = EINVAL; return -1; }

    if (sock_ < 0) {
        if (connect_any() < 0) return -1;
    }

    if (write_all(body, len) < 0) {
        int err = errno;
        if (!sent_any_) {
            close_socket();
            if (connect_any() == 0 && write_all(body, len) == 0) {
            } else {
                errno = err;
                return -1;
            }
        } else {
            return -1;
        }
    }

    int st = -1;
    int rc = read_status_and_drain(st);
    if (rc < 0) return -1;
    status_out = st;
    return 0;
}

void RawHttpClient::close_socket() {
    if (sock_ >= 0) { close(sock_); sock_ = -1; }
    sent_any_ = false;
}

void RawHttpClient::build_request_prefix() {
    req_prefix_.reserve(256 + extra_headers_.size());
    req_prefix_.append(method_).append(" ").append(path_).append(" HTTP/1.1\r\n");
    req_prefix_.append("Host: ").append(host_).append(":").append(std::to_string(port_)).append("\r\n");
    req_prefix_.append("Connection: keep-alive\r\n");
    if (!extra_headers_.empty()) req_prefix_.append(extra_headers_);
    req_prefix_.append("Content-Type: application/json\r\n");
    req_prefix_.append("Content-Length: ").append(std::to_string(body_len_)).append("\r\n");
    req_prefix_.append("\r\n");
}

bool RawHttpClient::resolve_all() {
    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_ADDRCONFIG;

    char portstr[16];
    std::snprintf(portstr, sizeof(portstr), "%d", port_);

    struct addrinfo* res = nullptr;
    int rc = getaddrinfo(host_.c_str(), portstr, &hints, &res);
    if (rc != 0 || !res) return false;

    for (auto* ai = res; ai; ai = ai->ai_next) {
        std::vector<char> blob(ai->ai_addrlen);
        std::memcpy(blob.data(), ai->ai_addr, ai->ai_addrlen);
        addrs_.push_back(std::move(blob));
    }
    freeaddrinfo(res);
    return !addrs_.empty();
}

int RawHttpClient::connect_any() {
    const size_t n = addrs_.size();
    for (size_t attempt = 0; attempt < n; ++attempt) {
        size_t idx = (next_addr_idx_ + attempt) % n;
        if (connect_one(addrs_[idx].data(), addrs_[idx].size()) == 0) {
            next_addr_idx_ = (idx + 1) % n;
            return 0;
        }
    }
    return -1;
}

int RawHttpClient::connect_one(const void* sa, size_t salen) {
    close_socket();
    sock_ = ::socket(((const struct sockaddr*)sa)->sa_family, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock_ < 0) return -1;

    int on = 1;
    setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

    int rc = ::connect(sock_, (const struct sockaddr*)sa, (socklen_t)salen);
    if (rc == 0) return 0;
    if (rc < 0 && errno != EINPROGRESS) { close_socket(); return -1; }

    rc = poll_wait(sock_, POLLOUT, connect_to_ms_);
    if (rc <= 0) { errno = (rc==0)? ETIMEDOUT : errno; close_socket(); return -1; }

    int soerr = check_connect_error(sock_);
    if (soerr != 0) { errno = soerr; close_socket(); return -1; }

    return 0;
}

int RawHttpClient::write_all(const char* body, size_t len) {
    sent_any_ = false;

    struct iovec iov[2];
    iov[0].iov_base = (void*)req_prefix_.data();
    iov[0].iov_len  = req_prefix_.size();
    iov[1].iov_base = (void*)body;
    iov[1].iov_len  = len;

    size_t total = iov[0].iov_len + iov[1].iov_len;
    size_t sent  = 0;

    int i = 0;
    size_t off = 0;

    while (sent < total) {
        int rc = poll_wait(sock_, POLLOUT, io_to_ms_);
        if (rc <= 0) { errno = (rc==0)? ETIMEDOUT : errno; return -1; }

        struct iovec cur[2];
        int cnt = 0;
        if (i == 0) {
            cur[cnt++] = { (char*)iov[0].iov_base + off, iov[0].iov_len - off };
            if (off >= iov[0].iov_len) { i = 1; off = 0; continue; }
            if (iov[1].iov_len > 0) cur[cnt++] = iov[1];
        } else {
            cur[cnt++] = { (char*)iov[1].iov_base + off, iov[1].iov_len - off };
        }

        ssize_t n = sendv_nosig(sock_, cur, cnt);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        sent_any_ = sent_any_ || (n > 0);
        sent += (size_t)n;

        size_t adv = (size_t)n;
        while (adv > 0) {
            size_t left = (i == 0 ? (iov[0].iov_len - off) : (iov[1].iov_len - off));
            if (adv < left) { off += adv; adv = 0; }
            else {
                adv -= left;
                i += 1; off = 0;
                if (i > 1) break;
            }
        }
    }
    return 0;
}

int RawHttpClient::read_status_and_drain(int& status_out) {
    status_out = -1;

    std::vector<char> hdr;
    hdr.reserve(4096);
    size_t header_end = std::string::npos;

    for (;;) {
        if (hdr.size() >= 4) {
            for (size_t pos = (hdr.size() >= 3 ? hdr.size() - 3 : 0); pos + 3 <= hdr.size(); ++pos) {
                if (hdr[pos] == '\r' && hdr[pos+1] == '\n' && hdr[pos+2] == '\r' && hdr[pos+3] == '\n') {
                    header_end = pos + 4;
                    break;
                }
            }
            if (header_end != std::string::npos) break;
        }

        if (hdr.size() > max_hdr_) { errno = EMSGSIZE; return -1; }

        int rc = poll_wait(sock_, POLLIN, io_to_ms_);
        if (rc <= 0) { errno = (rc==0)? ETIMEDOUT : errno; return -1; }

        char chunk[4096];
        ssize_t n = ::recv(sock_, chunk, sizeof(chunk), 0);
        if (n == 0) { errno = ECONNRESET; return -1; }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        hdr.insert(hdr.end(), chunk, chunk + n);
    }

    const char* base = hdr.data();
    const char* end  = hdr.data() + header_end;
    const char* crlf = (const char*)memmem(base, header_end, "\r\n", 2);
    if (!crlf) { errno = EPROTO; return -1; }
    int major=0, minor=0, code=-1;
    if (std::sscanf(base, "HTTP/%d.%d %d", &major, &minor, &code) != 3 || code < 0) {
        errno = EPROTO; return -1;
    }
    status_out = code;

    ssize_t content_len = -1;
    bool connection_close = false;

    auto iequal = [](char a, char b){ return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); };
    auto ifind  = [&](const char* h, size_t hlen, const char* key)->const char* {
        size_t klen = std::strlen(key);
        for (size_t i = 0; i + klen <= hlen; ++i) {
            if (std::equal(key, key + klen, h + i, iequal)) return h + i;
        }
        return nullptr;
    };

    const char* headers = base;
    size_t headers_len  = header_end;

    if (const char* p = ifind(headers, headers_len, "Content-Length:")) {
        const char* q = p + std::strlen("Content-Length:");
        while (q < end && (*q == ' ' || *q == '\t')) ++q;
        long long v = 0;
        while (q < end && *q >= '0' && *q <= '9') { v = v*10 + (*q - '0'); ++q; }
        if (v >= 0) content_len = (ssize_t)v;
    }

    if (const char* p = ifind(headers, headers_len, "Connection:")) {
        const char* q = p + std::strlen("Connection:");
        while (q < end && (*q == ' ' || *q == '\t')) ++q;
        const char* eol = (const char*)memmem(q, end - q, "\r\n", 2);
        if (!eol) eol = end;
        const char close_str[] = "close";
        const size_t cs = sizeof(close_str)-1;
        for (const char* s = q; s + cs <= eol; ++s) {
            if (std::equal(close_str, close_str+cs, s, iequal)) { connection_close = true; break; }
        }
    }

    size_t already_read_body = hdr.size() - header_end;
    ssize_t to_drain = -1;

    if (content_len >= 0) {
        to_drain = content_len - (ssize_t)already_read_body;
        if (to_drain < 0) to_drain = 0;
    } else {
        connection_close = true;
    }

    if (to_drain >= 0) {
        char sink[8192];
        while (to_drain > 0) {
            int rc = poll_wait(sock_, POLLIN, io_to_ms_);
            if (rc <= 0) { errno = (rc==0)? ETIMEDOUT : errno; return -1; }
            ssize_t n = ::recv(sock_, sink, (size_t)std::min<ssize_t>(to_drain, (ssize_t)sizeof(sink)), 0);
            if (n <= 0) {
                if (n == 0) { errno = ECONNRESET; }
                return -1;
            }
            to_drain -= n;
        }
    } else {
        char sink[8192];
        for (;;) {
            int rc = poll_wait(sock_, POLLIN, io_to_ms_);
            if (rc == 0) { errno = ETIMEDOUT; return -1; }
            if (rc < 0)  { return -1; }
            ssize_t n = ::recv(sock_, sink, sizeof(sink), 0);
            if (n == 0) break;
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                return -1;
            }
        }
    }

    if (connection_close || content_len < 0) {
        close_socket();
    }
    return 0;
}

