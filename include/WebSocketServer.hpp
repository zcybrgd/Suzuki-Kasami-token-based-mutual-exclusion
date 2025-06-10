#pragma once
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <nlohmann/json.hpp>
#include <set>
#include <mutex>
#include <thread>
#include "Config.hpp"

using json = nlohmann::json;
typedef websocketpp::server<websocketpp::config::asio> server;
typedef server::message_ptr message_ptr;

class WebSocketServer {
private:
    server ws_server;
    std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> connections;
    std::mutex connections_mutex;
    std::thread server_thread;
    bool running;

    void on_open(websocketpp::connection_hdl hdl);
    void on_close(websocketpp::connection_hdl hdl);
    void on_message(websocketpp::connection_hdl hdl, message_ptr msg);

public:
    WebSocketServer();
    ~WebSocketServer();
    void start();
    void stop();
    void broadcast_process_state(int process_id, const std::string& state, const std::vector<int>& queue);
    void broadcast_system_state(const json& system_state);
private:
    void run_server();
};
