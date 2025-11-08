#include "server.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include "imsi_to_bcd.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <sstream>
#include <iomanip>
#include <signal.h>

Server::Server(Config cfg) : cfg_(std::move(cfg)) {
    try {
        auto logger = spdlog::basic_logger_mt("pgw_logger", cfg_.log_file);
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
        spdlog::flush_on(spdlog::level::info);
    } catch (const spdlog::spdlog_ex &ex) {
        spdlog::warn("Failed to create file logger '{}': {}", cfg_.log_file, ex.what());
    }

    if (cfg_.log_level == "debug") spdlog::set_level(spdlog::level::debug);
    else if (cfg_.log_level == "info") spdlog::set_level(spdlog::level::info);
    else if (cfg_.log_level == "warn") spdlog::set_level(spdlog::level::warn);
    else if (cfg_.log_level == "err" || cfg_.log_level == "error") spdlog::set_level(spdlog::level::err);
    else spdlog::set_level(spdlog::level::info);

    cdr_out_.open(cfg_.cdr_file, std::ios::app);
    if (!cdr_out_) {
        spdlog::error("Failed to open CDR file '{}'", cfg_.cdr_file);
    } else {
        spdlog::info("CDR file opened: {}", cfg_.cdr_file);
    }
}

Server::~Server() {
    stop();
    if (http_thread_.joinable()) {
        try { http_thread_.join(); } catch (...) {}
    }
    if (cdr_out_.is_open()) cdr_out_.close();
}

void Server::start() {
    if (running_) {
        spdlog::warn("Server already running");
        return;
    }
    running_ = true;
    spdlog::info("Starting server: UDP {}:{}, HTTP on {}",
                 cfg_.udp_ip, cfg_.udp_port, cfg_.http_port);

    http_thread_ = std::thread(&Server::http_loop, this);
    udp_loop();

    if (http_thread_.joinable()) {
        try { http_thread_.join(); } catch (const std::exception &e) {
            spdlog::warn("Exception while joining http thread: {}", e.what());
        } catch (...) {
            spdlog::warn("Unknown exception while joining http thread");
        }
    }

    spdlog::info("Server stopped");
}

void Server::stop() {
    if (!running_) return;

    spdlog::info("Stop requested: initiating graceful shutdown");
    if (!offloading_) {
        start_offload(static_cast<size_t>(std::max(1, cfg_.graceful_shutdown_rate)));
    }

    stop_http_server();

    for (int i = 0; i < 10 && running_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    running_ = false;
}

bool Server::is_active(const std::string &imsi) {
    std::lock_guard<std::mutex> lk(sess_m_);
    return sessions_.find(imsi) != sessions_.end();
}

void Server::stop_http_server() {
    auto svr = http_svr_;
    if (svr) {
        try { svr->stop(); }
        catch (const std::exception &e) { spdlog::warn("Exception stopping http server: {}", e.what()); }
        catch (...) { spdlog::warn("Unknown exception stopping http server"); }
    }
}

std::string Server::now_ts() {
    std::time_t t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", std::localtime(&t));
    return std::string(buf);
}

void Server::append_cdr(const std::string &imsi, const std::string &action) {
    std::lock_guard<std::mutex> lk(cdr_m_);
    if (!cdr_out_) {
        spdlog::error("CDR file not available; cannot write CDR for {} {}", imsi, action);
        return;
    }
    cdr_out_ << now_ts() << ", " << imsi << ", " << action << "\n";
    cdr_out_.flush();
}

bool Server::is_blacklisted(const std::string &imsi) {
    for (const auto &b : cfg_.blacklist) {
        if (b == imsi) return true;
    }
    return false;
}

static size_t remove_sessions_batch(std::unordered_map<std::string, std::chrono::steady_clock::time_point> &sessions,
                                    std::mutex &sess_m,
                                    size_t n,
                                    std::function<void(const std::string&)> cdr_writer) {
    std::vector<std::string> to_remove;
    {
        std::lock_guard<std::mutex> lk(sess_m);
        to_remove.reserve(n);
        for (auto it = sessions.begin(); it != sessions.end() && to_remove.size() < n; ++it) {
            to_remove.push_back(it->first);
        }
        for (const auto &imsi : to_remove) sessions.erase(imsi);
    }
    for (const auto &imsi : to_remove) cdr_writer(imsi);
    return to_remove.size();
}

void Server::start_offload(size_t rate) {
    if (offloading_) {
        spdlog::warn("Offload already in progress");
        return;
    }
    offloading_ = true;
    spdlog::info("Starting offload at {} sessions/sec", rate);

    std::thread([this, rate]() {
        try {
            while (running_) {
                size_t removed = remove_sessions_batch(this->sessions_, this->sess_m_, rate,
                    [this](const std::string &imsi) {
                        this->append_cdr(imsi, "offloaded");
                        spdlog::info("Offloaded {}", imsi);
                    });
                if (removed == 0) {
                    spdlog::info("Offload complete - no sessions left");
                    break;
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        } catch (const std::exception &e) {
            spdlog::error("Exception in offload thread: {}", e.what());
        } catch (...) {
            spdlog::error("Unknown exception in offload thread");
        }

        this->running_ = false;
        this->offloading_ = false;
    }).detach();
}

void Server::http_loop() {
    http_svr_ = std::make_shared<httplib::Server>();
    auto svr = http_svr_;

    svr->Get("/check_subscriber", [this](const httplib::Request &req, httplib::Response &res){
        if (!req.has_param("imsi")) {
            res.status = 400;
            res.set_content("missing imsi param", "text/plain");
            return;
        }
        std::string imsi = req.get_param_value("imsi");
        bool active;
        {
            std::lock_guard<std::mutex> lk(sess_m_);
            active = sessions_.find(imsi) != sessions_.end();
        }
        res.set_content(active ? "active" : "not active", "text/plain");
    });

    svr->Post("/stop", [this, svr](const httplib::Request &req, httplib::Response &res){
        if (offloading_) {
            res.set_content("already offloading", "text/plain");
            return;
        }
        size_t rate = static_cast<size_t>(std::max(1, cfg_.graceful_shutdown_rate));
        if (req.has_param("rate")) {
            try {
                rate = std::stoul(req.get_param_value("rate"));
                if (rate == 0) rate = 1;
            } catch (...) {}
        }
        spdlog::info("HTTP /stop called, starting offload at rate {}", rate);

        offloading_ = true;
        std::thread([this, rate, svr]() {
            try {
                while (this->running_) {
                    size_t removed = remove_sessions_batch(this->sessions_, this->sess_m_, rate,
                        [this](const std::string &imsi) {
                            this->append_cdr(imsi, "offloaded");
                            spdlog::info("Offloaded {}", imsi);
                        });
                    if (removed == 0) {
                        spdlog::info("Offload complete - no sessions left");
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            } catch (const std::exception &e) {
                spdlog::error("Exception in offload thread: {}", e.what());
            } catch (...) {
                spdlog::error("Unknown exception in offload thread");
            }

            this->running_ = false;

            if (svr) {
                try { svr->stop(); }
                catch (const std::exception &e) { spdlog::warn("Exception stopping http server: {}", e.what()); }
                catch (...) { spdlog::warn("Unknown exception stopping http server"); }
            }
            this->offloading_ = false;
        }).detach();

        res.set_content("offload_started", "text/plain");
    });

    svr->Get("/health", [](const httplib::Request&, httplib::Response &res){
        res.set_content("ok", "text/plain");
    });

    spdlog::info("Starting HTTP server on 0.0.0.0:{}", cfg_.http_port);
    if (!svr->listen("0.0.0.0", cfg_.http_port)) {
        spdlog::error("HTTP server failed to start on port {}", cfg_.http_port);
    } else {
        spdlog::info("HTTP server stopped listening");
    }
}

void Server::udp_loop() {
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        spdlog::critical("socket() failed: {}", strerror(errno));
        running_ = false;
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg_.udp_port);
    if (inet_pton(AF_INET, cfg_.udp_ip.c_str(), &addr.sin_addr) <= 0) {
        spdlog::error("Invalid UDP IP: {}", cfg_.udp_ip);
        close(sock);
        running_ = false;
        return;
    }

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        spdlog::critical("bind() failed: {}", strerror(errno));
        close(sock);
        running_ = false;
        return;
    }

    struct timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    spdlog::info("UDP server listening on {}:{}", cfg_.udp_ip, cfg_.udp_port);

    std::thread cleaner([this]() {
        try {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                std::vector<std::string> expired;
                auto now = std::chrono::steady_clock::now();
                {
                    std::lock_guard<std::mutex> lk(sess_m_);
                    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
                        auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
                        if (diff >= cfg_.session_timeout_sec) expired.push_back(it->first);
                    }
                    for (const auto &imsi : expired) sessions_.erase(imsi);
                }
                for (const auto &imsi : expired) {
                    append_cdr(imsi, "timeout");
                    spdlog::info("Session {} timed out and removed", imsi);
                }
            }
        } catch (const std::exception &e) {
            spdlog::error("Exception in cleaner thread: {}", e.what());
        } catch (...) {
            spdlog::error("Unknown exception in cleaner thread");
        }
    });

    while (running_) {
        uint8_t buf[512];
        sockaddr_in cli{};
        socklen_t cli_len = sizeof(cli);
        ssize_t r = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&cli), &cli_len);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (errno == EINTR) continue;
            spdlog::error("recvfrom error: {}", strerror(errno));
            break;
        }

        std::vector<uint8_t> incoming(buf, buf + r);
        std::string imsi;
        try {
            imsi = decode_imsi_bcd(incoming);
        } catch (...) {
            spdlog::warn("Failed to decode BCD IMSI from {} bytes", r);
            continue;
        }

        spdlog::info("Received IMSI '{}' from {}:{}", imsi,
                     inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

        std::string reply;
        bool was_blacklisted = false;
        {
            std::lock_guard<std::mutex> lk(sess_m_);
            if (is_blacklisted(imsi)) {
                reply = "rejected";
                was_blacklisted = true;
            } else {
                auto it = sessions_.find(imsi);
                if (it == sessions_.end()) {
                    sessions_[imsi] = std::chrono::steady_clock::now();
                    append_cdr(imsi, "created");
                    reply = "created";
                    spdlog::info("Session created for {}", imsi);
                } else {
                    it->second = std::chrono::steady_clock::now();
                    reply = "active";
                    spdlog::debug("Session refreshed for {}", imsi);
                }
            }
        }

        if (was_blacklisted) {
            append_cdr(imsi, "rejected");
            spdlog::info("IMSI {} is blacklisted -> rejected", imsi);
        }

        ssize_t sent = sendto(sock, reply.c_str(), reply.size(), 0, reinterpret_cast<sockaddr*>(&cli), cli_len);
        if (sent < 0) spdlog::warn("sendto failed: {}", strerror(errno));
    }

    spdlog::info("UDP loop exiting, closing socket");
    close(sock);

    if (cleaner.joinable()) cleaner.join();
}
