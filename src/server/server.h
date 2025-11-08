#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <atomic>
#include <thread>
#include <fstream>
#include <vector>
#include <memory>
#include <functional>

#include <httplib.h>

struct Config {
    std::string udp_ip = "0.0.0.0";
    int udp_port = 9000;
    int session_timeout_sec = 30;
    std::string cdr_file = "cdr.log";
    int http_port = 8080;
    int graceful_shutdown_rate = 10; // sessions per second
    std::string log_file = "server.log";
    std::string log_level = "info";
    std::vector<std::string> blacklist;
};

class Server {
public:
    explicit Server(Config cfg);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // start server: launches HTTP in background and runs UDP loop in caller thread
    void start();

    // request stop (graceful).
    void stop();

    // query
    bool is_active(const std::string &imsi);

    // safe stop http from outside
    void stop_http_server();

private:
    // core routines
    void udp_loop();
    void http_loop();

    // offload
    void start_offload(size_t rate);

    // helpers
    void append_cdr(const std::string &imsi, const std::string &action);
    bool is_blacklisted(const std::string &imsi);
    std::string now_ts();


private:
    Config cfg_;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> sessions_;
    std::mutex sess_m_;

    std::ofstream cdr_out_;
    std::mutex cdr_m_;

    std::atomic<bool> running_{false};
    std::atomic<bool> offloading_{false};

    std::thread http_thread_;
    std::shared_ptr<httplib::Server> http_svr_;
};
