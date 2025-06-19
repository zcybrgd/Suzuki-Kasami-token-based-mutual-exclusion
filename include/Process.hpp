#pragma once

#include "Message.hpp"
#include "Config.hpp"
#include "WebSocketServer.hpp"
#include "Token.hpp"

#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <map>
#include <vector>

using namespace std;

enum ProcessState {
    IDLE, //repos
    REQUESTING, IN_CS, FAILED};

class Process {
private:
    int id;
    atomic<int> horlogeLogique; // horloge logique de lamport du processus a cet instant
    atomic<bool> jetonPresent; // détention du jeton ou pas
    atomic<ProcessState> state;  //État du processus
    atomic<bool> dansSC; //nous sommes inside la section critique?
    atomic<bool> enPanne; //cas de panne simulée

    vector<int> requetes;// requetes[i] = dernière horloge reçue de Pi
    Token* jeton;  // Jeton (tableau LN[i]), présent uniquement si jetonPresent == true

    mutex tokenMutex;
    condition_variable tokenCV;

    //pour la communication socket
    int serverSocket; //chaque process est un serveur qui ecoute sur un port unique BASE_PORT+id
    map<int, int> clientSockets; //chaque process est un client tcp qui se connecte au 9 autres
    mutex socketMutex;

    thread serverThread;
    thread mainThread;

    // websocket dashboard
    static WebSocketServer* ws_server;

    //fonctionnement interne
    void run(); // boucle principale qui contient la logique du programme ran by the process et attend de 1 a 5 sec avant de demander une sc
    void demanderSC();//envoie REQ aux autres
    void entrerSC(); //attente jeton puis SC
    void quitterSC();//màj jeton et cherche à qui le transmettre

    // communication peer-to-peer
    void runServer(); // TCP Server
    void connectToPeers();// TCP Clients
    void handleClient(int clientSocket);
    void recevoirMessage(const Message& msg);// traitement REQ ou l'acquistion d'unTOKEN

    void envoyerMessage(int targetId, const Message& msg);//pour envoyer le jeton au client cible
    void broadcastMessage(const Message& msg);//pour diffuser une requete SC

    // === WEBSOCKET ET UTILITAIRES ===
    void updateState(ProcessState newState);
    void notifyStateChange();
    int getRandomDelay(int minMs, int maxMs);

public:
    Process(int id, bool initialToken = false);
    ~Process();

    void start();      //lance le serveur et la logique de demande SC
    void stop();       //arrête tous les threads

    void fail();       //simule un crash
    void recover();    //reprise après crash

    static void setWebSocketServer(WebSocketServer* server);

    // Accès pour le frontend
    ProcessState getState() const { return state.load(); }
    int getId() const { return id; }
    vector<int> getQueue() const { return requetes; }
    bool hasToken() const { return jetonPresent.load(); }
    bool isFailed() const { return enPanne.load(); }
};
