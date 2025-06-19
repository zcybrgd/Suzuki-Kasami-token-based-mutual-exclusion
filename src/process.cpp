#include "Process.hpp"
#include "Config.hpp"
void Process::handle_message(int client_socket) {
    Message msg;
    read(client_socket, &msg, sizeof(msg));
    
    std::unique_lock<std::mutex> lock(mtx);
    
    switch(msg.type) {
        case REQUEST:
            LN[msg.sender_id] = std::max(LN[msg.sender_id], msg.sequence_number);
            if (has_token && !requesting && 
                RN[msg.sender_id] == LN[msg.sender_id] - 1) {
                send_token(msg.sender_id);
            }
            break;
            
        case REPLY:
            // Shouldn't happen in Suzuki-Kasami, just acknowledge
            break;
            
        case TOKEN:
            has_token = true;
            requesting = false;
            std::cout << "Process " << id << " received token" << std::endl;
            cv.notify_all();
            break;
    }
}

void Process::send_token(int receiver) {
    has_token = false;
    Message token_msg;
    token_msg.type = TOKEN;
    token_msg.sender_id = id;
    token_msg.sequence_number = 0;
    
    std::cout << "Process " << id << " sending token to " << receiver << std::endl;
    
    // Update token's RN array before sending
    RN = LN;
    
    write(client_sockets[receiver], &token_msg, sizeof(token_msg));
}

void Process::server_loop() {
    while (true) {
        int new_socket;
        sockaddr_in address;
        int addrlen = sizeof(address);
        
        if ((new_socket = accept(server_fd, (sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }
        
        std::thread([this, new_socket]() {
            while (true) {
                handle_message(new_socket);
            }
            close(new_socket);
        }).detach();
    }
}

void Process::setup_server() {
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(BASE_PORT + id);
    
    if (bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    server_thread = std::thread(&Process::server_loop, this);
}

void Process::setup_clients() {
    client_sockets.resize(NUM_PROCESSES, -1);
    
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (i == id) continue;
        
        int sock = 0;
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("socket creation failed");
            continue;
        }
        
        sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(BASE_PORT + i);
        
        if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
            perror("invalid address");
            continue;
        }
        
        // Retry connection until successful
        while (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            std::cout << "Process " << id << " failed to connect to " << i << ", retrying..." << std::endl;
            sleep(1);
        }
        
        client_sockets[i] = sock;
    }
}

Process::Process(int pid) : id(pid), sequence_number(0), has_token(false), requesting(false), 
                  LN(NUM_PROCESSES, 0), RN(NUM_PROCESSES, 0) {
    if (id == 0) has_token = true; // Initialize token at process 0
    
    setup_server();
    setup_clients();
}

Process::~Process() {
    for (int sock : client_sockets) {
        if (sock != -1) close(sock);
    }
    close(server_fd);
}

void Process::request_critical_section() {
    std::unique_lock<std::mutex> lock(mtx);
    requesting = true;
    sequence_number++;
    LN[id] = sequence_number;
    
    std::cout << "Process " << id << " requesting CS with SN: " << sequence_number << std::endl;
    
    // Broadcast request to all other processes
    Message msg;
    msg.type = REQUEST;
    msg.sender_id = id;
    msg.sequence_number = sequence_number;
    
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (i != id && client_sockets[i] != -1) {
            write(client_sockets[i], &msg, sizeof(msg));
        }
    }
    
    // Wait for token
    cv.wait(lock, [this]() { return has_token; });
}

void Process::release_critical_section() {
    std::unique_lock<std::mutex> lock(mtx);
    RN[id] = LN[id];
    requesting = false;
    
    // Check if there are pending requests
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (RN[i] == LN[i] - 1 && i != id) {
            send_token(i);
            break;
        }
    }
    
    std::cout << "Process " << id << " released CS" << std::endl;
}

void Process::critical_section() {
    std::cout << "Process " << id << " entering critical section" << std::endl;
    // Simulate work in critical section
    sleep(1 + rand() % 3);
    std::cout << "Process " << id << " exiting critical section" << std::endl;
}
