#include "WebSocketServer.hpp"
#include <iostream>

WebSocketServer::WebSocketServer() : running(false) {
    ws_server.set_access_channels(websocketpp::log::alevel::all);
    ws_server.clear_access_channels(websocketpp::log::alevel::frame_payload);
    ws_server.init_asio();
    ws_server.set_open_handler([this](websocketpp::connection_hdl hdl) {
        this->on_open(hdl);
    });
    ws_server.set_close_handler([this](websocketpp::connection_hdl hdl) {
        this->on_close(hdl);
    });
    ws_server.set_message_handler([this](websocketpp::connection_hdl hdl, message_ptr msg) {
        this->on_message(hdl, msg);
    });
    ws_server.set_reuse_addr(true);
}

WebSocketServer::~WebSocketServer() {
    stop();
}

void WebSocketServer::start() {
    if (running) return;
    running = true;
    server_thread = std::thread(&WebSocketServer::run_server, this);
    std::cout << "WebSocket server started on port " << WEBSOCKET_PORT << std::endl;
}

void WebSocketServer::stop() {
    if (!running) return;
    running = false;
    ws_server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
    std::cout << "WebSocket server stopped" << std::endl;
}

void WebSocketServer::run_server() {
    try {
        //Listen on port
        ws_server.listen(WEBSOCKET_PORT);
        ws_server.start_accept();
        // start the server
        ws_server.run();
    } catch (websocketpp::exception const & e) {
        std::cout << "WebSocket server error: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "WebSocket server unknown error" << std::endl;
    }
}

void WebSocketServer::on_open(websocketpp::connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(connections_mutex);
    connections.insert(hdl);
    std::cout << "New WebSocket connection established" << std::endl;
}

void WebSocketServer::on_close(websocketpp::connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(connections_mutex);
    connections.erase(hdl);
    std::cout << "WebSocket connection closed" << std::endl;
}

void WebSocketServer::on_message(websocketpp::connection_hdl hdl, message_ptr msg) {
    (void)hdl;
    std::cout << "Received WebSocket message: " << msg->get_payload() << std::endl;
}


void WebSocketServer::broadcast_process_state(int process_id, const std::string& state, const std::vector<int>& queue) {
    json message;
    message["type"] = "process_update";
    message["process_id"] = process_id;
    message["state"] = state;
    message["queue"] = queue;
    message["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::lock_guard<std::mutex> lock(connections_mutex);
    for (auto& conn : connections) {
        try {
            ws_server.send(conn, message.dump(), websocketpp::frame::opcode::text);
        } catch (const std::exception& e) {
            std::cout << "Error sending message: " << e.what() << std::endl;
        }
    }
}

void WebSocketServer::broadcast_system_state(const json& system_state) {
    json message;
    message["type"] = "system_update";
    message["data"] = system_state;
    message["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::lock_guard<std::mutex> lock(connections_mutex);
    for (auto& conn : connections) {
        try {
            ws_server.send(conn, message.dump(), websocketpp::frame::opcode::text);
        } catch (const std::exception& e) {
            std::cout << "Error sending system state: " << e.what() << std::endl;
        }
    }
}
