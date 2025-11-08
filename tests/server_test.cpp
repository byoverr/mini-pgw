#include <gtest/gtest.h>
#include "server.h"
#include "imsi_to_bcd.h"
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <httplib.h>

namespace fs = std::filesystem;

// fixture

class ServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = fs::temp_directory_path() / ("pgw_test_" + std::to_string(std::time(nullptr)));
        fs::create_directories(test_dir_);
        
        // test config
        cfg_.udp_ip = "127.0.0.1";
        cfg_.udp_port = find_free_port();
        cfg_.session_timeout_sec = 2;
        cfg_.cdr_file = (test_dir_ / "cdr.log").string();
        cfg_.http_port = find_free_port();
        cfg_.graceful_shutdown_rate = 10;
        cfg_.log_file = (test_dir_ / "server.log").string();
        cfg_.log_level = "error";
        cfg_.blacklist = {"001010123456789", "001010000000001"};
    }

    void TearDown() override {
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }

    Config cfg_;
    fs::path test_dir_;
    
    int find_free_port() {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) return 0;
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = 0;
        
        if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(sock);
            return 0;
        }
        
        socklen_t len = sizeof(addr);
        getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len);
        int port = ntohs(addr.sin_port);
        close(sock);
        return port;
    }
};


TEST_F(ServerTest, ServerConstruction) {
    Server server(cfg_);
    EXPECT_FALSE(server.is_active("123456789012345"));
}

TEST_F(ServerTest, ServerDestruction) {
    {
        Server server(cfg_);
    }
}

// blacklist

TEST_F(ServerTest, BlacklistCheck) {
    Server server(cfg_);
    
    EXPECT_TRUE(cfg_.blacklist.size() > 0);
    
}

// session

TEST_F(ServerTest, SessionCreation) {
    Server server(cfg_);
    
    std::thread server_thread([&server]() {
        server.start();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sock, 0);
    
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(cfg_.udp_port);
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);
    
    std::string imsi = "123456789012345";
    auto bcd = encode_imsi_bcd(imsi);
    
    sendto(sock, bcd.data(), bcd.size(), 0,
           reinterpret_cast<sockaddr*>(&srv), sizeof(srv));
    
    char buf[256];
    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);
    recvfrom(sock, buf, sizeof(buf), 0,
             reinterpret_cast<sockaddr*>(&from), &fromlen);
    
    close(sock);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(server.is_active(imsi));
    
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

TEST_F(ServerTest, SessionRefresh) {
    Server server(cfg_);
    
    std::thread server_thread([&server]() {
        server.start();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sock, 0);
    
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(cfg_.udp_port);
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);
    
    std::string imsi = "123456789012345";
    auto bcd = encode_imsi_bcd(imsi);
    
    sendto(sock, bcd.data(), bcd.size(), 0,
           reinterpret_cast<sockaddr*>(&srv), sizeof(srv));
    
    char buf[256];
    recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    sendto(sock, bcd.data(), bcd.size(), 0,
           reinterpret_cast<sockaddr*>(&srv), sizeof(srv));
    
    recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(server.is_active(imsi));
    
    close(sock);
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

TEST_F(ServerTest, BlacklistedIMSI) {
    Server server(cfg_);
    
    std::thread server_thread([&server]() {
        server.start();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sock, 0);
    
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(cfg_.udp_port);
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);
    
    std::string imsi = cfg_.blacklist[0];
    auto bcd = encode_imsi_bcd(imsi);
    
    sendto(sock, bcd.data(), bcd.size(), 0,
           reinterpret_cast<sockaddr*>(&srv), sizeof(srv));
    
    char buf[256] = {0};
    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);
    ssize_t r = recvfrom(sock, buf, sizeof(buf)-1, 0, reinterpret_cast<sockaddr*>(&from), &fromlen);
    ASSERT_GT(r, 0);
    buf[r] = '\0';
    std::string reply(buf);
    
    close(sock);
    
    EXPECT_EQ(reply, "rejected");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(server.is_active(imsi));
    
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

// cdr

TEST_F(ServerTest, CDRFileCreation) {
    Server server(cfg_);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(fs::exists(cfg_.cdr_file));
}

TEST_F(ServerTest, CDRWriteOnSessionCreate) {
    Server server(cfg_);
    
    std::thread server_thread([&server]() {
        server.start();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sock, 0);
    
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(cfg_.udp_port);
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);
    
    std::string imsi = "123456789012345";
    auto bcd = encode_imsi_bcd(imsi);
    
    sendto(sock, bcd.data(), bcd.size(), 0,
           reinterpret_cast<sockaddr*>(&srv), sizeof(srv));
    
    char buf[256];
    recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
    
    close(sock);
    
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::ifstream cdr(cfg_.cdr_file);
    ASSERT_TRUE(cdr.is_open());
    
    std::string line;
    bool found = false;
    while (std::getline(cdr, line)) {
        if (line.find(imsi) != std::string::npos &&
            line.find("created") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// http tests

TEST_F(ServerTest, HTTPHealthEndpoint) {
    Server server(cfg_);
    
    std::thread server_thread([&server]() {
        server.start();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    httplib::Client cli("127.0.0.1", cfg_.http_port);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/health");
    
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->body, "ok");
    
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

TEST_F(ServerTest, HTTPCheckSubscriberActive) {
    Server server(cfg_);
    
    std::thread server_thread([&server]() {
        server.start();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Create session via UDP
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(cfg_.udp_port);
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);
    
    std::string imsi = "123456789012345";
    auto bcd = encode_imsi_bcd(imsi);
    sendto(sock, bcd.data(), bcd.size(), 0,
           reinterpret_cast<sockaddr*>(&srv), sizeof(srv));
    char buf[256];
    recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
    close(sock);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Check via HTTP
    httplib::Client cli("127.0.0.1", cfg_.http_port);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/check_subscriber?imsi=" + imsi);
    
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->body, "active");
    
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

TEST_F(ServerTest, HTTPCheckSubscriberNotActive) {
    Server server(cfg_);
    
    std::thread server_thread([&server]() {
        server.start();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    httplib::Client cli("127.0.0.1", cfg_.http_port);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/check_subscriber?imsi=999999999999999");
    
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->body, "not active");
    
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

TEST_F(ServerTest, HTTPCheckSubscriberMissingParam) {
    Server server(cfg_);
    
    std::thread server_thread([&server]() {
        server.start();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    httplib::Client cli("127.0.0.1", cfg_.http_port);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/check_subscriber");
    
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
    
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}



TEST_F(ServerTest, SessionTimeout) {
    cfg_.session_timeout_sec = 1;
    Server server(cfg_);
    
    std::thread server_thread([&server]() {
        server.start();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create session
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(cfg_.udp_port);
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);
    
    std::string imsi = "123456789012345";
    auto bcd = encode_imsi_bcd(imsi);
    sendto(sock, bcd.data(), bcd.size(), 0,
           reinterpret_cast<sockaddr*>(&srv), sizeof(srv));
    char buf[256];
    recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
    close(sock);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(server.is_active(imsi));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    
    // Session should be expired
    EXPECT_FALSE(server.is_active(imsi));
    
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

// shutdown test

TEST_F(ServerTest, GracefulShutdown) {
    Server server(cfg_);
    
    std::thread server_thread([&server]() {
        server.start();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(cfg_.udp_port);
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);
    
    for (int i = 0; i < 3; ++i) {
        std::string imsi = "12345678901234" + std::to_string(i);
        auto bcd = encode_imsi_bcd(imsi);
        sendto(sock, bcd.data(), bcd.size(), 0,
               reinterpret_cast<sockaddr*>(&srv), sizeof(srv));
        char buf[256];
        recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
    }
    close(sock);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    server.stop();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    if (server_thread.joinable()) {
        server_thread.join();
    }
    
}

