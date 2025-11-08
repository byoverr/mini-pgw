#include "server.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <spdlog/spdlog.h>
#include <execinfo.h>
#include <unistd.h>
#include <cstdlib>

static void print_backtrace_and_exit() {
    void* bt[50];
    int sz = backtrace(bt, 50);
    backtrace_symbols_fd(bt, sz, STDERR_FILENO);
    _Exit(128);
}

static void terminate_handler() {
    std::cerr << "std::terminate() called. Backtrace:\n";
    print_backtrace_and_exit();
}

static Server* g_server = nullptr;

static void sigint_handler(int) {
    if (g_server) {
        spdlog::info("SIGINT received, requesting server stop()");
        g_server->stop();
    }
}

Config load_config_from_file(const std::string &path) {
    Config cfg;
    std::ifstream f(path);
    if (!f) {
        spdlog::warn("Config file {} not found; using defaults", path);
        return cfg;
    }
    try {
        nlohmann::json j; f >> j;
        if (j.contains("udp_ip")) cfg.udp_ip = j["udp_ip"].get<std::string>();
        if (j.contains("udp_port")) cfg.udp_port = j["udp_port"].get<int>();
        if (j.contains("session_timeout_sec")) cfg.session_timeout_sec = j["session_timeout_sec"].get<int>();
        if (j.contains("cdr_file")) cfg.cdr_file = j["cdr_file"].get<std::string>();
        if (j.contains("http_port")) cfg.http_port = j["http_port"].get<int>();
        if (j.contains("graceful_shutdown_rate")) cfg.graceful_shutdown_rate = j["graceful_shutdown_rate"].get<int>();
        if (j.contains("log_file")) cfg.log_file = j["log_file"].get<std::string>();
        if (j.contains("log_level")) cfg.log_level = j["log_level"].get<std::string>();
        if (j.contains("blacklist")) {
            for (auto &v : j["blacklist"]) cfg.blacklist.push_back(v.get<std::string>());
        }
    } catch (std::exception &e) {
        std::cerr << "Failed to parse config: " << e.what() << "\n";
    }
    return cfg;
}

int main(int argc, char** argv) {
    std::set_terminate(terminate_handler);

    std::string cfg_path = "configs/pgw_server_conf.json";
    if (argc > 1) cfg_path = argv[1];

    spdlog::set_level(spdlog::level::info);

    Config cfg = load_config_from_file(cfg_path);
    Server s(cfg);

    g_server = &s;
    signal(SIGINT, sigint_handler);

    try {
        s.start();
    } catch (const std::exception &e) {
        spdlog::critical("Unhandled exception in main: {}", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        spdlog::critical("Unhandled unknown exception in main");
        return EXIT_FAILURE;
    }

    g_server = nullptr;
    return 0;
}
