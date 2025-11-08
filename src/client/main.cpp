#include "imsi_to_bcd.h"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cctype>
#include <chrono>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

using json = nlohmann::json;

struct ClientConfig {
    std::string server_ip = "127.0.0.1";
    int server_port = 9000;
    std::string log_file = "client.log";
    std::string log_level = "info";
    int tx_timeout_ms = 2000;
};

static ClientConfig load_config(const std::string &path) {
    ClientConfig cfg;
    std::ifstream f(path);
    if (!f) {
        spdlog::warn("Config '{}' not found, using defaults", path);
        return cfg;
    }
    try {
        json j; f >> j;
        if (j.contains("server_ip")) cfg.server_ip = j["server_ip"].get<std::string>();
        if (j.contains("server_port")) cfg.server_port = j["server_port"].get<int>();
        if (j.contains("log_file")) cfg.log_file = j["log_file"].get<std::string>();
        if (j.contains("log_level")) cfg.log_level = j["log_level"].get<std::string>();
        if (j.contains("tx_timeout_ms")) cfg.tx_timeout_ms = j["tx_timeout_ms"].get<int>();
    } catch (const std::exception &e) {
        spdlog::warn("Failed to parse config '{}': {}", path, e.what());
    }
    return cfg;
}

static void init_logger(const std::string &log_file, const std::string &level) {
    try {
        auto logger = spdlog::basic_logger_mt("client_logger", log_file);
        spdlog::set_default_logger(logger);
    } catch (...) {
        spdlog::warn("Unable to open log file '{}', using console logger", log_file);
    }

    if (level == "debug") spdlog::set_level(spdlog::level::debug);
    else if (level == "info") spdlog::set_level(spdlog::level::info);
    else if (level == "warn") spdlog::set_level(spdlog::level::warn);
    else if (level == "err" || level == "error") spdlog::set_level(spdlog::level::err);
    else spdlog::set_level(spdlog::level::info);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: pgw_client IMSI [config.json]\n";
        return 2;
    }

    std::string imsi = argv[1];
    std::string cfg_path = "configs/pgw_client_conf.json";
    if (argc > 2) cfg_path = argv[2];

    spdlog::set_level(spdlog::level::info);
    spdlog::info("Starting pgw_client");

    ClientConfig cfg = load_config(cfg_path);
    init_logger(cfg.log_file, cfg.log_level);

    spdlog::info("Using server {}:{}", cfg.server_ip, cfg.server_port);

    std::vector<uint8_t> bcd;
    try {
        bcd = encode_imsi_bcd(imsi);
    } catch (const std::exception &e) {
        spdlog::error("Invalid IMSI '{}': {}", imsi, e.what());
        std::cerr << "Invalid IMSI: " << e.what() << "\n";
        return 3;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        spdlog::error("socket() failed: {}", strerror(errno));
        std::cerr << "socket() failed: " << strerror(errno) << "\n";
        return 4;
    }

    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(static_cast<uint16_t>(cfg.server_port));
    if (inet_pton(AF_INET, cfg.server_ip.c_str(), &srv.sin_addr) <= 0) {
        spdlog::error("Invalid server IP: {}", cfg.server_ip);
        std::cerr << "Invalid server IP: " << cfg.server_ip << "\n";
        close(sock);
        return 5;
    }

    ssize_t sent = sendto(sock, reinterpret_cast<const char*>(bcd.data()), bcd.size(), 0,
                          reinterpret_cast<sockaddr*>(&srv), sizeof(srv));
    if (sent < 0) {
        spdlog::error("sendto failed: {}", strerror(errno));
        std::cerr << "sendto failed: " << strerror(errno) << "\n";
        close(sock);
        return 6;
    }
    spdlog::info("Sent IMSI '{}' as {} bytes", imsi, sent);

    struct timeval tv{};
    tv.tv_sec = cfg.tx_timeout_ms / 1000;
    tv.tv_usec = (cfg.tx_timeout_ms % 1000) * 1000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv)) < 0) {
        spdlog::warn("setsockopt SO_RCVTIMEO failed: {}", strerror(errno));
    }

    char buf[256];
    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);
    ssize_t r = recvfrom(sock, buf, sizeof(buf)-1, 0, reinterpret_cast<sockaddr*>(&from), &fromlen);
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            spdlog::warn("Timed out waiting for reply ({} ms)", cfg.tx_timeout_ms);
            std::cerr << "timeout\n";
            close(sock);
            return 7;
        } else {
            spdlog::error("recvfrom failed: {}", strerror(errno));
            std::cerr << "recvfrom failed: " << strerror(errno) << "\n";
            close(sock);
            return 8;
        }
    }
    buf[r] = '\0';
    std::string reply(buf);
    spdlog::info("Received reply '{}' ({} bytes) from {}:{}", reply, r,
                 inet_ntoa(from.sin_addr), ntohs(from.sin_port));

    std::cout << reply << std::endl;

    close(sock);
    spdlog::info("Client finished");
    return 0;
}
