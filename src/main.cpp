#include "Process.hpp"
#include "Config.hpp"
#include "WebSocketServer.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <csignal>
#include <atomic>
#include <cstdlib>
#include <unistd.h>
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

// Variables globales pour la gestion de l'arrêt
vector<Process*> processes;
atomic<bool> running(true);
atomic<int> signalCount(0);
WebSocketServer* ws_server = nullptr;

void sigintHandler(int) {
    signalCount++;
    if (signalCount==1) {
        cout<<"\n\n=== ARRÊT DEMANDÉ (Ctrl+C again to force) ===\n";
        running=false;
    } else {
        for(auto&p:processes) p->stop();
        if (ws_server) ws_server->stop();
        if(signalCount>=3) abort();
    }
}

// Gestionnaire pour SIGTERM
void sigtermHandler(int) {
    running=false;
    for(auto&p:processes) p->stop();
    if (ws_server) ws_server->stop();
    exit(0);
}


// Simulation des pannes
void simulateFaults(vector<Process*>& procs) {
    mt19937 rng(random_device{}());
    uniform_int_distribution<> pd(0,NUM_PROCESS-1),td(5,15), dd(3,8);
    while(running) {
        for(int i=0;i<td(rng)&&running;i++) this_thread::sleep_for(1s);
        if(!running) break;
        int idx=pd(rng);
        cout<<"=== SIM: Panne P"<<idx<<" ===\n";
        procs[idx]->fail();
        for(int i=0;i<dd(rng)&&running;i++) this_thread::sleep_for(1s);
        if(!running) break;
        cout<<"=== SIM: Recovery P"<<idx<<" ===\n";
        procs[idx]->recover();
    }
}

// Stats périodiques
void displayStats() {
    while(running) {
        this_thread::sleep_for(2s);
        cout<<"=== STATS: "<<NUM_PROCESS<<" procs ===\n";
        // Send system state to WebSocket clients
        if (ws_server) {
            json system_state = json::array();
            for (auto& process : processes) {
                json proc_state;
                proc_state["id"] = to_string(process->getId());
                switch(process->getState()) {
                    case IDLE: proc_state["state"] = "IDLE"; break;
                    case REQUESTING: proc_state["state"] = "REQUESTING"; break;
                    case IN_CS: proc_state["state"] = "IN_CS"; break;
                    case FAILED: proc_state["state"] = "FAILED"; break;
                }
                proc_state["queue"] = process->getQueue();
                proc_state["hasToken"] = process->hasToken();
                system_state.push_back(proc_state);
            }
            ws_server->broadcast_system_state(system_state);
        }
    }
}


int main(){
    signal(SIGINT,  sigintHandler);
    signal(SIGTERM, sigtermHandler);
    cout<<"Démarrage RA83 ("<<NUM_PROCESS<<" procs)\n";

    ws_server = new WebSocketServer();
    ws_server->start();
    Process::setWebSocketServer(ws_server);

    for(int i=0;i<NUM_PROCESS;i++)
      processes.push_back(new Process(i,i==0));

    for(auto&p:processes) p->start();
    this_thread::sleep_for(3s);

    thread faultThread(simulateFaults,ref(processes));
    thread statsThread(displayStats);

    while(running) this_thread::sleep_for(500ms);

    cout<<"Arrêt...\n";
    for(auto&p:processes){ p->stop(); delete p; }
    if(faultThread.joinable()) faultThread.join();
    if(statsThread.joinable()) statsThread.join();

    if (ws_server) {
        ws_server->stop();
        delete ws_server;
    }

    cout<<"Terminé.\n";
    return 0;
}
