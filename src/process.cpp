class Process {
private:
    int id;                             // ID du processus (de 1 à N)
    atomic<int> clock;                  // Horloge logique
    atomic<bool> hasToken;              // Possession du jeton
    atomic<ProcessState> state;         // État du processus
    vector<int> token;                  // Tableau du jeton
    vector<int> requests;               // Tableau des requêtes
    atomic<bool> inCS;                  // Dans la section critique
    
    // Sockets et communication
    int serverSocket;                   // Socket serveur
    map<int, int> clientSockets;        // Sockets clients
    thread serverThread;                // Thread serveur
    thread mainProcessThread;           // Thread principal du processus
    
    // Variables de synchronisation
    mutex tokenMutex;                   // Mutex pour protéger le jeton
    condition_variable tokenCV;         // Variable de condition pour l'attente du jeton
    
    // Socket pour l'UI Web
    int webSocketServer;
    vector<int> webClients;
    thread webSocketThread;
    
    // État pour l'interface graphique
    json uiState;
    mutex uiStateMutex;
    
    // Générateur de nombres aléatoires
    mt19937 rng;
    
public:
    Process(int id, bool initialToken = false) : 
        id(id), 
        clock(0), 
        hasToken(initialToken), 
        state(IDLE),
        inCS(false) {
        
        // Initialisation des tableaux
        token.resize(NUM_PROCESSES, 0);
        requests.resize(NUM_PROCESSES, 0);
        
        // Initialisation du générateur aléatoire
        random_device rd;
        rng = mt19937(rd());
        
        // Initialisation de l'état UI
        updateUIState();
    }
    
    ~Process() {
        stop();
    }
    
    // Démarrage du processus
    void start() {
        // Démarrage du serveur de socket
        serverThread = thread(&Process::runServer, this);
        
        // Démarrage du serveur websocket pour l'UI
        webSocketThread = thread(&Process::runWebSocketServer, this);
        
        // Attente pour s'assurer que tous les serveurs sont démarrés
        this_thread::sleep_for(chrono::seconds(2));
        
        // Connexion aux autres processus
        connectToOtherProcesses();
        
        // Démarrage du thread principal
        mainProcessThread = thread(&Process::run, this);
    }
    
    // Arrêt du processus
    void stop() {
        // Fermeture des sockets
        close(serverSocket);
        for (auto& socket : clientSockets) {
            close(socket.second);
        }
        close(webSocketServer);
        
        // Attente de la fin des threads
        if (serverThread.joinable()) serverThread.join();
        if (mainProcessThread.joinable()) mainProcessThread.join();
        if (webSocketThread.joinable()) webSocketThread.join();
    }
    
    // Simuler une panne
    void fail() {
        state = FAILED;
        updateUIState();
    }
    
    // Récupérer d'une panne
    void recover() {
        state = IDLE;
        updateUIState();
    }
    
private:
    // Boucle principale du processus
    void run() {
        while (true) {
            if (state != FAILED) {
                // Attente aléatoire avant de demander la section critique
                int waitTime = getRandomTime(1000, 5000);
                this_thread::sleep_for(chrono::milliseconds(waitTime));
                
                // Demande de section critique
                requestCriticalSection();
                
                // Exécution de la section critique
                int csTime = getRandomTime(1000, 2000);
                this_thread::sleep_for(chrono::milliseconds(csTime));
                
                // Sortie de la section critique
                exitCriticalSection();
            } else {
                // En panne, on attend
                this_thread::sleep_for(chrono::milliseconds(1000));
            }
        }
    }
    
    // Algorithme de Ricart & Agrawala pour demander la section critique
    void requestCriticalSection() {
        if (state == FAILED) return;
        
        state = REQUESTING;
        updateUIState();
        
        // Incrémentation de l'horloge
        clock++;
        
        // Si on a déjà le jeton, on entre directement en section critique
        {
            unique_lock<mutex> lock(tokenMutex);
            if (hasToken) {
                enterCriticalSection();
                return;
            }
        }
        
        // Diffusion de la requête à tous les autres processus
        Message msg;
        msg.type = REQUEST;
        msg.sender = id;
        msg.clock = clock;
        
        broadcastMessage(msg);
        
        // Attente du jeton
        {
            unique_lock<mutex> lock(tokenMutex);
            tokenCV.wait(lock, [this] { return hasToken || state == FAILED; });
            
            if (state != FAILED) {
                enterCriticalSection();
            }
        }
    }
    
    // Entrée en section critique
    void enterCriticalSection() {
        inCS = true;
        state = IN_CS;
        updateUIState();
    }
    
    // Sortie de la section critique
    void exitCriticalSection() {
        if (state == FAILED) return;
        
        inCS = false;
        state = IDLE;
        
        // Mise à jour du jeton
        token[id-1] = clock;
        
        // Recherche du prochain processus à qui envoyer le jeton
        unique_lock<mutex> lock(tokenMutex);
        
        for (int j = id; j <= NUM_PROCESSES + id - 1; j++) {
            int nextProc = ((j - 1) % NUM_PROCESSES) + 1;
            if (nextProc != id && requests[nextProc-1] > token[nextProc-1]) {
                // Envoi du jeton au processus suivant
                hasToken = false;
                
                Message msg;
                msg.type = TOKEN;
                msg.sender = id;
                msg.token = token;
                
                sendMessage(nextProc, msg);
                break;
            }
        }
        
        updateUIState();
    }
    
    // Traitement des messages reçus
    void handleMessage(const Message& msg) {
        if (state == FAILED) return;
        
        switch (msg.type) {
            case REQUEST: {
                // Mise à jour du tableau des requêtes
                requests[msg.sender-1] = max(requests[msg.sender-1], msg.clock);
                
                // Si on a le jeton et qu'on n'est pas en section critique, on le transmet
                unique_lock<mutex> lock(tokenMutex);
                if (hasToken && !inCS) {
                    if (requests[msg.sender-1] > token[msg.sender-1]) {
                        hasToken = false;
                        
                        Message tokenMsg;
                        tokenMsg.type = TOKEN;
                        tokenMsg.sender = id;
                        tokenMsg.token = token;
                        
                        sendMessage(msg.sender, tokenMsg);
                    }
                }
                break;
            }
            case TOKEN: {
                // Réception du jeton
                unique_lock<mutex> lock(tokenMutex);
                token = msg.token;
                hasToken = true;
                
                // Notification de la réception du jeton
                tokenCV.notify_one();
                break;
            }
            default:
                break;
        }
        
        updateUIState();
    }
    
    // Démarrage du serveur socket
    void runServer() {
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        
        if (serverSocket < 0) {
            cerr << "Erreur lors de la création du socket serveur pour le processus " << id << endl;
            return;
        }
        
        // Réutilisation de l'adresse
        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        // Configuration de l'adresse du serveur
        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(BASE_PORT + id);
        
        if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            cerr << "Erreur lors du bind du socket serveur pour le processus " << id << endl;
            close(serverSocket);
            return;
        }
        
        if (listen(serverSocket, NUM_PROCESSES) < 0) {
            cerr << "Erreur lors de l'écoute du socket serveur pour le processus " << id << endl;
            close(serverSocket);
            return;
        }
        
        cout << "Processus " << id << " écoute sur le port " << (BASE_PORT + id) << endl;
        
        // Boucle d'acceptation des connexions
        while (true) {
            struct sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);
            
            int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
            
            if (clientSocket < 0) {
                cerr << "Erreur lors de l'acceptation d'une connexion pour le processus " << id << endl;
                continue;
            }
            
            // Lecture de l'ID du processus client
            int clientId;
            if (recv(clientSocket, &clientId, sizeof(clientId), 0) != sizeof(clientId)) {
                close(clientSocket);
                continue;
            }
            
            // Enregistrement du socket client
            clientSockets[clientId] = clientSocket;
            
            // Démarrage d'un thread pour gérer cette connexion
            thread([this, clientSocket, clientId]() {
                handleClientConnection(clientSocket, clientId);
            }).detach();
        }
    }
    
    // Gestion d'une connexion client
    void handleClientConnection(int clientSocket, int clientId) {
        while (true) {
            // Lecture du type de message
            MessageType type;
            if (recv(clientSocket, &type, sizeof(type), 0) != sizeof(type)) {
                break;
            }
            
            // Lecture de l'expéditeur
            int sender;
            if (recv(clientSocket, &sender, sizeof(sender), 0) != sizeof(sender)) {
                break;
            }
            
            // Lecture de l'horloge
            int msgClock;
            if (recv(clientSocket, &msgClock, sizeof(msgClock), 0) != sizeof(msgClock)) {
                break;
            }
            
            Message msg;
            msg.type = type;
            msg.sender = sender;
            msg.clock = msgClock;
            
            // Si c'est un message TOKEN, lire le vecteur token
            if (type == TOKEN) {
                msg.token.resize(NUM_PROCESSES);
                if (recv(clientSocket, msg.token.data(), sizeof(int) * NUM_PROCESSES, 0) != sizeof(int) * NUM_PROCESSES) {
                    break;
                }
            }
            
            // Traitement du message
            handleMessage(msg);
        }
        
        // Fermeture du socket
        close(clientSocket);
        clientSockets.erase(clientId);
    }
    
    // Connexion aux autres processus
    void connectToOtherProcesses() {
        for (int i = 1; i <= NUM_PROCESSES; i++) {
            if (i == id) continue;  // On ne se connecte pas à soi-même
            
            int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
            
            if (clientSocket < 0) {
                cerr << "Erreur lors de la création du socket client pour le processus " << id << endl;
                continue;
            }
            
            struct sockaddr_in serverAddr;
            memset(&serverAddr, 0, sizeof(serverAddr));
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
            serverAddr.sin_port = htons(BASE_PORT + i);
            
            // Tentative de connexion avec retries
            for (int retry = 0; retry < 5; retry++) {
                if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == 0) {
                    // Envoi de notre ID
                    send(clientSocket, &id, sizeof(id), 0);
                    
                    // Enregistrement du socket
                    clientSockets[i] = clientSocket;
                    break;
                }
                
                // Attente avant de réessayer
                this_thread::sleep_for(chrono::milliseconds(500));
            }
            
            if (clientSockets.find(i) == clientSockets.end()) {
                close(clientSocket);
            }
        }
    }
    
    // Envoi d'un message à un processus spécifique
    void sendMessage(int destId, const Message& msg) {
        if (clientSockets.find(destId) == clientSockets.end()) {
            cerr << "Processus " << id << " : Pas de connexion au processus " << destId << endl;
            return;
        }
        
        int socket = clientSockets[destId];
        
        // Envoi du type de message
        send(socket, &msg.type, sizeof(msg.type), 0);
        
        // Envoi de l'expéditeur
        send(socket, &msg.sender, sizeof(msg.sender), 0);
        
        // Envoi de l'horloge
        send(socket, &msg.clock, sizeof(msg.clock), 0);
        
        // Si c'est un message TOKEN, envoyer le vecteur token
        if (msg.type == TOKEN) {
            send(socket, msg.token.data(), sizeof(int) * NUM_PROCESSES, 0);
        }
    }
    
    // Diffusion d'un message à tous les processus
    void broadcastMessage(const Message& msg) {
        for (int i = 1; i <= NUM_PROCESSES; i++) {
            if (i != id) {  // Ne pas s'envoyer à soi-même
                sendMessage(i, msg);
            }
        }
    }
    
    // Génération d'un temps aléatoire
    int getRandomTime(int min, int max) {
        uniform_int_distribution<int> dist(min, max);
        return dist(rng);
    }
    
    // Mise à jour de l'état pour l'UI
    void updateUIState() {
        lock_guard<mutex> lock(uiStateMutex);
        
        uiState["id"] = id;
        uiState["clock"] = clock.load();
        uiState["hasToken"] = hasToken.load();
        
        string stateStr;
        switch (state) {
            case IDLE: stateStr = "IDLE"; break;
            case REQUESTING: stateStr = "REQUESTING"; break;
            case IN_CS: stateStr = "IN_CS"; break;
            case FAILED: stateStr = "FAILED"; break;
        }
        uiState["state"] = stateStr;
        
        uiState["token"] = token;
        uiState["requests"] = requests;
        
        // Diffusion de l'état à tous les clients web
        string jsonStr = uiState.dump();
        for (int client : webClients) {
            send(client, jsonStr.c_str(), jsonStr.length(), 0);
        }
    }
    
    // Serveur pour l'interface web
    void runWebSocketServer() {
        webSocketServer = socket(AF_INET, SOCK_STREAM, 0);
        
        if (webSocketServer < 0) {
            cerr << "Erreur lors de la création du socket web pour le processus " << id << endl;
            return;
        }
        
        // Réutilisation de l'adresse
        int opt = 1;
        setsockopt(webSocketServer, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        // Configuration de l'adresse du serveur
        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(WEB_SOCKET_PORT + id);
        
        if (bind(webSocketServer, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            cerr << "Erreur lors du bind du socket web pour le processus " << id << endl;
            close(webSocketServer);
            return;
        }
        
        if (listen(webSocketServer, 5) < 0) {
            cerr << "Erreur lors de l'écoute du socket web pour le processus " << id << endl;
            close(webSocketServer);
            return;
        }
        
        cout << "Interface web du processus " << id << " écoute sur le port " << (WEB_SOCKET_PORT + id) << endl;
        
        // Boucle d'acceptation des connexions
        while (true) {
            struct sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);
            
            int clientSocket = accept(webSocketServer, (struct sockaddr*)&clientAddr, &clientAddrLen);
            
            if (clientSocket < 0) {
                cerr << "Erreur lors de l'acceptation d'une connexion web pour le processus " << id << endl;
                continue;
            }
            
            webClients.push_back(clientSocket);
            
            // Envoi immédiat de l'état actuel
            {
                lock_guard<mutex> lock(uiStateMutex);
                string jsonStr = uiState.dump();
                send(clientSocket, jsonStr.c_str(), jsonStr.length(), 0);
            }
            
            // Démarrage d'un thread pour gérer cette connexion
            thread([this, clientSocket]() {
                char buffer[1024];
                while (true) {
                    // Lecture des commandes depuis l'interface web
                    int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
                    if (bytesRead <= 0) break;
                    
                    buffer[bytesRead] = '\0';
                    string command(buffer);
                    
                    if (command == "fail") {
                        fail();
                    } else if (command == "recover") {
                        recover();
                    }
                }
                
                // Suppression du client de la liste
                auto it = find(webClients.begin(), webClients.end(), clientSocket);
                if (it != webClients.end()) {
                    webClients.erase(it);
                }
                
                close(clientSocket);
            }).detach();
        }
    }
};
