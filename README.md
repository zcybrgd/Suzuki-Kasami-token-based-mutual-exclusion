# Ricart-Agrawala-with-token-based-mutual-exclusion
simulating Ricart &amp; Agrawala 1983 with token-based mutual exclusion, peer-to-peer sockets, multithreading, and GUI to visualize process communication and token handover 

mkdir build
cd build
cmake ..
make
./ricart_agrawala <process_id>


## File Structure
project-root/
│
├── CMakeLists.txt or Makefile
│
├── include/                  # Public headers
│   ├── process.hpp           # Declaration of Process class
│   ├── message.hpp           # Message struct, enums
│   ├── config.hpp            # Constants (NUM_PROCESSES, ports, etc.)
│   └── utils.hpp             # Utility declarations
│
├── src/                      # Source code
│   ├── main.cpp              # Entry point
│   ├── process.cpp           # Process class implementation
│   ├── message.cpp           # JSON conversion, message helpers
│   ├── server.cpp            # TCP server handling
│   ├── websocket.cpp         # WebSocket server for UI
│   └── utils.cpp             # Utility function implementations
│
├── common/          
│   └── json.hpp          
│
└── build/  

