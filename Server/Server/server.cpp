#include "server.h"
using namespace std;

bool FileExists(char * filename)
{
	int result;
	struct _stat stat_buf;
	result = _stat(filename, &stat_buf);
	return (result == 0);
}

long GetFileSize(char * filename)
{
	int result;
	struct _stat stat_buf;
	result = _stat(filename, &stat_buf);
	if (result != 0) return 0;
	return stat_buf.st_size;
}

int Send(int sock, Handshake * ptr_handshake, MessageFrame * ptr_message_frame, Acknowledgment * ack)
{
	int bytes = (ptr_handshake != nullptr) ? sendto(sock, (const char *)ptr_handshake, sizeof(*ptr_handshake), 0, (struct sockaddr*)&sa_in, sizeof(sa_in)) :
		(ptr_message_frame != nullptr ? sendto(sock, (const char*)ptr_message_frame, sizeof(*ptr_message_frame), 0, (struct sockaddr*)&sa_in, sizeof(sa_in)) :
		sendto(sock, (const char*)ack, sizeof(*ack), 0, (struct sockaddr*)&sa_in, sizeof(sa_in)));
	return bytes;
}


void err_sys(char * fmt, ...)
{
	perror(NULL);
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "error: ");
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
	printf(("Press Enter to exit.\n")); getchar();
	exit(1);
}

ReceiveResult Receive(int sock, MessageFrame * ptr_message_frame, Handshake * ptr_handshake, Acknowledgment * ack)
{
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(sock, &readfds);
	int bytes_recvd;
	int outfds = select(1, &readfds, NULL, NULL, &timeouts);
	switch (outfds)
	{
	case 0:
		return TIMEOUT; break;
	case 1:
		if (ptr_message_frame != nullptr)
		{
			bytes_recvd = recvfrom(sock, (char *)ptr_message_frame, sizeof(*ptr_message_frame), 0, (struct sockaddr*)&sa_in, &sa_in_size);
		}
		else if (ptr_handshake != nullptr)
		{
			bytes_recvd = recvfrom(sock, (char *)ptr_handshake, sizeof(*ptr_handshake), 0, (struct sockaddr*)&sa_in, &sa_in_size);
		}
		else if (ack != nullptr)
		{
			bytes_recvd = recvfrom(sock, (char *)ack, sizeof(*ack), 0, (struct sockaddr*)&sa_in, &sa_in_size);
		}
		return INCOMING_PACKET; break;
	default:
		return RECEIVE_ERROR; break;
	}
}

void createList()
{
	DIR *mydir = opendir(".");

	struct dirent *entry = NULL;
	char files[1028] = "";

	while ((entry = readdir(mydir)))
	{
		strcat(entry->d_name, "\r\n");
		strcat(files, entry->d_name);
	}

	closedir(mydir);

	FILE *file = fopen("List/list.txt", "w");

	int results = fputs(files, file);
	if (results == EOF) { fout << "Error writing the list file"; }
	fclose(file);
}

bool SendFile(int sock, char * filename, char * sending_hostname, int client_number, bool list)
{
	MessageFrame frame; frame.packet_type = FRAME;
	Acknowledgment ack; ack.number = -1;
	long bytes_counter = 0, byte_count = 0;
	int bytes_sent = 0, bytes_read = 0, bytes_read_total = 0;
	int packetsSent = 0, packetsSentNeeded = 0;
	bool bFirstPacket = true, bFinished = false;
	int sequence_number = client_number % 2;
	int nTries; bool bMaxAttempts = false;

	fout << "Sender started on host " << sending_hostname << endl;

	if (list)
	{
		createList();
	}

	FILE * stream = fopen(filename, "r+b");

	if (stream != NULL)
	{
		bytes_counter = GetFileSize(filename);
		while (1)
		{
			if (bytes_counter > MAX_FRAME_SIZE)
			{
				frame.header = (bFirstPacket ? INITIAL_DATA : DATA);
				byte_count = MAX_FRAME_SIZE;
			}
			else
			{
				byte_count = bytes_counter;
				bFinished = true;
				frame.header = FINAL_DATA;
			}

			bytes_counter -= MAX_FRAME_SIZE;

			bytes_read = fread(frame.buffer, sizeof(char), byte_count, stream);
			bytes_read_total += bytes_read;
			frame.buffer_length = byte_count;
			frame.snwseq = sequence_number;

			nTries = 0;
			do {
				nTries++;
				if (Send(sock, nullptr,&frame,nullptr) != sizeof(frame))
					return false;
				packetsSent++;
				if (nTries == 1)
					packetsSentNeeded++;
				bytes_sent += sizeof(frame);

				cout << "Sender: sent frame " << sequence_number << endl;
				fout << "Sender: sent frame " << sequence_number << endl;

				if (bFinished && (nTries > MAX_RETRIES))
				{
					bMaxAttempts = true;
					break;
				}

			} while (Receive(sock, nullptr,nullptr, &ack) != INCOMING_PACKET || ack.number != sequence_number);

			if (bMaxAttempts)
			{
				cout << "Sender: did not receive ACK " << sequence_number << " after " << MAX_RETRIES << " tries. Transfer finished." << endl;
				fout << "Sender: did not receive ACK " << sequence_number << " after " << MAX_RETRIES << " tries. Transfer finished." << endl;
			}
			else
			{
				cout << "Sender: received ACK " << ack.number << endl;
				fout << "Sender: received ACK " << ack.number << endl;
			}

			bFirstPacket = false;

			sequence_number = (sequence_number == 0 ? 1 : 0);

			if (bFinished)
				break;
		}

		fclose(stream);
		cout << "Sender: file transfer complete" << endl;
		cout << "Sender: number of packets sent: " << packetsSent << endl;
		cout << "Sender: number of packets sent (needed): " << packetsSentNeeded << endl;
		cout << "Sender: number of bytes sent: " << bytes_sent << endl;
		cout << "Sender: number of bytes read: " << bytes_read_total << endl << endl;

		fout << "Sender: file transfer complete" << endl;
		fout << "Sender: number of packets sent: " << packetsSent << endl;
		fout << "Sender: number of packets sent (needed): " << packetsSentNeeded << endl;
		fout << "Sender: number of bytes sent: " << bytes_sent << endl;
		fout << "Sender: number of bytes read: " << bytes_read_total << endl << endl;
		
		return true;
	}
	else
	{
		cout << "Sender: problem opening the file." << endl;
		fout << "Sender: problem opening the file." << endl;
		return false;
	}
}

bool ReceiveFile(int sock, char * filename, char * receiving_hostname, int server_number)
{
	MessageFrame frame;
	Acknowledgment ack; ack.packet_type = FRAME_ACK;
	long byte_count = 0;
	int packetsSent = 0, packetsSentNeeded = 0;
	int bytes_received = 0, bytes_written = 0, bytes_written_total = 0;
	int sequence_number = server_number % 2;

	fout << "Receiver started on host " << receiving_hostname << endl;

	FILE * stream = fopen(filename, "w+b");

	if (stream != NULL)
	{
		while (1)
		{
			while (Receive(sock, &frame,nullptr,nullptr) != INCOMING_PACKET) { ; }

			bytes_received += sizeof(frame);

			if (frame.packet_type == HANDSHAKE)
			{
				cout << "Receiver: received handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
				fout << "Receiver: received handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
			}
			else if (frame.packet_type == FRAME)
			{
				cout << "Receiver: received frame " << (int)frame.snwseq << endl;
				fout << "Receiver: received frame " << (int)frame.snwseq << endl;

				if ((int)frame.snwseq != sequence_number)
				{
					ack.number = (int)frame.snwseq;
					if (Send(sock, nullptr,nullptr,&ack) != sizeof(ack))
						return false;
					cout << "Receiver: sent ACK " << ack.number << " again" << endl;
					fout << "Receiver: sent ACK " << ack.number << " again" << endl;
					packetsSent++;
				}
				else
				{
					ack.number = (int)frame.snwseq;
					if (Send(sock, nullptr,nullptr,&ack) != sizeof(ack))
						return false;
					cout << "Receiver: sent ack " << ack.number << endl;
					fout << "Receiver: sent ack " << ack.number << endl;
					packetsSent++;
					packetsSentNeeded++;

					byte_count = frame.buffer_length;
					bytes_written = fwrite(frame.buffer, sizeof(char), byte_count, stream);
					bytes_written_total += bytes_written;

					sequence_number = (sequence_number == 0 ? 1 : 0);

					if (frame.header == FINAL_DATA)
						break;
				}
			}
		}

		fclose(stream);
		cout << "Receiver: file transfer complete" << endl;
		cout << "Receiver: number of packets sent: " << packetsSent << endl;
		cout << "Receiver: number of packets sent (needed): " << packetsSentNeeded << endl;
		cout << "Receiver: number of bytes received: " << bytes_received << endl;
		cout << "Receiver: number of bytes written: " << bytes_written_total << endl << endl;

		fout << "Receiver: file transfer complete" << endl;
		fout << "Receiver: number of packets sent: " << packetsSent << endl;
		fout << "Receiver: number of packets sent (needed): " << packetsSentNeeded << endl;
		fout << "Receiver: number of bytes received: " << bytes_received << endl;
		fout << "Receiver: number of bytes written: " << bytes_written_total << endl << endl;

		return true;
	}
	else
	{
		cout << "Receiver: problem opening the file." << endl;
		fout << "Receiver: problem opening the file." << endl;
		return false;
	}
}

void setHandshake(Direction direction)
{
	switch (direction)
	{
	case GET:
		cout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests GET file: \"" << handshake.filename << "\"" << endl;
		fout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests GET file: \"" << handshake.filename << "\"" << endl;
		if (FileExists(handshake.filename))
			handshake.type = ACK_CLIENT_NUM;
		else
		{
			handshake.type = FILE_NOT_EXIST;
			cout << "Server: requested file does not exist." << endl;
			fout << "Server: requested file does not exist." << endl;
		}
		break;

	case PUT:
		cout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests PUT file: \"" << handshake.filename << "\"" << endl;
		fout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests PUT file: \"" << handshake.filename << "\"" << endl;
		handshake.type = ACK_CLIENT_NUM;
		break;

	case LIST:
		cout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests GET file: \"" << handshake.filename << "\"" << endl;
		fout << "Server: user \"" << handshake.username << "\" on host \"" << handshake.hostname << "\" requests GET file: \"" << handshake.filename << "\"" << endl;
		handshake.type = ACK_CLIENT_NUM;
		break;

	default:
		handshake.type = INVALID;
		cout << "Server: invalid request." << endl;
		fout << "Server: invalid request." << endl;
		break;
	}
}

void waitForHandshake()
{
	do {
		if (Send(sock, &handshake,nullptr,nullptr) != sizeof(handshake))
			err_sys("Error in sending packet.");

		cout << "Server: sent handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
		fout << "Server: sent handshake C" << handshake.client_number << " S" << handshake.server_number << endl;

	} while (Receive(sock, nullptr, &handshake, nullptr) != INCOMING_PACKET || handshake.type != ACK_SERVER_NUM);
}

void HandshakeFactory(Handshake handshake)
{
	switch (handshake.direction)
	{
	case GET:
		if (!SendFile(sock, handshake.filename, server_name, handshake.client_number, false))
			err_sys("An error occurred while sending the file.");
		break;

	case LIST:
		strcpy(handshake.filename, "List/list.txt");
		if (!SendFile(sock, handshake.filename, server_name, handshake.client_number, true))
			err_sys("An error occurred while sending the file.");
		break;

	case PUT:
		if (!ReceiveFile(sock, handshake.filename, server_name, handshake.server_number))
			err_sys("An error occurred while receiving the file.");
		break;

	default:
		break;
	}
}

void sockConnection()
{
	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		err_sys("socket() failed");

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(PORT1);
	sa_in_size = sizeof(sa_in);

	if (bind(sock, (LPSOCKADDR)&sa, sizeof(sa)) < 0)
		err_sys("Socket binding error");
}

void handshakeType()
{

	if (handshake.type != ACK_CLIENT_NUM)
	{
		if (Send(sock, &handshake, nullptr,nullptr) != sizeof(handshake))
			err_sys("Error in sending packet.");

		cout << "Server: sent error message to client. " << endl;
		fout << "Server: sent error message to client. " << endl;
	}
	else if (handshake.type == ACK_CLIENT_NUM)
	{
		srand((unsigned)time(NULL));
		random = rand() % 256;
		handshake.server_number = random;

		waitForHandshake();

		cout << "Server: received handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
		fout << "Server: received handshake C" << handshake.client_number << " S" << handshake.server_number << endl;

		if (handshake.type == ACK_SERVER_NUM)
		{
			HandshakeFactory(handshake);
		}
		else
		{
			cout << "Handshake error!" << endl;
			fout << "Handshake error!" << endl;
		}
	}
}

unsigned long ResolveName(char name[])
{
	struct hostent *host;
	if ((host = gethostbyname(name)) == NULL)
		err_sys("gethostbyname() failed");
	return *((unsigned long *)host->h_addr_list[0]);
}

int main(void)
{
	timeouts.tv_sec = 0;
	timeouts.tv_usec = 300000;

	fout.open("server_log.txt");
	
	if (WSAStartup(0x0202, &wsadata) != 0)
	{
		WSACleanup();
		err_sys("Error in starting WSAStartup()\n");
	}

	if (gethostname(server_name, HOSTNAME_LENGTH) != 0)
		err_sys("Server gethostname() error.");

	printf("Server started on host [%s]\n", server_name);
	printf("Awaiting request for file transfer...\n", server_name);
	printf("\n");

	sockConnection();

	while (true)
	{
		while (Receive(sock, nullptr, &handshake, nullptr) != INCOMING_PACKET || handshake.type != CLIENT_REQ) { ; }

		cout << "Server: received handshake C" << handshake.client_number << endl;
		fout << "Server: received handshake C" << handshake.client_number << endl;

		setHandshake(handshake.direction);

		handshakeType();
	}

	fout.close();
	WSACleanup();

	return 0;
}
