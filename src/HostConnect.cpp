#include "inc/HostConnect.h"

/*	HostConnect:
 * 		Constructor for the host connection class.
 * 		This function does not initiaate host threads.
 */
HostConnect::HostConnect(){
	prev_init = 0;
}

/*	init_host:
 * 		Called by user to open and bind the UDP socket used to get and send info to other players.
 * 		Creates a send and recv thread and initializes player info and locks
 *	returns: 0 on success, other on error
 */
int HostConnect::init_host(Conn_Info_t* conn){
	//null check
	if(conn == NULL){
		return -1;
	}
	
	//check not init
	if(prev_init){
		return -1;
	}
	
	//initializing winsock
	if(WSAStartup(MAKEWORD(2,2),&(conn->wsa))!=0){
		err_out(&(conn->err), "Initialization Failed. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
		return WSAGetLastError();
	}
	
	//creating a non-blocking UDP socket
	if((conn->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SOCKET_ERROR){
		err_out(&(conn->err), "Socket Not Created. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
		return WSAGetLastError();
	}
	conn->ul = 1;
	if((conn->nRet = ioctlsocket(conn->s, FIONBIO, (unsigned long*)&(conn->ul))) == SOCKET_ERROR){
		err_out(&(conn->err), "Non-Blocking Mode Failed. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
		return WSAGetLastError();
	}
	
	//fill server sockaddr structure
	memset((char*)&(conn->server), 0, sizeof(conn->server));
	conn->server.sin_family = AF_INET;
	conn->server.sin_addr.s_addr = INADDR_ANY;
	conn->server.sin_port = htons(SERVER_PORT);
	
	//bind the socket
	if(bind(conn->s, (struct sockaddr*)&(conn->server), sizeof(conn->server)) == SOCKET_ERROR){
		err_out(&(conn->err), "Socket Bind Failed. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
		return WSAGetLastError();
	}
	
	log_out(&(conn->log), "Socket successfully created and bound to self address\n");
	
	//initialize the conn info player values
	for(int i=0; i<MAX_PLAYER; i++){
		conn->players[i].px_loc = 0.0;
		conn->players[i].py_loc = 0.0;
		memset((char*)&(conn->players[i].p_addr), 0, sizeof(conn->players[i].p_addr));
	}
	conn->self_x_loc = 0.0;
	conn->self_y_loc = 0.0;
	conn->exit = 0;
	conn->send_p = 0;
	conn->pkt_num = 0;
	
	//set the values for the self player
	conn->self_player_num = 0;
	conn->players[0].px_loc = 0.0;
	conn->players[0].py_loc = 0.0;
	conn->players[0].in_use = 1;
	conn->players[0].p_addr = conn->server;
	
	//initialize mutex states
	for(int i=0; i<MAX_PLAYER; i++){
		pthread_mutex_init(&(conn->players[i].lock), NULL);
	}
	pthread_mutex_init(&(conn->exit_lock), NULL);
	pthread_mutex_init(&(conn->send_p_lock), NULL);
	
	//create the send and recv threads
	t_send = pthread_create(&send_thread, NULL, host_send, (void*)conn);
	t_recv = pthread_create(&recv_thread, NULL, host_recv, (void*)conn);
	
	log_out(&(conn->log), "Send and Receive threads successfully created\n");
	
	prev_init = 1;
	return 0;
}

/*	quit_host:
 * 		Called by the user to quit hosting a multiplayer game.
 * 		First sending a quit request to each connected player and checks to see all disconnected
 * 		before quitting itself.
 *	returns: 0 for success, other for error
 */
int HostConnect::quit_host(Conn_Info_t* conn){
	unsigned long last_sent = 0;
	char message[MAX_PACKET_LEN];
	int all_quit = 0;
	
	//null check
	if(conn == NULL){
		return -1;
	}
	
	//check if the connections have already been terminated with the exit bit
	pthread_mutex_lock(&(conn->exit_lock));
	if(conn->exit){
		pthread_mutex_unlock(&(conn->exit_lock));
		log_out(&(conn->log), "Connection Already Terminated. Game Ended.\n");
		closesocket(conn->s);
		WSACleanup();
		return 0;
	} else{
		pthread_mutex_unlock(&(conn->exit_lock));
	}
	
	//pause the sending
	pthread_mutex_lock(&(conn->send_p_lock));
	conn->send_p = 1;
	pthread_mutex_unlock(&(conn->send_p_lock));
	
	while(!all_quit){
		if(last_sent + REQ_TIMEOUT < get_timestamp()){
			all_quit = 1;
			last_sent = get_timestamp();
			for(int i=1; i<MAX_PLAYER; i++){
				pthread_mutex_lock(&(conn->players[i].lock));
				if(conn->players[i].in_use){
					pthread_mutex_unlock(&(conn->players[i].lock));
					all_quit = 0;
					
					//build the quit request for each player and wait for an ack
					((Header_t*)message)->flags = PF_QUIT;
					((Header_t*)message)->player_id = i;
					((Header_t*)message)->packet_num = conn->pkt_num;
					((Header_t*)message)->timestamp = get_timestamp();
					pthread_mutex_lock(&(conn->players[i].lock));
					if(sendto(conn->s, message, PACKET_HEAD_LEN, 0, (struct sockaddr*)&(conn->players[i].p_addr), sizeof(conn->players[i].p_addr)) == SOCKET_ERROR){
						err_out(&(conn->err), "Send Failed. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
						return -1;
					}
					(conn->pkt_num)++;
					pthread_mutex_unlock(&(conn->players[i].lock));
				} else{
					pthread_mutex_unlock(&(conn->players[i].lock));
				}
			}
		}
	}
	pthread_mutex_lock(&(conn->exit_lock));
	conn->exit = 1;
	pthread_mutex_unlock(&(conn->exit_lock));
	
	log_out(&(conn->log), "All players successfully disconnected\n");
	
	//wait here for the threads to quit (always close send first)
	if(pthread_join(send_thread, NULL) != 0){
		err_out(&(conn->err), "Error ending send thread\n");
	}
	if(pthread_join(recv_thread, NULL) != 0){
		err_out(&(conn->err), "Error ending recv thread\n");
	}
	
	//close the socket
	closesocket(conn->s);
	WSACleanup();
	
	log_out(&(conn->log), "Send and Receive threads successfully closed\n");
	
	return 0;
}

/*	get_prev_init:
 * 		Called by user to check if this class has been initialized.
 *	returns: 0 if not initialized, 1 if initialized
 */
int HostConnect::get_prev_init(){
	return prev_init;
}

/*	get_send_thread:
 * 		Called by user to get the send thread structure from the class
 *	returns: pthread_t structure for send thread
 */
pthread_t HostConnect::get_send_thread(){
	return send_thread;
}

/*	get_recv_thread:
 * 		Called by user to get the recv thread structure from the class
 *	returns: pthread_t structure for recv thread
 */
pthread_t HostConnect::get_recv_thread(){
	return recv_thread;
}

// ##################################################################### Thread Functions and Helpers

/*	host_recv:
 * 		Recv thread function for the host.
 * 		Non-blocking UDP recv calls that gets player packets and forks the process to handle the packets
 * 		*** add backlog limit to the number of packets that can be handled at a time
 *	returns: N/A (thread functions have no return value)
 */
void* host_recv(void* input){
	Conn_Info_t* conn = (Conn_Info_t*) input;
	char buf[MAX_PACKET_LEN];
	struct sockaddr_in si_other;
	int slen = sizeof(si_other);
	int numbytes;
	
	//thread stuff
	Recv_Thread_t rt[MAX_BACKLOG];
	for(int i=0; i<MAX_BACKLOG; i++){
		rt[i].use_handler = 0;
		pthread_mutex_init(&(rt[i].use_lock), NULL);
	}
	
	//null check
	if(conn == NULL){
		pthread_exit(NULL);
	}
	
	while(1){
		memset(buf, '\0', MAX_PACKET_LEN);
		//non blocking call to receive UDP data
		if((numbytes = recvfrom(conn->s, buf, MAX_PACKET_LEN, 0, (struct sockaddr*)&si_other, &slen)) != SOCKET_ERROR){
			//action after receiving data (find an open thread)
			int i;
			for(i=0; i<MAX_BACKLOG; i++){
				pthread_mutex_lock(&(rt[i].use_lock));
				if(!rt[i].use_handler){
					rt[i].use_handler = 1;
					pthread_mutex_unlock(&(rt[i].use_lock));
					
					//fill the info struct
					rt[i].conn = conn;
					rt[i].numbytes = numbytes;
					memcpy(rt[i].buf, buf, MAX_PACKET_LEN);
					memcpy(&(rt[i].si_other), &si_other, slen);
					
					//set up the thread for packet handling here (it will return on its own)
					rt[i].t_handler = pthread_create(&(rt[i].handler_thread), NULL, host_pkt_handle_wrap, (void*)(&rt[i]));
					break;
					
				} else{
					pthread_mutex_unlock(&(rt[i].use_lock));
					
				}
			}
			//check if the action was completed or looped through all the options
			if(i == MAX_BACKLOG){
				err_out(&(conn->err), "Packet not handled. Max backlog exceeded.\n");
			}
		} else if(WSAGetLastError() != WSAEWOULDBLOCK){
			err_out(&(conn->err), "Receive Failed. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
			pthread_mutex_lock(&(conn->exit_lock));
			conn->exit = 1;
			pthread_mutex_unlock(&(conn->exit_lock));
		}
		
		//check the status of the send thread and terminate if necessary
		pthread_mutex_lock(&(conn->exit_lock));
		if(conn->exit){
			pthread_mutex_unlock(&(conn->exit_lock));
			pthread_exit(NULL);
		} else{
			pthread_mutex_unlock(&(conn->exit_lock));
		}
	}
}

/*
 * host_pkt_handle_wrap:
 * 		Wrapper around the host packet handler to be used as a separate thread function
 */
void* host_pkt_handle_wrap(void* input){
	Recv_Thread_t* rt_in = (Recv_Thread_t*) input;
	//if the input is null then cannot free the space up and want error to be thrown
	if(host_pkt_handle(rt_in->conn, rt_in->numbytes, rt_in->buf, &(rt_in->si_other)) == -1){
		err_out(&(rt_in->conn->err), "Packet handled incorrectly\n");
	}
	pthread_mutex_lock(&(rt_in->use_lock));
	rt_in->use_handler = 0;
	pthread_mutex_unlock(&(rt_in->use_lock));
	pthread_exit(NULL);
}

/*	host_pkt_handle:
 * 		Incoming packet handler for the host recv thread.
 * 		Determines the type of packet, confirms the sender's address, and performs necessary operations for the pkt.
 * 		This function can also send an ACK for join or quit requests
 *	returns: 0 on success, -1 on failure
 */
int host_pkt_handle(Conn_Info_t* conn, int bytes, char* buf, struct sockaddr_in* si_other){
	if(conn == NULL || buf == NULL || si_other == NULL){
		return -1;
	}
	
	//check the flags for the message type
	if((((Header_t*)buf)->flags & PF_JOIN) == PF_JOIN){
		char player_num;
		
		//check for proper join request size
		if(bytes != PACKET_HEAD_LEN){
			return -1;
		}
		
		//check if this source is already connected
		int i;
		for(i=1; i<MAX_PLAYER; i++){
			pthread_mutex_lock(&(conn->players[i].lock));
			if(conn->players[i].p_addr.sin_addr.S_un.S_addr == si_other->sin_addr.S_un.S_addr){
				player_num = i;
				pthread_mutex_unlock(&(conn->players[i].lock));
				break;
			} else{
				pthread_mutex_unlock(&(conn->players[i].lock));
			}
		}
		if(i == MAX_PLAYER){
			//check if there is a currently open spot
			for(i=1; i<MAX_PLAYER; i++){
				pthread_mutex_lock(&(conn->players[i].lock));
				if(!conn->players[i].in_use){
					//set up the player in this spot
					conn->players[i].px_loc = 0.0;
					conn->players[i].py_loc = 0.0;
					conn->players[i].in_use = 1;
					conn->players[i].p_addr = (*si_other);
				}
				pthread_mutex_unlock(&(conn->players[i].lock));
				break;
			}
			player_num = i; //if player num is MAX_PLAYER then no space
		}
		
		//send the ack
		char message[PACKET_HEAD_LEN];
		((Header_t*)message)->flags = PF_JOIN | PF_ACK;
		if(player_num == MAX_PLAYER){
			((Header_t*)message)->flags = ((Header_t*)message)->flags | PF_DENY;
		}
		((Header_t*)message)->player_id = player_num;
		((Header_t*)message)->packet_num = ((Header_t*)buf)->packet_num;
		((Header_t*)message)->timestamp = get_timestamp();
		pthread_mutex_lock(&(conn->players[(int)player_num].lock));
		if(sendto(conn->s, message, PACKET_HEAD_LEN, 0, (struct sockaddr*)&(conn->players[(int)player_num].p_addr), sizeof(conn->players[(int)player_num].p_addr)) == SOCKET_ERROR){
			err_out(&(conn->err), "Send Failed. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
			pthread_mutex_unlock(&(conn->players[(int)player_num].lock));
			return -1;
		} else{
			pthread_mutex_unlock(&(conn->players[(int)player_num].lock));
		}
		
	} else if((((Header_t*)buf)->flags & PF_QUIT) == PF_QUIT){
		//check for valid player number
		char player_num = ((Header_t*)buf)->player_id;
		if(player_num > 10){
			return -1;
		}
		
		//check for proper quit request size
		if(bytes != PACKET_HEAD_LEN){
			return -1;
		}
		
		pthread_mutex_lock(&(conn->players[(int)player_num].lock));
		if(conn->players[(int)player_num].in_use){
			if(conn->players[(int)player_num].p_addr.sin_addr.S_un.S_addr != si_other->sin_addr.S_un.S_addr){
				//source address does not match the player setup address
				pthread_mutex_unlock(&(conn->players[(int)player_num].lock));
				return -1;
			} else{
				pthread_mutex_unlock(&(conn->players[(int)player_num].lock));
			}
		} else{
			pthread_mutex_unlock(&(conn->players[(int)player_num].lock));
		}
		
		//otherwise proper request, only send the ack if this is not an ack
		if((((Header_t*)buf)->flags & PF_ACK) != PF_ACK){
			char message[PACKET_HEAD_LEN];
			((Header_t*)message)->flags = PF_QUIT | PF_ACK;
			((Header_t*)message)->player_id = player_num;
			((Header_t*)message)->packet_num = ((Header_t*)buf)->packet_num;
			((Header_t*)message)->timestamp = get_timestamp();
			pthread_mutex_lock(&(conn->players[(int)player_num].lock));
			if(sendto(conn->s, message, PACKET_HEAD_LEN, 0, (struct sockaddr*)si_other, sizeof(*(si_other))) == SOCKET_ERROR){
				pthread_mutex_unlock(&(conn->players[(int)player_num].lock));
				err_out(&(conn->err), "Send Failed. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
				return -1;
			} else{
				pthread_mutex_unlock(&(conn->players[(int)player_num].lock));
			}
		}
		
		//clear the proper data
		pthread_mutex_lock(&(conn->players[(int)player_num].lock));
		if(!conn->players[(int)player_num].in_use){
			pthread_mutex_unlock(&(conn->players[(int)player_num].lock));
		} else{
			//clear the player info
			memset((char*)&(conn->players[(int)player_num].p_addr), 0, sizeof(conn->players[(int)player_num].p_addr));
			conn->players[(int)player_num].px_loc = 0.0;
			conn->players[(int)player_num].py_loc = 0.0;
			conn->players[(int)player_num].in_use = 0;
			pthread_mutex_unlock(&(conn->players[(int)player_num].lock));
		}
		
	} else if((((Header_t*)buf)->flags & PF_KEYS) == PF_KEYS){
		//check for valid player number
		char player_num = ((Header_t*)buf)->player_id;
		if(player_num> 10){
			return -1;
		}
		
		//check for the proper keys message size
		if(bytes != keys_packet_len){
			return -1;
		}
		
		pthread_mutex_lock(&(conn->players[(int)player_num].lock));
		if(!conn->players[(int)player_num].in_use){
			//this player has not yet joined the game or already left
			pthread_mutex_unlock(&(conn->players[(int)player_num].lock));
			return -1;
		} else if(conn->players[(int)player_num].p_addr.sin_addr.S_un.S_addr != si_other->sin_addr.S_un.S_addr){
			//source address does not match the player setup address
			pthread_mutex_unlock(&(conn->players[(int)player_num].lock));
			return -1;
		} else{
			conn->players[(int)player_num].px_loc = ((Keys_Packet_t*)buf)->px_loc;
			conn->players[(int)player_num].py_loc = ((Keys_Packet_t*)buf)->py_loc;
			pthread_mutex_unlock(&(conn->players[(int)player_num].lock));
		}
		//no ack sent for key updates
		
	} else {
		err_out(&(conn->err), "Unknown Message Received\n");
		fflush(stdout);
		return -1;
	}
	return 0;
}

/*	host_send:
 * 		Send thread function for the host.
 * 		Builds the packet holding the display information and sends it to each connected player
 *	returns: N/A (thread functions have no return value)
 */
void* host_send(void* input){
	Conn_Info_t* conn = (Conn_Info_t*) input;
	char* message = new char[MAX_PACKET_LEN];
	unsigned long last_sent = 0;
	
	//null check
	if(conn == NULL){
		delete(message);
		pthread_exit(NULL);
	}
	
	while(1){
		//sending to each player only done based on max server pkt per sec
		if(last_sent + max_server_time < get_timestamp()){
			last_sent = get_timestamp();
			//only send messages if pause bit not set
			pthread_mutex_lock(&(conn->send_p_lock));
			if(!conn->send_p){
				pthread_mutex_unlock(&(conn->send_p_lock));
				//build the display message
				if(host_build_disp_message(conn, message) == -1){
					err_out(&(conn->err), "Error Building Disp Message\n");
					pthread_mutex_lock(&(conn->exit_lock));
					conn->exit = 1;
					pthread_mutex_unlock(&(conn->exit_lock));
				} else{
					//send message to each connected player (skip 0 since don't need to send to self)
					for(int i=1; i<MAX_PLAYER; i++){
						//check if the player is in use
						pthread_mutex_lock(&(conn->players[i].lock));
						if(conn->players[i].in_use){
							((Disp_Packet_t*)message)->head.player_id = i;
							((Disp_Packet_t*)message)->head.timestamp = get_timestamp();
							//send the message
							if(sendto(conn->s, message, disp_packet_len, 0, (struct sockaddr*)&(conn->players[i].p_addr), sizeof(conn->players[i].p_addr)) == SOCKET_ERROR){
								err_out(&(conn->err), "Send Failed. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
								pthread_mutex_unlock(&(conn->players[i].lock));
								pthread_mutex_lock(&(conn->exit_lock));
								conn->exit = 1;
								pthread_mutex_unlock(&(conn->exit_lock));
							} else{
								pthread_mutex_unlock(&(conn->players[i].lock));
							}
						} else{
							pthread_mutex_unlock(&(conn->players[i].lock));
						}
					}
					(conn->pkt_num)++;
				}
			} else{
				pthread_mutex_unlock(&(conn->send_p_lock));
			}
		}
		
		//check the status of the other threads and terminate if necessary
		pthread_mutex_lock(&(conn->exit_lock));
		if(conn->exit){
			pthread_mutex_unlock(&(conn->exit_lock));
			delete(message);
			pthread_exit(NULL);
		} else{
			pthread_mutex_unlock(&(conn->exit_lock));
		}
	}
}

/*	host_build_disp_message:
 * 		Display packet builder for the host send thread
 * 		Adds all current player info to the packet
 *	returns: 0 for success, -1 for error
 */
int host_build_disp_message(Conn_Info_t* conn, char* message){
	if(conn == NULL || message == NULL){
		return -1;
	}
	
	//clear the message then fill the pkt (player_id specific to player)
	memset(message, '\0', MAX_PACKET_LEN);
	((Disp_Packet_t*)message)->head.flags = PF_DISP;
	((Disp_Packet_t*)message)->head.packet_num = conn->pkt_num;
	
	//player info
	((Disp_Packet_t*)message)->in_use = 0;
	for(int i=0; i<MAX_PLAYER; i++){
		pthread_mutex_lock(&(conn->players[i].lock));
		if(conn->players[i].in_use){
			char bit = 0x01;
			((Disp_Packet_t*)message)->in_use += (bit << i);
			((Disp_Packet_t*)message)->px_loc[i] = conn->players[i].px_loc;
			((Disp_Packet_t*)message)->py_loc[i] = conn->players[i].py_loc;
		}
		pthread_mutex_unlock(&(conn->players[i].lock));
	}
	return 0;
}
