#ifndef HOST_CONNECT_H_
#define HOST_CONNECT_H_

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

class HostConnect {
	private:
		//thread info
		pthread_t send_thread, recv_thread;
		int t_send, t_recv;
		
		//initialized
		int prev_init;
	
	public:
		HostConnect();
		int init_host(Conn_Info_t* conn_ptr);
		int quit_host(Conn_Info_t* conn_ptr);
		int get_prev_init();
		pthread_t get_send_thread();
		pthread_t get_recv_thread();
};

//thread functions and helpers
void* host_recv(void* input);
void* host_pkt_handle_wrap(void* input);
int host_pkt_handle(Conn_Info_t* conn, int bytes, char* buf, struct sockaddr_in* si_other);

void* host_send(void* input);
int host_build_disp_message(Conn_Info_t* conn, char* message);

#endif
