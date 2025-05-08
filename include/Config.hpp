#pragma once
// to avoid multiple and redundant declarations of globally used varuiables
const int NUM_PROCESS = 10; // total nodes
const int BASE_PORT=8000; // the starrting tcp port for listening between nodes since they communicate by sockets
const int WEBSOCKET_PORT=9000; // for the websocket ui dashboard


/*

to avoid hardcoding values throughout the codebase.
easier to maintain and modify system-wide settings.
improves code readability and avoids duplication.

*/
