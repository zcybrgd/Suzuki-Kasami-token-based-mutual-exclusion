#pragma once
#include <string>

constexpr int BASE_PORT = 5000;

inline int get_port(int process_id) {
    return BASE_PORT + process_id;
}

inline std::string get_ip() {
    return "127.0.0.1"; // assuming localhost for all peers
}
