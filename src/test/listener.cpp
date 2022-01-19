/*
** listener.c -- a datagram "server" demo with non-blocking recv sockets
*/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <winsock2.h>

#define SERVERPORT 4950	// the port users will be connecting to
#define BUFLEN 1024

int main(int argc, char *argv[]){
	SOCKET s;
	unsigned long ul = 1;
	int nRet;
	struct sockaddr_in server, si_other;
	int slen, numbytes;
	WSADATA wsa;
	char buf[BUFLEN];
	
	//check arguments
	if (argc != 1) {
		fprintf(stderr,"usage: ./listener\n");
		exit(1);
	}
	
	slen = sizeof(si_other);
	
	//initializing winsock
	if(WSAStartup(MAKEWORD(2,2),&wsa)!=0){
		printf("Initialization Failed. Error Code: %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	
	//creating a non-blocking socket
	if((s = socket(AF_INET, SOCK_DGRAM, 0)) == SOCKET_ERROR){
		printf("Socket Not Created. Error Code: %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	if((nRet = ioctlsocket(s, FIONBIO, (unsigned long*)&ul)) == SOCKET_ERROR){
		printf("Non-Blocking Mode Failed. Error Code: %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	
	//fill sockaddr structure
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(SERVERPORT);
	
	//bind the socket
	if(bind(s, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR){
		printf("Socket Bind Failed. Error Code: %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	
	//listen loop
	numbytes = -1;
	while(numbytes == -1){
		printf("Waiting to Receive ...\n");
		fflush(stdout);
		
		//clear the buffer of previously received message
		memset(buf, '\0', BUFLEN);
		
		//non blocking call to receive UDP data
		if((numbytes = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr*)&si_other, &slen)) == SOCKET_ERROR){
			if(WSAGetLastError() == WSAEWOULDBLOCK){
				printf("No Data Received\n");
				continue;
			}
			else{
				printf("Receive Failed. Error Code: %d", WSAGetLastError());
				exit(EXIT_FAILURE);
			}
		}
	}
		
	//print transmission info
	printf("Received Packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
	printf("Data:%s\n", buf);
	
	//reply with the same data
	if(sendto(s, buf, numbytes, 0, (struct sockaddr*)&si_other, slen) == SOCKET_ERROR){
		printf("Send Failed. Error Code: %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	
	closesocket(s);
	WSACleanup();
	
	return 0;
}
