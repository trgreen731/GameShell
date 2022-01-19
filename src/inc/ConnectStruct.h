#ifndef CONNECT_STRUCT_H_
#define CONNECT_STRUCT_H_

#include <pthread.h>
#include <winsock2.h>
#include <minwinbase.h>
#include <vector>
#include <string>
#include <fstream>

//test variables
#define LOG 1
#define ERR 1

//player info
#define PLAYER_SIZE 0.1

//timing info
#define MAX_FPS 240.0
#define MAX_CLIENT_PPS 240.0
#define MAX_SERVER_PPS 240.0

//socket connections
#define SERVER_PORT 3940 		// the port the host will use
#define CLIENT_PORT 3990		// the port the client will use
#define MAX_PLAYER 8			// max number of players in game including self
#define MAX_BACKLOG 10			// the max threads open handling messages
#define REQ_TIMEOUT 80 			// the time (ms) before resending request
#define CONN_LOST 30000			// the time (ms) without disp packet before quitting
#define MAX_PACKET_LEN 1414		// the max size of a single packet
#define PACKET_HEAD_LEN 14		// packet header length

//packet flags
#define PF_JOIN 0x01
#define PF_QUIT 0x02
#define PF_KEYS 0x04
#define PF_DISP 0x08
#define PF_ACK  0x10
#define PF_DENY 0x20

//ascii key info
#define W_ASCII 119
#define S_ASCII 115
#define A_ASCII 97
#define D_ASCII 100
#define ESC_ASCII 27

/* winsock codes
WSAEFAULT (10014) = bad address or size given to receive or send call
WSAEWOULDBLOCK (10035) = non-blocking recv call had nothing to recv
WSAEAFNOSUPPORT (10047) = bad send address due to IP family differences
WSAEADDRINUSE (10048) = binding failed because port already being used
WSAECONNRESET (10054) = abrupt disconnect means failed packet recv
WSANOTINITIALISED (10093) = attempting to send or recv with wsa not setup or closed
*/

//useful constants that depend on precompiler definitions
const unsigned int disp_packet_len = PACKET_HEAD_LEN + 1 + (8*MAX_PLAYER);
const unsigned int keys_packet_len = PACKET_HEAD_LEN + 8;
const unsigned long max_client_time = (unsigned long)(1000.0/MAX_CLIENT_PPS);
const unsigned long max_server_time = (unsigned long)(1000.0/MAX_SERVER_PPS);
const unsigned long max_fps_time = (unsigned long)(1000.0/MAX_FPS);

//all info needed for single player
typedef struct Player_Info {
	pthread_mutex_t lock;
	float px_loc;
	float py_loc;
	struct sockaddr_in p_addr;
	int in_use;
} Player_Info_t;

//structure holding important connection and player info
typedef struct Conn_Info {
	//socket connection info
	SOCKET s;
	unsigned long ul;
	int nRet;
	struct sockaddr_in server, client;
	WSADATA wsa;
	unsigned pkt_num;
	
	//logging info
	std::ofstream log, err;
	
	//player connections info
	char self_player_num;
	float self_x_loc;
	float self_y_loc;
	Player_Info_t players[MAX_PLAYER];
	pthread_mutex_t exit_lock, send_p_lock;
	int exit, send_p;
} Conn_Info_t;

//struct to hold the items necessary for a single recv thread
typedef struct Recv_Thread {
	//thread info
	pthread_t handler_thread;
	int t_handler;
	int use_handler;
	pthread_mutex_t use_lock;
	
	//packet info
	Conn_Info_t* conn;
	int numbytes;
	char buf[MAX_PACKET_LEN];
	sockaddr_in si_other;
} Recv_Thread_t;

//packet header format
typedef struct Header {
	char flags;
	char player_id;
	int packet_num;
	unsigned long timestamp;
} Header_t;

//display info packet format
typedef struct Disp_Packet {
	Header_t head;
	char in_use;
	float px_loc[MAX_PLAYER];
	float py_loc[MAX_PLAYER];
} Disp_Packet_t;

//key info packet format
typedef struct Keys_Packet {
	Header_t head;
	float px_loc;
	float py_loc;
} Keys_Packet_t;

//broad helper functions
unsigned long get_timestamp();
void err_out(std::ofstream* err, std::string text);
void log_out(std::ofstream* log, std::string text);

#endif
