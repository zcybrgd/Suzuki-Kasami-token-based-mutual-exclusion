#include "Process.hpp"
#include "WebSocketServer.hpp"

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <csignal>
#include <random>

using namespace std;

vector<Process*> processes;
vector<thread> failureThreads;
WebSocketServer wsServer;
atomic<bool> running(true);

// Simule des pannes aléatoires toutes les X secondes
void simulateFailures() {
    while (running) {
        this_thread::sleep_for(chrono::seconds(15));

        int victim = rand() % NUM_PROCESS;
        cout << "[MAIN] Simulating failure of process P" << victim << endl;
        processes[victim]->fail();

        this_thread::sleep_for(chrono::seconds(5));

        cout << "[MAIN] Recovering process P" << victim << endl;
        processes[victim]->recover();
    }
}

void signalHandler(int signal) {
    cout << "\n[MAIN] Caught signal " << signal << ", shutting down..." << endl;
    running = false;
    for (auto& proc : processes) proc->stop();
    wsServer.stop();
    exit(0);
}

int main() {
    srand(time(nullptr));
    signal(SIGINT, signalHandler); // CTRL+C handling

    // Lancer le serveur WebSocket
    wsServer.start();
    Process::setWebSocketServer(&wsServer);

    // Créer les processus (P0 possède initialement le jeton)
    for (int i = 0; i < NUM_PROCESS; ++i) {
        processes.push_back(new Process(i, i == 0));
    }

    // Lancer les processus
    for (auto& proc : processes) {
        proc->start();
    }

    // Lancer le thread de simulation de pannes
    thread failureThread(simulateFailures);
    failureThreads.push_back(move(failureThread));

    // Attendre la fin
    for (auto& t : failureThreads) t.join();

    return 0;
}
