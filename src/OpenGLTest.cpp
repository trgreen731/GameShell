#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

//common libraries
#include <iostream>
#include <fstream>
#include <pthread.h>
#include <vector>
#include <sys/time.h>
#include <windows.h>
#include "GL/freeglut.h"

//custom files
#include "inc/HostConnect.h"
#include "inc/JoinConnect.h"
#include "inc/ConnectStruct.h"

//globals
Conn_Info_t conn;
HostConnect hc;
JoinConnect jc;
unsigned long last_frame = 0;

void processNormKey(unsigned char key, int x, int y){
	
	//maybe avoid glut key tracking with "GetAsyncKeyState" in "winuser.h"
	
	pthread_mutex_lock(&(conn.players[(int)(conn.self_player_num)].lock));
	switch(key){
		case ESC_ASCII:
			if(hc.get_prev_init()){
				hc.quit_host(&conn);
			} else if(jc.get_prev_init()){
				jc.quit_join(&conn);
			}
			//close the files before exit
			#if ERR
			conn.err.close();
			#endif
			#if LOG
			conn.log.close();
			#endif

			exit(0);
			break;
		case W_ASCII:
			conn.players[(int)(conn.self_player_num)].py_loc += 0.05;
			conn.self_y_loc += 0.05;
			break;
		case A_ASCII:
			conn.players[(int)(conn.self_player_num)].px_loc -= 0.05;
			conn.self_x_loc -= 0.05;
			break;
		case S_ASCII:
			conn.players[(int)(conn.self_player_num)].py_loc -= 0.05;
			conn.self_y_loc -= 0.05;
			break;
		case D_ASCII:
			conn.players[(int)(conn.self_player_num)].px_loc += 0.05;
			conn.self_x_loc += 0.05;
		default:
			break;
	}
	pthread_mutex_unlock(&(conn.players[(int)(conn.self_player_num)].lock));
}

void display(){
	//only update if max fps timer has passed
	if(last_frame + max_fps_time < get_timestamp()){
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		
		for(int i=0; i<MAX_PLAYER; i++){
			pthread_mutex_lock(&(conn.players[i].lock));
			if(conn.players[i].in_use){
				glBegin(GL_POLYGON);
					glVertex2f((-1*PLAYER_SIZE)+conn.players[i].px_loc, (-1*PLAYER_SIZE)+conn.players[i].py_loc);
					glVertex2f((-1*PLAYER_SIZE)+conn.players[i].px_loc, PLAYER_SIZE+conn.players[i].py_loc);
					glVertex2f(PLAYER_SIZE+conn.players[i].px_loc, PLAYER_SIZE+conn.players[i].py_loc);
					glVertex2f(PLAYER_SIZE+conn.players[i].px_loc, (-1*PLAYER_SIZE)+conn.players[i].py_loc);
				glEnd();
			}
			pthread_mutex_unlock(&(conn.players[i].lock));
		}

		glutSwapBuffers();
	}
	//check on the connection threads to see if the game still going
	pthread_mutex_lock(&(conn.exit_lock));
	if(conn.exit){
		pthread_mutex_unlock(&(conn.exit_lock));
		
		//wait here for the threads to quit (always close send first)
		if(jc.get_prev_init()){
			if(pthread_join(jc.get_send_thread(), NULL) != 0){
				err_out(&(conn.err), "Error ending send thread\n");
			}
			if(pthread_join(jc.get_recv_thread(), NULL) != 0){
				err_out(&(conn.err), "Error ending recv thread\n");
			}
		} else if(hc.get_prev_init()){
			if(pthread_join(hc.get_send_thread(), NULL) != 0){
				err_out(&(conn.err), "Error ending send thread\n");
			}
			if(pthread_join(hc.get_recv_thread(), NULL) != 0){
				err_out(&(conn.err), "Error ending recv thread\n");
			}
		}
		
		//close the files before exit
		#if ERR
		conn.err.close();
		#endif
		#if LOG
		conn.log.close();
		#endif
			
		exit(0);
		
	} else{
		pthread_mutex_unlock(&(conn.exit_lock));
	}
}

void init(){
	glClearColor(0.0, 0.1, 0.4, 0.0);
	glColor3f(0.3, 0.3, 0.0);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(-1.0, 1.0, -1.0, 1.0);
}

int main(int argc, char** argv){
	//check input validity and set up the host or join connect class
	if(argc < 2){
		err_out(&(conn.err), "Improper Input: Must specify host or join\n");
		return -1;
	} else if(strcmp(argv[1], "host") == 0){
		hc.init_host(&conn);
	} else if(strcmp(argv[1], "join") == 0){
		if(argc < 3){
			err_out(&(conn.err), "Improper Input: If joining, must specify desired hostname\n");
			return -1;
		} else{
			std::string hostname = argv[2];
			jc.init_join(&conn, hostname);
		}
	} else{
		err_out(&(conn.err), "Improper Input: Must specify host or join\n");
		return -1;
	}
	
	//gl window setup
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
	glutInitWindowSize(640, 480);
	glutInitWindowPosition(0, 0);
	glutCreateWindow("Marvel Heros");
	
	//gl display and interaction functions
	glutDisplayFunc(display);
	glutIdleFunc(display);
	glutKeyboardFunc(processNormKey);
	
	//start gl loop
	init();
	glutMainLoop();
}
