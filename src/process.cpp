#include "Process.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

using namespace std;
using json = nlohmann::json;

WebSocketServer* Process::ws_server = nullptr;

Process::Process(int id, bool initialToken)
    : id(id), horlogeLogique(0), jetonPresent(initialToken),
      state(IDLE), dansSC(false), enPanne(false) {
    requetes.resize(NUM_PROCESS, 0);
    jeton = initialToken ? new Token(NUM_PROCESS) : nullptr;
}

Process::~Process() {
    stop();
    if (jeton) delete jeton;
}

void Process::start() {
    connectToPeers();
    serverThread = thread(&Process::runServer, this);
    mainThread = thread(&Process::run, this);
}

void Process::stop() {
    enPanne = true;
    if (serverThread.joinable()) serverThread.join();
    if (mainThread.joinable()) mainThread.join();
    close(serverSocket);
    for (auto& [_, sock] : clientSockets) close(sock);
}

void Process::fail() {
    enPanne = true;
    updateState(FAILED);
}

void Process::recover() {
    enPanne = false;
    updateState(IDLE);
}

void Process::run() {
    while (true) {
        if (enPanne) {
            this_thread::sleep_for(chrono::seconds(1));
            continue;
        }

        this_thread::sleep_for(chrono::milliseconds(getRandomDelay(1000, 5000)));
        demanderSC();
        entrerSC();
        quitterSC();
    }
}

void Process::demanderSC() {
    horlogeLogique++;
    requetes[id] = horlogeLogique;
    updateState(REQUESTING);

    Message req{REQUEST, id, horlogeLogique, {}};
    broadcastMessage(req);
}

void Process::entrerSC() {
    unique_lock<mutex> lock(tokenMutex);
    tokenCV.wait(lock, [&]() { return jetonPresent.load(); });
    updateState(IN_CS);
    dansSC = true;
    this_thread::sleep_for(chrono::milliseconds(getRandomDelay(1000, 2000)));
}

void Process::quitterSC() {
    dansSC = false;
    horlogeLogique++;
    jeton->lastRequestClock[id] = horlogeLogique;

    for (int j = (id + 1) % NUM_PROCESS; j != id; j = (j + 1) % NUM_PROCESS) {
        if (requetes[j] > jeton->lastRequestClock[j]) {
            Message tokenMsg{TOKEN, id, horlogeLogique, jeton->lastRequestClock};
            envoyerMessage(j, tokenMsg);
            jetonPresent = false;
            updateState(IDLE);
            return;
        }
    }
    updateState(IDLE);
}

void Process::recevoirMessage(const Message& msg) {
    if (enPanne) return;
    horlogeLogique = max(horlogeLogique.load(), msg.clock) + 1;

    if (msg.type == REQUEST) {
        requetes[msg.sender] = max(requetes[msg.sender], msg.clock);
        if (jetonPresent && !dansSC) {
            if (requetes[msg.sender] > jeton->lastRequestClock[msg.sender]) {
                Message tokenMsg{TOKEN, id, horlogeLogique, jeton->lastRequestClock};
                envoyerMessage(msg.sender, tokenMsg);
                jetonPresent = false;
                updateState(IDLE);
            }
        }
    } else if (msg.type == TOKEN) {
        if (!jeton) jeton = new Token(NUM_PROCESS);
        jeton->lastRequestClock = msg.token;
        jetonPresent = true;
        tokenCV.notify_one();
    }
}

void Process::envoyerMessage(int targetId, const Message& msg) {
    lock_guard<mutex> lock(socketMutex);
    if (clientSockets.find(targetId) == clientSockets.end()) return;

    json j;
    j["type"] = msg.type;
    j["sender"] = msg.sender;
    j["clock"] = msg.clock;
    j["token"] = msg.token;

    string serialized = j.dump();
    send(clientSockets[targetId], serialized.c_str(), serialized.size(), 0);
}

void Process::broadcastMessage(const Message& msg) {
    for (int i = 0; i < NUM_PROCESS; ++i) {
        if (i != id && !enPanne) {
            envoyerMessage(i, msg);
        }
    }
}

int Process::getRandomDelay(int minMs, int maxMs) {
    return minMs + rand() % (maxMs - minMs + 1);
}

void Process::updateState(ProcessState newState) {
    state = newState;
    notifyStateChange();
}

void Process::notifyStateChange() {
    if (ws_server) {
        ws_server->broadcast_process_state(id, 
            state == IDLE ? "idle" : state == REQUESTING ? "requesting" : state == IN_CS ? "in_cs" : "failed", 
            requetes);
    }
}

void Process::setWebSocketServer(WebSocketServer* server) {
    ws_server = server;
}

void Process::runServer() {
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(getProcessPort(id));

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Bind failed\n";
        exit(1);
    }

    listen(serverSocket, NUM_PROCESS);
    cout << "[P" << id << "] Listening on port " << getProcessPort(id) << endl;

    while (!enPanne) {
        sockaddr_in clientAddr;
        socklen_t len = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &len);
        if (clientSocket >= 0) {
            thread(&Process::handleClient, this, clientSocket).detach();
        }
    }
}

void Process::connectToPeers() {
    for (int i = 0; i < NUM_PROCESS; ++i) {
        if (i == id) continue;
        int sock = socket(AF_INET, SOCK_STREAM, 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(getProcessPort(i));
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        while (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            this_thread::sleep_for(chrono::milliseconds(100));
        }

        lock_guard<mutex> lock(socketMutex);
        clientSockets[i] = sock;
    }
}

void Process::handleClient(int clientSocket) {
    char buffer[4096];
    while (!enPanne) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break;

        try {
            json j = json::parse(string(buffer, bytes));
            Message msg;
            msg.type = static_cast<MessageType>(j["type"].get<int>());
            msg.sender = j["sender"].get<int>();
            msg.clock = j["clock"].get<int>();
            msg.token = j["token"].get<vector<int>>();

            recevoirMessage(msg);
        } catch (...) {
            cerr << "[P" << id << "] Failed to parse message" << endl;
        }
    }
    close(clientSocket);
}
