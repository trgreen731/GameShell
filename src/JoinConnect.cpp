#include "inc/JoinConnect.h"

/*	JoinConnect Constructor:
 * 		Constructor for the joining connection class
 * 		Sets the initial value for the packet id sent by the join
 */
JoinConnect::JoinConnect(){
	prev_init = 0;
}

/*	init_join:
 * 		Called by the user to set up the socket, threads, and locks of the joining player
 *		Uses a single socket to send and receive
 *	returns: 0 for success, other for error
 */
int JoinConnect::init_join(Conn_Info_t* conn, std::string hostname){
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
	
	//creating a non-blocking socket
	if((conn->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SOCKET_ERROR){
		err_out(&(conn->err), "Socket Not Created. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
		return WSAGetLastError();
	}
	conn->ul = 1;
	if((conn->nRet = ioctlsocket(conn->s, FIONBIO, (unsigned long*)&(conn->ul))) == SOCKET_ERROR){
		err_out(&(conn->err), "Non-Blocking Mode Failed. Error Code: %d\n" + std::to_string(WSAGetLastError()) + "\n");
		return WSAGetLastError();
	}
	
	//fill server sockaddr structure
	memset((char*)&(conn->server), 0, sizeof(conn->server));
	conn->server.sin_family = AF_INET;
	conn->server.sin_addr.S_un.S_addr = inet_addr(hostname.c_str());
	conn->server.sin_port = htons(SERVER_PORT);
	
	//fill client sockaddr structure
	memset((char*)&(conn->client), 0, sizeof(conn->client));
	conn->client.sin_family = AF_INET;
	conn->client.sin_addr.s_addr = INADDR_ANY;
	conn->client.sin_port = htons(CLIENT_PORT);
	
	//bind the socket
	if(bind(conn->s, (struct sockaddr*)&(conn->client), sizeof(conn->client)) == SOCKET_ERROR){
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
	
	//initialize mutex states
	for(int i=0; i<MAX_PLAYER; i++){
		pthread_mutex_init(&(conn->players[i].lock), NULL);
	}
	pthread_mutex_init(&(conn->exit_lock), NULL);
	pthread_mutex_init(&(conn->send_p_lock), NULL);
	
	//perform the join request operation
	if((conn->self_player_num = join_request_handshake(conn)) == -1){
		conn->self_player_num = 0;
		conn->players[(int)(conn->self_player_num)].px_loc = 0.0;
		conn->players[(int)(conn->self_player_num)].py_loc = 0.0;
		conn->players[(int)(conn->self_player_num)].in_use = 1;
		conn->players[(int)(conn->self_player_num)].p_addr = conn->client;
		return -1;
	} else {
		conn->players[(int)(conn->self_player_num)].px_loc = 0.0;
		conn->players[(int)(conn->self_player_num)].py_loc = 0.0;
		conn->players[(int)(conn->self_player_num)].in_use = 1;
		conn->players[(int)(conn->self_player_num)].p_addr = conn->client;
		
		log_out(&(conn->log), "Successfully joined host with player number: " + std::to_string(conn->self_player_num) + "\n");
	}
	
	//create the send and recv threads
	t_send = pthread_create(&send_thread, NULL, join_send, (void*)conn);
	t_recv = pthread_create(&recv_thread, NULL, join_recv, (void*)conn);
	
	log_out(&(conn->log), "Send and Receive threads successfully created\n");
	
	prev_init = 1;
	return 0;
}

/*	quit_join:
 * 		Called by the user to have the joining player leave the connected game
 * 		Sends the quit request and ack will be received by the recv thread
 * 		*** may need to remove the exit timer if taking too long to exit after ack received
 *	returns: 0 for success, other for error
 */
int JoinConnect::quit_join(Conn_Info_t* conn){
	unsigned long last_sent = 0;
	char message[MAX_PACKET_LEN];
	
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
	
	//pause the sending thread (don't want any more key messages)
	pthread_mutex_lock(&(conn->send_p_lock));
	conn->send_p = 1;
	pthread_mutex_unlock(&(conn->send_p_lock));
	
	//build the quit request
	((Header_t*)message)->flags = PF_QUIT;
	((Header_t*)message)->player_id = conn->self_player_num;
	while(1){
		if((last_sent + REQ_TIMEOUT) < get_timestamp()){
			last_sent = get_timestamp();
			//send request and wait for response (check for timeout before resending)
			((Header_t*)message)->packet_num = conn->pkt_num;
			((Header_t*)message)->timestamp = get_timestamp();
			if(sendto(conn->s, message, PACKET_HEAD_LEN, 0, (struct sockaddr*)&(conn->server), sizeof(conn->server)) == SOCKET_ERROR){
				err_out(&(conn->err), "Send Failed. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
				return -1;
			}
			(conn->pkt_num)++;
		}
		
		//check if the ack has been received by the exit bit being set
		pthread_mutex_lock(&(conn->exit_lock));
		if(conn->exit){
			pthread_mutex_unlock(&(conn->exit_lock));
			
			log_out(&(conn->log), "All players successfully disconnected\n");
			
			//wait here for the threads to quit
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
		} else{
			pthread_mutex_unlock(&(conn->exit_lock));
		}
	}
}

/*	get_prev_init:
 * 		Called by user to check if this class has been initialized.
 *	returns: 0 if not initialized, 1 if initialized
 */
int JoinConnect::get_prev_init(){
	return prev_init;
}

/*	get_send_thread:
 * 		Called by user to get the send thread structure from the class
 *	returns: pthread_t structure for send thread
 */
pthread_t JoinConnect::get_send_thread(){
	return send_thread;
}

/*	get_recv_thread:
 * 		Called by user to get the recv thread structure from the class
 *	returns: pthread_t structure for recv thread
 */
pthread_t JoinConnect::get_recv_thread(){
	return recv_thread;
}

/*	join_request_handshake:
 * 		Performs the joining request handshake to connect to a host.
 * 		Sends the join request to specified host ip then waits for the ack
 *		this function called before starting threads as recv in two parallel threads not good
 *	returns: self player number assigned by host
 */
char JoinConnect::join_request_handshake(Conn_Info_t* conn){
	unsigned long last_sent = 0;
	char message[MAX_PACKET_LEN];
	char buf[MAX_PACKET_LEN];
	struct sockaddr_in si_other;
	int slen = sizeof(si_other);
	int numbytes;
	
	//null check
	if(conn == NULL){
		return -1;
	}
	
	//time of starting the request sending
	unsigned long start_join_request = get_timestamp();
	
	//build the join request
	((Header_t*)message)->flags = PF_JOIN;
	((Header_t*)message)->player_id = 0xFF;
	while(start_join_request + CONN_LOST > get_timestamp()){
		if((last_sent + REQ_TIMEOUT) < get_timestamp()){
			last_sent = get_timestamp();
			//send request and wait for response (check for timeout before resending)
			((Header_t*)message)->packet_num = conn->pkt_num;
			((Header_t*)message)->timestamp = get_timestamp();
			if(sendto(conn->s, message, PACKET_HEAD_LEN, 0, (struct sockaddr*)&(conn->server), sizeof(conn->server)) == SOCKET_ERROR){
				err_out(&(conn->err), "Send Failed. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
				return -1;
			}
			(conn->pkt_num)++;
		}
		if((numbytes = recvfrom(conn->s, buf, MAX_PACKET_LEN, 0, (struct sockaddr*)&si_other, &slen)) != SOCKET_ERROR){
			//check that the source matches the server address and player num matches our number
			if(conn->server.sin_addr.S_un.S_addr != si_other.sin_addr.S_un.S_addr){
				return -1;
			} else if((((Header_t*)buf)->flags & PF_DENY) == PF_DENY){
				err_out(&(conn->err), "Unable to join game at this time\n");
				return -1;
			} else if((((Header_t*)buf)->flags & (PF_JOIN | PF_ACK)) == (PF_JOIN | PF_ACK)){
				return ((Header_t*)buf)->player_id;
			}
		} else if(WSAGetLastError() != WSAEWOULDBLOCK){
			err_out(&(conn->err), "Receive Failed. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
			return -1;
		}
	}
	
	//if still no response then can't connect
	log_out(&(conn->log), "Cannot connect to host\n");
	return -1;
}

// ##################################################################### Thread Functions and Helpers

/*	join_recv:
 * 		Recv thread function for the joining player.
 * 		Non-blocking UDP recv calls that gets server packets and forks the process to handle the packets
 * 		*** add backlog limit to the number of packets that can be handled at a time
 *	returns: N/A (thread functions have no return value)
 */
void* join_recv(void* input){
	Conn_Info_t* conn = (Conn_Info_t*) input;
	char buf[MAX_PACKET_LEN];
	struct sockaddr_in si_other;
	int slen = sizeof(si_other);
	int numbytes;
	unsigned long last_recv = 0;
	
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
			last_recv = get_timestamp();
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
					rt[i].t_handler = pthread_create(&(rt[i].handler_thread), NULL, join_pkt_handle_wrap, (void*)(&rt[i]));
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
		} else if(last_recv != 0 && last_recv + CONN_LOST < get_timestamp()){
			//too long without packet so quit
			log_out(&(conn->log), "Lost Connection to Host\n");
			pthread_mutex_lock(&(conn->exit_lock));
			conn->exit = 1;
			pthread_mutex_unlock(&(conn->exit_lock));
		}
		
		//check the status of the other threads and terminate if necessary
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
 * join_pkt_handle_wrap:
 * 		Wrapper around the host packet handler to be used as a separate thread function
 */
void* join_pkt_handle_wrap(void* input){
	Recv_Thread_t* rt_in = (Recv_Thread_t*) input;
	//if the input is null then cannot free the space up and want error to be thrown
	if(join_pkt_handle(rt_in->conn, rt_in->numbytes, rt_in->buf, &(rt_in->si_other)) == -1){
		err_out(&(rt_in->conn->err), "Packet handled incorrectly\n");
	}
	pthread_mutex_lock(&(rt_in->use_lock));
	rt_in->use_handler = 0;
	pthread_mutex_unlock(&(rt_in->use_lock));
	pthread_exit(NULL);
}

/*	join_pkt_handle:
 * 		Incoming packet handler for the join recv thread.
 * 		Determines the type of packet, confirms the server's address, and performs necessary operations for the pkt.
 * 		This function can also send an ACK for quit requests
 *	returns: 0 for sucess, -1 for error
 */
int join_pkt_handle(Conn_Info_t* conn, int bytes, char* buf, struct sockaddr_in* si_other){
	//null check
	if(buf == NULL || si_other == NULL){
		return -1;
	}
	//check that the source matches the server address and player num matches our number
	if(conn->server.sin_addr.S_un.S_addr != si_other->sin_addr.S_un.S_addr || conn->self_player_num != buf[1]){
		return -1;
	}
	
	//check that it is a disp or quit packet because don't know any others
	if((((Header_t*)buf)->flags & PF_DISP) == PF_DISP){
		//check for proper disp packet size
		if(bytes != disp_packet_len){
			return -1;
		}
		
		for(int i=0; i<MAX_PLAYER; i++){
			char bit = 0x01;
			if((((Disp_Packet_t*)buf)->in_use & (bit << i)) == (bit << i)){
				pthread_mutex_lock(&(conn->players[i].lock));
				conn->players[i].in_use = 1;
				conn->players[i].px_loc = ((Disp_Packet_t*)buf)->px_loc[i];
				conn->players[i].py_loc = ((Disp_Packet_t*)buf)->py_loc[i];
				pthread_mutex_unlock(&(conn->players[i].lock));
			}
			else{
				pthread_mutex_lock(&(conn->players[i].lock));
				conn->players[i].in_use = 0;
				pthread_mutex_unlock(&(conn->players[i].lock));
			}
		}
		
	} else if((((Header_t*)buf)->flags & PF_QUIT) == PF_QUIT){
		//check for proper quit request packet size
		if(bytes != PACKET_HEAD_LEN){
			return -1;
		}
		
		//send the quit ack if this is not the ack
		if((((Header_t*)buf)->flags & PF_ACK) != PF_ACK){
			char message[PACKET_HEAD_LEN];
			((Header_t*)message)->flags = PF_QUIT | PF_ACK;
			((Header_t*)message)->player_id = conn->self_player_num;
			((Header_t*)message)->packet_num = ((Header_t*)buf)->packet_num;
			((Header_t*)message)->timestamp = get_timestamp();
			if(sendto(conn->s, message, PACKET_HEAD_LEN, 0, (struct sockaddr*)&(conn->server), sizeof(conn->server)) == SOCKET_ERROR){
				err_out(&(conn->err), "Send Failed. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
				return WSAGetLastError();
			}
			log_out(&(conn->log), "Host has ended the game\n");
			
		} else{
			log_out(&(conn->log), "Successfully left the game\n");
		}
		
		//exit
		pthread_mutex_lock(&(conn->exit_lock));
		conn->exit = 1;
		pthread_mutex_unlock(&(conn->exit_lock));
		
	} else{
		err_out(&(conn->err), "Unknown Message Received\n");
		return -1;
	}
	return 0;
}

/*	join_send:
 * 		Send thread function for the joining player.
 * 		Builds the packet holding the keys information and sends it to the server
 *	returns: N/A (thread functions have no return value)
 */
void* join_send(void* input){
	Conn_Info_t* conn = (Conn_Info_t*) input;
	char* message = new char[MAX_PACKET_LEN];
	unsigned long last_sent = 0;
	
	//null check
	if(conn == NULL){
		delete(message);
		pthread_exit(NULL);
	}
	
	while(1){
		//build and send keys message if the max fps timer passed
		if(last_sent + max_client_time < get_timestamp()){
			last_sent = get_timestamp();
			//only send if pause bit is not set
			pthread_mutex_lock(&(conn->send_p_lock));
			if(!(conn->send_p)){
				pthread_mutex_unlock(&(conn->send_p_lock));
				if(join_build_keys_message(conn, message) == -1){
					err_out(&(conn->err), "Error Building Keys Message\n");
					pthread_mutex_lock(&(conn->exit_lock));
					conn->exit = 1;
					pthread_mutex_unlock(&(conn->exit_lock));
				} else{
					//send message to the server
					((Keys_Packet_t*)message)->head.timestamp = get_timestamp();
					if(sendto(conn->s, message, keys_packet_len, 0, (struct sockaddr*)&(conn->server), sizeof(conn->server)) == SOCKET_ERROR){
						err_out(&(conn->err), "Send Failed. Error Code: " + std::to_string(WSAGetLastError()) + "\n");
						pthread_mutex_lock(&(conn->exit_lock));
						conn->exit = 1;
						pthread_mutex_unlock(&(conn->exit_lock));
					}
					(conn->pkt_num)++;
				}
			} else{
				pthread_mutex_unlock(&(conn->send_p_lock));
			}
		
			//exit checking only performed once per frame like the sending
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
}

/*	join_build_keys_message:
 * 		Builds the keys message to be sent by the join send thread
 *		Uses the self player info to fill the packet buffer
 *	returns: 0 for sucess, -1 for error
 */
int join_build_keys_message(Conn_Info_t* conn, char* message){
	if(conn == NULL || message == NULL){
		return -1;
	}
	
	//clear the message then fill the pkt
	memset(message, '\0', MAX_PACKET_LEN);
	((Keys_Packet_t*)message)->head.flags = PF_KEYS;
	((Keys_Packet_t*)message)->head.player_id = conn->self_player_num;
	((Keys_Packet_t*)message)->head.packet_num = conn->pkt_num;
	pthread_mutex_lock(&(conn->players[(int)(conn->self_player_num)].lock));
	((Keys_Packet_t*)message)->px_loc = conn->self_x_loc;
	((Keys_Packet_t*)message)->py_loc = conn->self_y_loc;
	pthread_mutex_unlock(&(conn->players[(int)(conn->self_player_num)].lock));
	return 0;
}
