#pragma once
#include <vector>

using namespace std;
//définir les types de messages (REQUEST, TOKEN, STATUS_UPDATE) et la structure Message


enum MessageType {
 REQUEST, // demande de section critiques de tous les noeuds du systeme
 TOKEN, //envoi du token au processus demandant
 STATUS_UPDATE, // we'll need it later for frontend

};


struct Message {
  MessageType type;
  int sender;
  int clock; // horloge logique de lamport
  vector<int> token;
};
