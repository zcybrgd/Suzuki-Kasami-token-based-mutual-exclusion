#include "Process.hpp"
#include "Config.hpp"
#include <thread>
#include <vector>
#include <stdlib.h>
#include <time.h>

void process_work(Process& proc) {
    while (true) {
        // Random delay before requesting CS again
        sleep(1 + rand() % 5);
        
        proc.request_critical_section();
        proc.critical_section();
        proc.release_critical_section();
    }
}

int main() {
    srand(time(NULL)); // Seed random number generator
    std::vector<std::thread> process_threads;
    
    for (int i = 0; i < NUM_PROCESSES; i++) {
        process_threads.emplace_back([i]() {
            Process proc(i);
            process_work(proc);
        });
        
        // Stagger process creation slightly
        sleep(1);
    }
    
    for (auto& t : process_threads) {
        t.join();
    }
    
    return 0;
}
