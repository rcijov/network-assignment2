#pragma once
#pragma comment(lib,"wsock32.lib")
#include <winsock.h>
#include <iostream>
#include <fstream>
#include <windows.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <process.h>
#include "Headers\dirent.h"

#define STIMER 0
#define UTIMER 300000

#define PORT1 5001
#define PORT2 7001

#define MAX_RETRIES 10

#define INPUT_LENGTH    40
#define HOSTNAME_LENGTH 40
#define USERNAME_LENGTH 40
#define FILENAME_LENGTH 40
#define MAX_FRAME_SIZE 256
#define SEQUENCE_WIDTH 1

typedef enum { GET = 1, PUT, LIST } Direction;
typedef enum { TIMEOUT = 1, INCOMING_PACKET, RECEIVE_ERROR } ReceiveResult;
typedef enum { CLIENT_REQ = 1, ACK_CLIENT_NUM, ACK_SERVER_NUM, FILE_NOT_EXIST, INVALID } HandshakeType;
typedef enum { INITIAL_DATA = 1, DATA, FINAL_DATA } MessageFrameHeader;
typedef enum { HANDSHAKE = 1, FRAME, FRAME_ACK } PacketType;

typedef struct {
	PacketType packet_type;
	int number;
} Acknowledgment;

typedef struct {
	PacketType packet_type;
	MessageFrameHeader header;
	unsigned int snwseq : SEQUENCE_WIDTH;
	int buffer_length;
	char buffer[MAX_FRAME_SIZE];
} MessageFrame;

typedef struct {
	PacketType packet_type;
	HandshakeType type;
	Direction direction;
	int client_number;
	int server_number;
	char hostname[HOSTNAME_LENGTH];
	char username[USERNAME_LENGTH];
	char filename[FILENAME_LENGTH];
} Handshake;

int sock;
struct sockaddr_in sa;
struct sockaddr_in sa_in;
int sa_in_size;
char server_name[HOSTNAME_LENGTH];
struct timeval timeouts;
int random;
WSADATA wsadata;

Handshake handshake;
std::ofstream fout;
