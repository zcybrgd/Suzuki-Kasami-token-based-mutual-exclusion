#pragma once
#include <vector>

struct Token {
    std::vector<int> lastRequestClock;// jeton[i] = horloge de la dernière visite à Pi

    Token(int numProcesses) {
        lastRequestClock.resize(numProcesses, 0);
    }
};
