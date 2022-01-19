/*
** talker.c -- a datagram "client" demo with blocking recv sockets
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
	struct sockaddr_in si_other;
	int slen, numbytes;
	WSADATA wsa;
	char buf[BUFLEN];
	
	//check arguments
	if (argc != 3) {
		fprintf(stderr,"usage: ./talker hostname message\n");
		exit(1);
	}
	
	slen = sizeof(si_other);
	
	//initializing winsock
	if(WSAStartup(MAKEWORD(2,2),&wsa)!=0){
		printf("Initialization Failed. Error Code: %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	
	//creating a socket
	if((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SOCKET_ERROR){
		printf("Socket Not Created. Error Code: %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	
	//fill sockaddr structure
	memset((char*)&si_other, 0, slen);
	si_other.sin_family = AF_INET;
	si_other.sin_addr.S_un.S_addr = inet_addr(argv[1]);
	si_other.sin_port = htons(SERVERPORT);
	
	printf("Sending Message ...\n");
	
	//send the message
	if(sendto(s, argv[2], strlen(argv[2]), 0, (struct sockaddr*)&si_other, slen) == SOCKET_ERROR){
		printf("Send Failed. Error Code: %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	
	//clear the receiving buffer
	memset(buf, '\0', BUFLEN);
	
	//receive reply with a blocking call
	if((numbytes = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr*)&si_other, &slen)) == SOCKET_ERROR){
		printf("Receive Failed. Error Code: %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	
	printf("Reply: %s\n", buf);
	
	closesocket(s);
	WSACleanup();
	
	return 0;
}
