#include "inc/ConnectStruct.h"

unsigned long get_timestamp(){
	unsigned long out = 0;
	SYSTEMTIME system_time;
	GetSystemTime(&system_time);
	out += (((unsigned long)(system_time.wMinute)) * 60000);
	out += (((unsigned long)(system_time.wSecond)) * 1000);
	out += ((unsigned long)(system_time.wMilliseconds));
	return out;
}

void err_out(std::ofstream* err, std::string text){
	#if ERR
	if(!err->is_open()){
		err->open("log/" + std::to_string(get_timestamp()) + ".err");
	}
	(*err) << text;
	#endif
	return;
}

void log_out(std::ofstream* log, std::string text){
	#if LOG
	if(!log->is_open()){
		log->open("log/" + std::to_string(get_timestamp()) + ".log");
	}
	(*log) << text;
	#endif
	return;
}
