#pragma once

#include "Message.hpp"
#include "Config.hpp"
#include <atomic>

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

  void executer(); // son programme principale ou il demande la ressource critique
  void demandeSC();
  void entrerSC();
  void quitterSC();
  void receptionMessage(const Message& msg);
  void runServer(); //since les processus communiquent par sockets donc il attend que des clients se connectent et lance des threads pour gérer chaque connexion client
  void connectToOtherProcesses(); // initialiser le serveur de communication par sockets
  void envoiMessage(int targetId, const Message& msg);
  void broadcastMessage(const Message& msg); // to all processes (apart ceux qui sont en panne)
  void updateUIState();
  void handleClientConnection(int socket, int clientId); //appelée par un thread séparé pour gérer la communication avec un client spécifique après qu'une connexion a été établie
  void runWebSocketServer();

  int getRandomTime(int minMs, int maxMs);

 public:
    Process(int id, bool initialToken = false);
    ~Process(); //automatically called when a Process object is destroyed
    void start();
    void stop();
    void fail();
    void recover();

}
