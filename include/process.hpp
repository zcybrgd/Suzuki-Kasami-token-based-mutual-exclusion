#pragma once
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <map>

class Process {
public:
    Process(int id);
    void run();

private:
    void listener_thread();
    void request_token();
    void enter_critical_section();
    void release_token();
    void handle_request(int src_id, int timestamp);
    void handle_token(std::map<int, int> token_data);

    int id;
    int clock;
    int n = 10;

    std::atomic<bool> token_present;
    std::atomic<bool> in_cs;
    std::vector<int> request_ts;
    std::vector<int> token_ts;

    std::mutex mtx;
};
