#include "Process.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <algorithm>

using namespace std;

// Static member initialization
WebSocketServer* Process::ws_server = nullptr;

//constructeur
Process::Process(int id, bool initialToken)
  : id(id), horlogelogique(0), jetonpresent(initialToken), state(IDLE), dedans(false)
  , enpanne(false)
{
    jeton = vector<int>(NUM_PROCESS, 0);
    requetes = vector<int>(NUM_PROCESS, 0);
    cout << "Processus " << id
         << " créé" << (initialToken ? " avec jeton initial" : "") << endl;
}

//destructor for proper and gracefully stopping
Process::~Process() {
    stop();
}

void Process::setWebSocketServer(WebSocketServer* server) {
    ws_server = server;
}

//démarrage des threads du processus
void Process::start() {
    serverThread      = thread(&Process::runServer, this);
    mainProcessThread = thread(&Process::run,       this);
}

// Arrêt du processus et nettoyage
void Process::stop() {
    // signaler fin aux boucles
    enpanne = true;
    tokenCV.notify_all();
    shutdown(serverSocket, SHUT_RDWR);

    if (serverThread.joinable())      serverThread.join();
    if (mainProcessThread.joinable()) mainProcessThread.join();

    close(serverSocket);
    for (auto &p : clientSockets) close(p.second);
}

// Simulation d'une panne
void Process::fail() {
    lock_guard<mutex> lock(tokenMutex);
    enpanne = true;
    updateState(FAILED);
    cout << "Processus " << id << " en panne" << endl;
    
    // If this process had the token, we simulate token loss
    // In a real system, you'd need a token regeneration mechanism
    if (jetonpresent) {
        cout << "Processus " << id << " : Perte du jeton due à la panne" << endl;
    }
}

// Récupération après une panne
void Process::recover() {
    lock_guard<mutex> lock(tokenMutex);
    enpanne = false;
    updateState(IDLE);
    cout << "Processus " << id << " récupéré" << endl;
    
    // Reset request for this process
    requetes[id] = 0;
    tokenCV.notify_all();
}

// Méthode principale du processus
void Process::run() {
    this_thread::sleep_for(chrono::seconds(3));
    connectToOtherProcesses();
    this_thread::sleep_for(chrono::seconds(2));

    mt19937 rng(random_device{}());
    uniform_int_distribution<> waitDist(1, 5);
    uniform_int_distribution<> csDist(1, 2);

    while (!enpanne) {
        // Wait phase
        int waitTime = waitDist(rng);
        cout << "Processus " << id << " attend " << waitTime << " secondes" << endl;
        this_thread::sleep_for(chrono::seconds(waitTime));

        if (enpanne) continue;

        // Request CS
        demandeSC();
        
        if (enpanne || !jetonpresent) continue;
        
        // Enter CS
        entrerSC();

        // Stay in CS
        int csTime = csDist(rng);
        cout << "Processus " << id << " est en SC pendant " << csTime << " secondes" << endl;
        this_thread::sleep_for(chrono::seconds(csTime));

        // Exit CS
        quitterSC();
    }
}

// Demande d'accès à la section critique
void Process::demandeSC() {
    unique_lock<mutex> lock(tokenMutex);

    // If we already have the token, no need to request
    if (jetonpresent && !enpanne) {
        cout << "Processus " << id << " a déjà le jeton, pas besoin de le demander" << endl;
        return;
    }

    if (enpanne) return;

    // Increment logical clock and set our request timestamp
    horlogelogique++;
    requetes[id] = horlogelogique;

    updateState(REQUESTING);
    cout << "Processus " << id << " demande la SC avec horloge=" << horlogelogique << endl;

    // Send request to all other processes
    Message msg{REQUEST, id, horlogelogique, {}};
    
    // Unlock before broadcasting to avoid deadlock
    lock.unlock();
    broadcastMessage(msg);
    lock.lock();

    // Wait until we get the token or fail
    tokenCV.wait(lock, [&]{ return jetonpresent.load() || enpanne.load(); });
    
    if (jetonpresent && !enpanne) {
        cout << "Processus " << id << " a obtenu le jeton" << endl;
    }
}

// Entrée en section critique
void Process::entrerSC() {
    lock_guard<mutex> lock(tokenMutex);
    if (!jetonpresent || enpanne) {
        cout << "Processus " << id << " : Impossible d'entrer en SC (pas de jeton ou en panne)" << endl;
        return;
    }
    
    dedans = true;
    updateState(IN_CS);
    cout << "Processus " << id << " entre en SC" << endl;
}

// Sortie de section critique
void Process::quitterSC() {
    lock_guard<mutex> lock(tokenMutex);
    
    if (!dedans) return;
    
    dedans = false;
    jeton[id] = horlogelogique; // Update token with current timestamp
    requetes[id] = 0; // Clear our own request

    updateState(IDLE);
    cout << "Processus " << id << " sort de SC" << endl;

    // Find the next process that should get the token
    // Priority: oldest request timestamp that is newer than token timestamp
    int nextProcess = -1;
    int oldestRequestTime = INT_MAX;
    
    for (int j = 0; j < NUM_PROCESS; j++) {
        if (j != id && !enpanne && requetes[j] > jeton[j] && requetes[j] > 0) {
            if (requetes[j] < oldestRequestTime) {
                nextProcess = j;
                oldestRequestTime = requetes[j];
            }
        }
    }

    if (nextProcess != -1) {
        cout << "Processus " << id << " envoie le jeton au processus " << nextProcess 
             << " (requête timestamp: " << requetes[nextProcess] << ")" << endl;
        jetonpresent = false;
        Message m{TOKEN, id, horlogelogique, jeton};
        envoiMessage(nextProcess, m);
    } else {
        cout << "Processus " << id << " garde le jeton (aucun demandeur valide)" << endl;
    }
}

// Serveur d'écoute des connexions entrantes
void Process::runServer() {
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(BASE_PORT + id);

    if (bind(serverSocket, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        return;
    }
    
    if (listen(serverSocket, NUM_PROCESS) < 0) {
        perror("listen failed");
        return;
    }

    cout << "Processus " << id << " : Serveur démarré sur port " << (BASE_PORT + id) << endl;

    while (!enpanne) {
        sockaddr_in cli{};
        socklen_t len = sizeof(cli);
        int cliSock = accept(serverSocket, (sockaddr*)&cli, &len);
        if (cliSock < 0) {
            if (!enpanne) perror("accept failed");
            continue;
        }

        char buf[16];
        if (recv(cliSock, buf, sizeof(buf), 0) > 0) {
            int peerId = atoi(buf);
            cout << "Processus " << id << " : Connexion acceptée de processus " << peerId << endl;
            {
                lock_guard<mutex> lk(socketMutex);
                clientSockets[peerId] = cliSock;
            }
            thread(&Process::handleClientConnection, this, cliSock).detach();
        }
    }
}

// Établissement des connexions avec les autres processus
void Process::connectToOtherProcesses() {
    cout << "Processus " << id << " : Tentative de connexion aux autres processus..." << endl;
    for (int i = 0; i < NUM_PROCESS; ++i) {
        if (i == id) continue;

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(BASE_PORT + i);
        addr.sin_addr.s_addr = INADDR_ANY;
        std::memset(&(addr.sin_zero), 0, sizeof(addr.sin_zero));

        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        bool connected = false;
        for (int attempt = 0; attempt < 10 && !connected; ++attempt) {
            if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
                connected = true;
                cout << "Processus " << id << " : Connecté au processus " << i << endl;
            } else {
                this_thread::sleep_for(chrono::milliseconds(500));
            }
        }
        if (connected) {
            string idStr = to_string(id);
            send(sock, idStr.c_str(), idStr.size() + 1, 0);
            lock_guard<mutex> lk(socketMutex);
            clientSockets[i] = sock;
        } else {
            cout << "Processus " << id << " : Impossible de se connecter au processus " << i << endl;
            close(sock);
        }
    }
}

// Gestion d'une connexion client
void Process::handleClientConnection(int clientSocket) {
    char buf[1024];
    ssize_t n;
    while ((n = recv(clientSocket, buf, sizeof(buf), 0)) > 0) {
        Message msg;
        memcpy(&msg.type,   buf, sizeof(msg.type));
        memcpy(&msg.sender, buf + sizeof(msg.type), sizeof(int));
        memcpy(&msg.clock,  buf + sizeof(msg.type) + sizeof(int), sizeof(int));
        
        if (msg.type == TOKEN) {
            msg.token.resize(NUM_PROCESS);
            memcpy(msg.token.data(),
                   buf + sizeof(msg.type) + 2 * sizeof(int),
                   NUM_PROCESS * sizeof(int));
        }
        
        recevoirMessage(msg);
    }
    
    if (n <= 0) {
        close(clientSocket);
    }
}

// Traitement d'un message reçu
void Process::recevoirMessage(const Message& msg) {
    lock_guard<mutex> lock(tokenMutex);
    
    if (enpanne) {
        cout << "Processus " << id << " : Message ignoré (processus en panne)" << endl;
        return;
    }
    
    int oldClock = horlogelogique;
    horlogelogique = max(horlogelogique.load(), msg.clock) + 1;
    
    cout << "Processus " << id << " : Reçu " 
         << (msg.type == REQUEST ? "REQUEST" : "TOKEN") 
         << " de P" << msg.sender 
         << " (clk:" << msg.clock << ", local:" << oldClock << "->" << horlogelogique << ")" << endl;

    if (msg.type == REQUEST) {
        // Update the request queue with sender's request
        requetes[msg.sender] = msg.clock;
        cout << "Processus " << id << " : Requête de P" << msg.sender 
             << " enregistrée avec timestamp " << msg.clock << endl;
        
        // If we have the token, not in CS, and this request has priority
        if (jetonpresent && !dedans) {
            // Check if this request should be satisfied immediately
            if (requetes[msg.sender] > jeton[msg.sender]) {
                cout << "Processus " << id << " : Envoi immédiat du jeton à P" << msg.sender << endl;
                jetonpresent = false;
                Message tokenMsg{TOKEN, id, horlogelogique, jeton};
                envoiMessage(msg.sender, tokenMsg);
            }
        }
    } else if (msg.type == TOKEN) {
        // Receive the token
        jeton = msg.token;
        jetonpresent = true;
        cout << "Processus " << id << " : Jeton reçu de P" << msg.sender << endl;
        
        // Notify waiting thread
        tokenCV.notify_all();
    }
    
    // Notify WebSocket clients of state change
    notifyStateChange();
}

// Envoi d'un message
void Process::envoiMessage(int targetId, const Message& msg) {
    lock_guard<mutex> lk(socketMutex);
    auto it = clientSockets.find(targetId);
    if (it == clientSockets.end()) {
        cout << "Processus " << id << " : Pas de connexion vers P" << targetId << endl;
        return;
    }
    
    char buf[1024];
    size_t offset = 0;
    
    memcpy(buf + offset, &msg.type, sizeof(msg.type));
    offset += sizeof(msg.type);
    
    memcpy(buf + offset, &msg.sender, sizeof(int));
    offset += sizeof(int);
    
    memcpy(buf + offset, &msg.clock, sizeof(int));
    offset += sizeof(int);
    
    if (msg.type == TOKEN) {
        memcpy(buf + offset, msg.token.data(), msg.token.size() * sizeof(int));
        offset += msg.token.size() * sizeof(int);
    }
    
    if (send(it->second, buf, offset, 0) < 0) {
        cout << "Processus " << id << " : Erreur envoi vers P" << targetId << endl;
    }
}

// Diffusion d'un message
void Process::broadcastMessage(const Message& msg) {
    vector<int> targets;
    {
        lock_guard<mutex> lk(socketMutex);
        for (const auto& pair : clientSockets) {
            targets.push_back(pair.first);
        }
    }
    
    cout << "Processus " << id << " : Diffusion " 
         << (msg.type == REQUEST ? "REQUEST" : "TOKEN") 
         << " vers " << targets.size() << " processus" << endl;
    
    for (int target : targets) {
        envoiMessage(target, msg);
    }
}

// màj de l'état du processus
void Process::updateState(ProcessState newState) {
    state = newState;
    static const char* stateNames[] = {"IDLE", "REQUESTING", "IN_CS", "FAILED"};
    cout << "Processus " << id << " : État -> " << stateNames[newState] << endl;
    
    // Notify WebSocket clients
    notifyStateChange();
}

// Notify WebSocket clients of state change
void Process::notifyStateChange() {
    if (ws_server) {
        string state_str;
        switch(state.load()) {
            case IDLE: state_str = "IDLE"; break;
            case REQUESTING: state_str = "REQUESTING"; break;
            case IN_CS: state_str = "IN_CS"; break;
            case FAILED: state_str = "FAILED"; break;
        }
        ws_server->broadcast_process_state(id, state_str, requetes);
    }
}

// Génération d'un temps aléatoire
int Process::getRandomTime(int minSec, int maxSec) {
    static mt19937 rng(random_device{}());
    uniform_int_distribution<> dist(minSec, maxSec);
    return dist(rng);
}
