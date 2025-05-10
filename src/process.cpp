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

//constructeur
Process::Process(int id, bool initialToken)
  : id(id)
  , horlogelogique(0)
  , jetonpresent(initialToken)
  , state(IDLE)
  , dedans(false)
  , enpanne(false)
{
    jeton    = vector<int>(NUM_PROCESS, 0);
    requetes = vector<int>(NUM_PROCESS, 0);
    cout << "Processus " << id
         << " créé" << (initialToken ? " avec jeton initial" : "") << endl;
}

//destructor for proper and gracefully stopping
Process::~Process() {
    stop();
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
    enpanne = true;
    updateState(FAILED);
    cout << "Processus " << id << " en panne" << endl;
}

// Récupération après une panne
void Process::recover() {
    enpanne = false;
    updateState(IDLE);
    cout << "Processus " << id << " récupéré" << endl;
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
        int waitTime = waitDist(rng);
        cout << "Processus " << id << " attend " << waitTime << " secondes" << endl;
        this_thread::sleep_for(chrono::seconds(waitTime));

        if (enpanne) continue;

        demandeSC();
        entrerSC();

        int csTime = csDist(rng);
        cout << "Processus " << id << " est en SC pendant " << csTime << " secondes" << endl;
        this_thread::sleep_for(chrono::seconds(csTime));

        quitterSC();
    }
}

// Demande d'accès à la section critique
void Process::demandeSC() {
    lock_guard<mutex> lock(tokenMutex);

    if (jetonpresent) {
        cout << "Processus " << id << " a déjà le jeton, pas besoin de le demander" << endl;
        return;
    }

    horlogelogique++;
    requetes[id] = horlogelogique;

    updateState(REQUESTING);
    cout << "Processus " << id << " demande la SC avec horloge=" << horlogelogique << endl;

    Message msg{REQUEST, id, horlogelogique, {}};
    broadcastMessage(msg);

    unique_lock<mutex> waitLock(tokenMutex);
    tokenCV.wait(waitLock, [&]{ return jetonpresent.load() || enpanne.load(); });
}

// Entrée en section critique
void Process::entrerSC() {
    dedans = true;
    updateState(IN_CS);
    cout << "Processus " << id << " entre en SC" << endl;
}

// Sortie de section critique
void Process::quitterSC() {
    dedans = false;
    jeton[id] = horlogelogique;

    updateState(IDLE);
    cout << "Processus " << id << " sort de SC" << endl;

    for (int j = (id+1)%NUM_PROCESS; j != id; j = (j+1)%NUM_PROCESS) {
        if (requetes[j] > jeton[j]) {
            cout << "Processus " << id << " envoie le jeton au processus " << j << endl;
            jetonpresent = false;
            Message m{TOKEN, id, horlogelogique, jeton};
            envoiMessage(j, m);
            return;
        }
    }

    cout << "Processus " << id << " garde le jeton (aucun demandeur)" << endl;
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

    bind(serverSocket, (sockaddr*)&addr, sizeof(addr));
    listen(serverSocket, NUM_PROCESS);

    while (true) {
        sockaddr_in cli{};
        socklen_t len = sizeof(cli);
        int cliSock = accept(serverSocket, (sockaddr*)&cli, &len);
        if (cliSock < 0) continue;

        char buf[16];
        if (recv(cliSock, buf, sizeof(buf), 0) > 0) {
            int peerId = atoi(buf);
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
    cout << "Processus " << id << " : Tentative de connexion..." << endl;
    for (int i=0; i<NUM_PROCESS; ++i) {
        if (i==id) continue;
        int sock = socket(AF_INET, SOCK_STREAM,0);
        sockaddr_in a{AF_INET, htons(BASE_PORT+i), 0};
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        for(int attempt=0; attempt<5 && connect(sock,(sockaddr*)&a,sizeof(a))!=0; ++attempt)
            this_thread::sleep_for(chrono::seconds(1));

        string idStr = to_string(id);
        send(sock, idStr.c_str(), idStr.size()+1, 0);
        lock_guard<mutex> lk(socketMutex);
        clientSockets[i]=sock;
    }
}

// Gestion d'une connexion client
void Process::handleClientConnection(int clientSocket) {
    char buf[1024];
    ssize_t n;
    while ((n=recv(clientSocket, buf, sizeof(buf),0))>0) {
        Message msg;
        memcpy(&msg.type,   buf,               sizeof(msg.type));
        memcpy(&msg.sender, buf+sizeof(msg.type), sizeof(int));
        memcpy(&msg.clock,  buf+sizeof(msg.type)+sizeof(int), sizeof(int));
        if (msg.type==TOKEN) {
            msg.token.resize(NUM_PROCESS);
            memcpy(msg.token.data(),
                   buf+sizeof(msg.type)+2*sizeof(int),
                   NUM_PROCESS*sizeof(int));
        }
        recevoirMessage(msg);
    }
}

// Traitement d'un message reçu
void Process::recevoirMessage(const Message& msg) {
    int old = horlogelogique;
    horlogelogique = max(horlogelogique.load(), msg.clock)+1;
    cout << "Processus " << id << " : Msg de " << msg.sender
         << (msg.type==REQUEST?" REQUEST":" TOKEN")
         << " clk="<<msg.clock<<" (local "<<old<<"->"<<horlogelogique<<")\n";

    if (msg.type==REQUEST) {
        requetes[msg.sender]=max(requetes[msg.sender], msg.clock);
        if (jetonpresent && !dedans && requetes[msg.sender]>jeton[msg.sender]) {
            cout<<"Processus "<<id<<" : envoi jeton à "<<msg.sender<<endl;
            jetonpresent=false;
            Message m{TOKEN,id,horlogelogique,jeton};
            envoiMessage(msg.sender,m);
        }
    } else {
        jeton=msg.token;
        jetonpresent=true;
        cout<<"Processus "<<id<<" : Jeton reçu de "<<msg.sender<<endl;
        tokenCV.notify_all();
    }
}

// Envoi d'un message
void Process::envoiMessage(int targetId, const Message& msg) {
    lock_guard<mutex> lk(socketMutex);
    auto it=clientSockets.find(targetId);
    if (it==clientSockets.end()) return;
    char buf[1024]; size_t off=0;
    memcpy(buf+off,&msg.type,sizeof(msg.type)); off+=sizeof(msg.type);
    memcpy(buf+off,&msg.sender,sizeof(int));     off+=sizeof(int);
    memcpy(buf+off,&msg.clock,sizeof(int));      off+=sizeof(int);
    if (msg.type==TOKEN) {
        memcpy(buf+off,msg.token.data(),
               msg.token.size()*sizeof(int));
        off+=msg.token.size()*sizeof(int);
    }
    send(it->second,buf,off,0);
}

// Diffusion d'un message
void Process::broadcastMessage(const Message& msg) {
    vector<int> dests;
    {
      lock_guard<mutex> lk(socketMutex);
      for(auto &p:clientSockets) dests.push_back(p.first);
    }
    for(int d:dests) envoiMessage(d,msg);
}

// màj de l'état du processus
void Process::updateState(ProcessState newState) {
    state=newState;
    static const char* names[]={"IDLE","REQUESTING","IN_CS","FAILED"};
    cout<<"Processus "<<id<<" : État changé à "<<names[newState]<<endl;
}

// Génération d'un temps aléatoire
int Process::getRandomTime(int minSec, int maxSec) {
    static mt19937 rng(random_device{}());
    uniform_int_distribution<> dist(minSec,maxSec);
    return dist(rng);
}
