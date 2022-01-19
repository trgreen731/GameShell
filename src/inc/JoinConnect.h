#ifndef JOIN_CONNECT_H_
#define JOIN_CONNECT_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <winsock2.h>
#include <pthread.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include "ConnectStruct.h"

class JoinConnect {
	private:		
		//thread info
		pthread_t send_thread, recv_thread;
		int t_send, t_recv;
		
		//initialized
		int prev_init;
		
		//private helper functions
		char join_request_handshake(Conn_Info_t* conn_ptr);
	
	public:
		JoinConnect();
		int init_join(Conn_Info_t* conn_ptr, std::string hostname);
		int quit_join(Conn_Info_t* conn_ptr);
		int get_prev_init();
		pthread_t get_send_thread();
		pthread_t get_recv_thread();
};

//thread functions and helpers
void* join_recv(void* input);
void* join_pkt_handle_wrap(void* input);
int join_pkt_handle(Conn_Info_t* conn, int bytes, char* buf, struct sockaddr_in* si_other);

void* join_send(void* input);
int join_build_keys_message(Conn_Info_t* conn, char* message);

#endif
