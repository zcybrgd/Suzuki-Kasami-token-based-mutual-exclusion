#pragma once

#include "Message.hpp"
#include "Config.hpp"
#include "WebSocketServer.hpp"
#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <map>
using namespace std;

enum ProcessState {

 IDLE, // repos

REQUESTING, IN_CS, FAILED // en panne

};



class Process {

 private:
  int id;
  atomic<int> horlogelogique; // horloge logique locale de lamport
  atomic<bool> jetonpresent;              // possession du jeton
  atomic<ProcessState> state;         // État du processus
  vector<int> jeton;                  // Tableau du jeton
  vector<int> requetes;               // Tableau des requêtes
  atomic<bool> dedans;                  // Dans la section critique
  atomic<bool> enpanne; // has failed ou pas

  mutex tokenMutex;
  condition_variable tokenCV;

  // Socket serveur et sockets clients
  int serverSocket;
  mutex socketMutex;
  map<int, int> clientSockets; // Map de (id -> socket)

  thread serverThread; // Thread d'écoute des connexions
  thread mainProcessThread;

  // webSocket server reference
  static WebSocketServer* ws_server;


  //le coeur de l'algorithme
  void run(); // son programme principale ou il demande la ressource critique
  void demandeSC();
  void entrerSC();
  void quitterSC();

  //gestion des communications
  void runServer();                   // Serveur d'écoute des connexions
  void connectToOtherProcesses();     // Établit les connexions avec les autres processus
  void handleClientConnection(int clientSocket); // traite les messages d'un client (appelee par un thread separe pour gerer la comm avec un client specifique apres qu une connexion a ete etablie)
  void recevoirMessage(const Message& msg); // reception d un message recu

  // Envoi de messages
  void envoiMessage(int targetId, const Message& msg); // to all processes (apart ceux qui sont en panne)
  void broadcastMessage(const Message& msg);

  void updateState(ProcessState newState);
  int getRandomTime(int minMs, int maxMs);


  // WebSocket notification methods
  void notifyStateChange();

 public:
    Process(int id, bool initialToken = false);
    ~Process(); //automatically called when a Process object is destroyed

    void start();
    void stop();
    void fail();
    void recover();

    // Static method to set WebSocket server
    static void setWebSocketServer(WebSocketServer* server);
    // Getters for frontend
    ProcessState getState() const { return state.load(); }
    int getId() const { return id; }
    vector<int> getQueue() const { return requetes; }
    bool hasToken() const { return jetonpresent.load(); }

};

