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
#include "WebSocketServer.hpp"

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
    serverThread = thread(&Process::runServer, this);
    mainProcessThread = thread(&Process::run, this);
    tokenCheckThread = thread(&Process::checkTokenLoss, this); // Nouveau thread
}

// Arrêt du processus et nettoyage
void Process::stop() {
    enpanne = true;
    tokenCV.notify_all();
    shutdown(serverSocket, SHUT_RDWR);

    if (serverThread.joinable()) serverThread.join();
    if (mainProcessThread.joinable()) mainProcessThread.join();
    if (tokenCheckThread.joinable()) tokenCheckThread.join(); // Joindre le nouveau thread

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

    // If we had a pending request, re-request SC
    if (requetes[id] > 0) {
        cout << "Processus " << id << " : Re-demande SC après récupération" << endl;
        demandeSC();
    }
    notifyStateChange();
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
    horlogelogique++; // Ensure clock advances
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

void Process::quitterSC() {
    lock_guard<mutex> lock(tokenMutex);
    
    if (!dedans) return;
    
    dedans = false;
    jeton[id] = horlogelogique; // Update token with current logical clock
    requetes[id] = 0; // Clear our own request

    updateState(IDLE);
    cout << "Processus " << id << " sort de SC" << endl;

    // Debug: Print the requetes and jeton arrays
    cout << "Processus " << id << " : requetes = [";
    for (int j = 0; j < NUM_PROCESS; ++j) {
        cout << requetes[j] << (j < NUM_PROCESS - 1 ? ", " : "");
    }
    cout << "]" << endl;
    cout << "Processus " << id << " : jeton = [";
    for (int j = 0; j < NUM_PROCESS; ++j) {
        cout << jeton[j] << (j < NUM_PROCESS - 1 ? ", " : "");
    }
    cout << "]" << endl;

    // Find the next process with a pending request
    int nextProcess = -1;
    for (int j = 0; j < NUM_PROCESS; ++j) {
        if (j != id && requetes[j] > jeton[j] && requetes[j] > 0) {
            nextProcess = j;
            break; // Take the first process with a pending request
        }
    }

    if (nextProcess != -1) {
        cout << "Processus " << id << " envoie le jeton à P" << nextProcess 
             << " (req=" << requetes[nextProcess] << ", token=" << jeton[nextProcess] << ")" << endl;
        jetonpresent = false;
        Message m{TOKEN, id, horlogelogique, jeton};
        envoiMessage(nextProcess, m);
    } else {
        cout << "Processus " << id << " garde le jeton (aucune requête prioritaire)" << endl;
    }
}

void Process::entrerSC() {
    unique_lock<mutex> lock(tokenMutex);
    tokenCV.wait(lock, [&]{
        return jetonpresent.load() && !enpanne.load();
    });

    // Recheck requests in a loop to handle incoming messages
    while (true) {
        int nextProcess = -1;
        for (int j = 0; j < NUM_PROCESS; ++j) {
            if (j != id && requetes[j] > jeton[j] && requetes[j] > 0) {
                if (requetes[id] == 0 || requetes[j] < requetes[id] || (requetes[j] == requetes[id] && j < id)) {
                    nextProcess = j;
                    break;
                }
            }
        }

        if (nextProcess != -1) {
            cout << "Processus " << id << " détecte une requête prioritaire de P" << nextProcess << endl;
            jetonpresent = false;
            Message m{TOKEN, id, horlogelogique, jeton};
            envoiMessage(nextProcess, m);
            tokenCV.wait(lock, [&]{ return jetonpresent.load() && !enpanne.load(); });
        } else {
            dedans = true;
            updateState(IN_CS);
            cout << "Processus " << id << " entre en SC" << endl;
            break;
        }
    }
}

// Serveur d'écoute des connexions entrantes
void Process::runServer() {
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        cout << "Processus " << id << " : Échec création socket serveur (errno=" << errno << ")" << endl;
        return;
    }
    cout << "Processus " << id << " : Socket serveur créé (fd=" << serverSocket << ")" << endl;

    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        cout << "Processus " << id << " : Échec setsockopt (errno=" << errno << ")" << endl;
        close(serverSocket);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(BASE_PORT + id);

    if (bind(serverSocket, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cout << "Processus " << id << " : Échec bind sur port " << (BASE_PORT + id) << " (errno=" << errno << ")" << endl;
        close(serverSocket);
        return;
    }
    cout << "Processus " << id << " : Bind réussi sur port " << (BASE_PORT + id) << endl;
    
    if (listen(serverSocket, NUM_PROCESS) < 0) {
        cout << "Processus " << id << " : Échec listen (errno=" << errno << ")" << endl;
        close(serverSocket);
        return;
    }
    cout << "Processus " << id << " : Serveur démarré sur port " << (BASE_PORT + id) << endl;

    while (!enpanne) {
        sockaddr_in cli{};
        socklen_t len = sizeof(cli);
        cout << "Processus " << id << " : Attente connexion sur socket " << serverSocket << endl;
        int cliSock = accept(serverSocket, (sockaddr*)&cli, &len);
        if (cliSock < 0) {
            if (!enpanne) cout << "Processus " << id << " : Échec accept (errno=" << errno << ")" << endl;
            continue;
        }
        cout << "Processus " << id << " : Connexion acceptée, socket client=" << cliSock << endl;

        char buf[16];
        ssize_t n = recv(cliSock, buf, sizeof(buf), 0);
        if (n > 0) {
            buf[n] = '\0'; // Null-terminate
            int peerId = atoi(buf);
            cout << "Processus " << id << " : Connexion acceptée de processus " << peerId << " sur socket " << cliSock << endl;
            {
                lock_guard<mutex> lk(socketMutex);
                clientSockets[peerId] = cliSock;
            }
            thread(&Process::handleClientConnection, this, cliSock).detach();
        } else {
            cout << "Processus " << id << " : Échec réception ID client (n=" << n << ", errno=" << errno << ")" << endl;
            close(cliSock);
        }
    }
    close(serverSocket);
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
    cout << "Processus " << id << " : Début gestion connexion sur socket " << clientSocket << endl;
    while ((n = recv(clientSocket, buf, sizeof(buf), 0)) > 0) {
        if (enpanne) {
            cout << "Processus " << id << " : Ignorer message, processus en panne" << endl;
            break;
        }
        cout << "Processus " << id << " : Reçu " << n << " octets sur socket " << clientSocket << endl;
        cout << "Processus " << id << " : Données brutes=[";
        for (ssize_t i = 0; i < n; ++i) cout << hex << (int)(unsigned char)buf[i] << " ";
        cout << dec << "]" << endl;

        if (n < static_cast<ssize_t>(sizeof(MessageType) + 2 * sizeof(int))) {
            cout << "Processus " << id << " : Message incomplet (taille=" << n << ")" << endl;
            continue;
        }

        Message msg;
        memcpy(&msg.type, buf, sizeof(msg.type));
        memcpy(&msg.sender, buf + sizeof(msg.type), sizeof(int));
        memcpy(&msg.clock, buf + sizeof(msg.type) + sizeof(int), sizeof(int));

        cout << "Processus " << id << " : Message décodé: type=" << (msg.type == REQUEST ? "REQUEST" : "TOKEN")
             << ", sender=" << msg.sender << ", clock=" << msg.clock << endl;

        if (msg.type == TOKEN) {
            if (n < static_cast<ssize_t>(sizeof(MessageType) + 2 * sizeof(int) + NUM_PROCESS * sizeof(int))) {
                cout << "Processus " << id << " : Message TOKEN incomplet (taille=" << n << ")" << endl;
                continue;
            }
            msg.token.resize(NUM_PROCESS);
            memcpy(msg.token.data(), buf + sizeof(msg.type) + 2 * sizeof(int), NUM_PROCESS * sizeof(int));
            cout << "Processus " << id << " : Jeton décodé, contenu=[";
            for (int v : msg.token) cout << v << ",";
            cout << "]" << endl;
        }

        recevoirMessage(msg);
    }

    if (n < 0) {
        cout << "Processus " << id << " : Erreur réception sur socket " << clientSocket
             << " (errno=" << errno << ")" << endl;
    } else {
        cout << "Processus " << id << " : Connexion fermée avec socket " << clientSocket
             << " (recv=" << n << ")" << endl;
    }
    close(clientSocket);
}

// Traitement d'un message reçu
void Process::recevoirMessage(const Message& msg) {
    lock_guard<mutex> lock(tokenMutex);

    if (enpanne) {
        cout << "Processus " << id << " : Message ignoré (processus en panne)" << endl;
        return;
    }

    int oldClock = horlogelogique.load(); // Use .load() to get current value
    horlogelogique = std::max(horlogelogique.load(), msg.clock) + 1; // Fix: Use .load()

    cout << "Processus " << id << " : Reçu "
         << (msg.type == REQUEST ? "REQUEST" : "TOKEN")
         << " de P" << msg.sender
         << " (clk:" << msg.clock << ", local:" << oldClock << "->" << horlogelogique.load() << ")" << endl;

    if (msg.type == REQUEST) {
        requetes[msg.sender] = msg.clock;
        cout << "Processus " << id << " : Requête de P" << msg.sender
             << " enregistrée avec timestamp " << msg.clock << endl;

        if (jetonpresent && !dedans && state == REQUESTING) {
            int nextProcess = -1;
            for (int j = 0; j < NUM_PROCESS; ++j) {
                if (j != id && requetes[j] > jeton[j] && requetes[j] > 0) {
                    if (nextProcess == -1 ||
                        requetes[j] < requetes[nextProcess] ||
                        (requetes[j] == requetes[nextProcess] && j < nextProcess)) {
                        nextProcess = j;
                    }
                }
            }
            if (nextProcess != -1 && (requetes[id] == 0 || requetes[nextProcess] < requetes[id] ||
                                     (requetes[nextProcess] == requetes[id] && nextProcess < id))) {
                cout << "Processus " << id << " : Envoi du jeton à P" << nextProcess << endl;
                jetonpresent = false;
                Message tokenMsg{TOKEN, id, horlogelogique.load(), jeton}; // Use .load() for consistency
                envoiMessage(nextProcess, tokenMsg);
            }
        }
    } else if (msg.type == TOKEN) {
        jeton = msg.token;
        jetonpresent = true;
        jeton[id] = horlogelogique.load(); // Use .load() for consistency
        cout << "Processus " << id << " : Jeton reçu de P" << msg.sender << endl;

        // If we were requesting, try to enter CS
        if (state == REQUESTING && !dedans) {
            cout << "Processus " << id << " : Tentative d'entrée en SC après réception jeton" << endl;
            tokenCV.notify_all();
        }
    }

    notifyStateChange();
}
 
  
// Envoi d'un message
void Process::envoiMessage(int targetId, const Message& msg) {
    lock_guard<mutex> lk(socketMutex);
    if (enpanne) return;

    auto it = clientSockets.find(targetId);
    if (it == clientSockets.end() || it->second <= 0) {
        cout << "Processus " << id << " : Connexion vers P" << targetId << " invalide, tentative de reconnexion" << endl;
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(BASE_PORT + targetId);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
            string idStr = to_string(id);
            send(sock, idStr.c_str(), idStr.size() + 1, 0);
            clientSockets[targetId] = sock;
            it = clientSockets.find(targetId);
        } else {
            cout << "Processus " << id << " : Échec de reconnexion à P" << targetId << endl;
            close(sock);
            return;
        }
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

    ssize_t sent = send(it->second, buf, offset, 0);
    if (sent < 0 || static_cast<size_t>(sent) != offset) {
        cout << "Processus " << id << " : Erreur envoi " << (msg.type == REQUEST ? "REQUEST" : "TOKEN")
             << " vers P" << targetId << " (envoyé " << sent << "/" << offset << " octets)" << endl;
        close(it->second);
        clientSockets.erase(targetId);
    } else {
        cout << "Processus " << id << " : Message " << (msg.type == REQUEST ? "REQUEST" : "TOKEN")
             << " envoyé à P" << targetId << " (" << offset << " octets)" << endl;
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
        switch (state) { // Removed .load()
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

void Process::checkTokenLoss() {
    while (!enpanne) {
        this_thread::sleep_for(chrono::seconds(5)); // Reduced to 5s for faster recovery
        if (id == 0 && !jetonpresent && state == REQUESTING) {
            lock_guard<mutex> lock(tokenMutex);
            bool otherRequesting = false;
            for (int j = 0; j < NUM_PROCESS; ++j) {
                if (j != id && requetes[j] > 0) {
                    otherRequesting = true;
                    break;
                }
            }
            if (!otherRequesting) {
                cout << "Processus " << id << " : Régénération du jeton (aucune requête)" << endl;
                jetonpresent = true;
                jeton = vector<int>(NUM_PROCESS, 0);
                requetes = vector<int>(NUM_PROCESS, 0);
                tokenCV.notify_all();
            } else {
                // Check if token is stuck with a failed process
                bool tokenLost = true;
                for (int j = 0; j < NUM_PROCESS; ++j) {
                    if (j != id && clientSockets.find(j) != clientSockets.end()) {
                        // Assume a simple ping to check if process is alive
                        // In practice, use a heartbeat mechanism
                        char ping = 'P';
                        if (send(clientSockets[j], &ping, 1, 0) > 0) {
                            tokenLost = false;
                            break;
                        }
                    }
                }
                if (tokenLost) {
                    cout << "Processus " << id << " : Régénération du jeton (jeton perdu)" << endl;
                    jetonpresent = true;
                    jeton = vector<int>(NUM_PROCESS, 0);
                    requetes = vector<int>(NUM_PROCESS, 0);
                    tokenCV.notify_all();
                } else {
                    cout << "Processus " << id << " : Régénération différée, autres requêtes en cours" << endl;
                }
            }
        }
    }
}
