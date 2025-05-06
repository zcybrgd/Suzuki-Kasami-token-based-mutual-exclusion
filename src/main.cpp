#include "process.hpp"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: ./ricart_agrawala <process_id>\n";
        return 1;
    }

    int id = std::stoi(argv[1]);
    Process p(id);
    p.run();

    return 0;
}
