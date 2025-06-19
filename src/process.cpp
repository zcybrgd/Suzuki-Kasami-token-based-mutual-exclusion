//each Process acts as a node in a simulated distributed system able to send and eceive messages, request the critical section (CS), and pass the token
//what do i need to take in consideration while coding (from l enonce tae TP):
/*
in Suzuki-Kasami: Only one token exists. The process with the token can enter the CS. Other processes request it by broadcasting their sequence number.

TCP Sockets are solely used for IPC

Each server accepts connections in a loop and handles them using detached threads
 */


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
            // Shouldn't happen in Suzuki-Kasami but just in case the prof demanded RA81 instead of RA83, just acknowledge
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
    
    //update token's RN array before sending
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
    //this creates a tcp socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(BASE_PORT + id); // the port number of the socket it should be unique
    //and here we bind the socket to its port number
    if (bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    //here we make the socket ready to accept connections by executing the function server_loop (socket is ready to work)
    server_thread = std::thread(&Process::server_loop, this);
}

void Process::setup_clients() {
    //each process connect to the other 9 available
    client_sockets.resize(NUM_PROCESSES, -1);
    //the loop of connecting to the nine other servers sockets
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
        
        //Retry connection until successful on sait jamais we have deadlocks somewher
        while (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            std::cout << "Process " << id << " failed to connect to " << i << ", retrying..." << std::endl;
            sleep(1);
        }
        //this quite literally lead us to full mesh connectivity like mentioned f le cours
        client_sockets[i] = sock;
    }
}

Process::Process(int pid) : id(pid), sequence_number(0), has_token(false), requesting(false), 
                  LN(NUM_PROCESSES, 0), RN(NUM_PROCESSES, 0) {
    if (id == 0) has_token = true; //initialize token at first process
    
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
    
    //Broadcast request to all other processes
    Message msg;
    msg.type = REQUEST;
    msg.sender_id = id;
    msg.sequence_number = sequence_number;
    
    for (int i = 0; i < NUM_PROCESSES; i++) {
        if (i != id && client_sockets[i] != -1) {
            write(client_sockets[i], &msg, sizeof(msg));
        }
    }
    
    //Wait for token
    cv.wait(lock, [this]() { return has_token; });
}

void Process::release_critical_section() {
    std::unique_lock<std::mutex> lock(mtx);
    RN[id] = LN[id];
    requesting = false;
    
    //check if there are pending requests to send to the first one that we meet asking for token
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
    // Simulate work in critical section (like in TP enonce)
    sleep(1 + rand() % 3);
    std::cout << "Process " << id << " exiting critical section" << std::endl;
}
