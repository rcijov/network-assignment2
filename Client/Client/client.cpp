#include "client.h"
using namespace std;

bool FileExists(char * filename)
{
	std::ifstream infile(filename);
	return infile.good();
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

ReceiveResult Receive(int sock, MessageFrame * ptr_message_frame, Handshake * ptr_handshake, Acknowledgment * ack)
{
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(sock, &readfds);
	int bytes_recvd;
	int output = select(1, &readfds, NULL, NULL, &timeouts);
	switch (output)
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

bool SendFile(int sock, char * filename, char * sending_hostname, int server_number)
{
	MessageFrame frame; frame.packet_type = FRAME;
	Acknowledgment ack; ack.number = -1;
	long bytes_counter = 0, byte_count = 0;
	int bytes_sent = 0, bytes_read = 0, bytes_read_total = 0;
	int packetsSent = 0, packetsSentNeeded = 0;
	bool bFirstPacket = true, bFinished = false;
	int sequence_number = server_number % 2;
	int nTries; bool bMaxAttempts = false;

	fout << "Sender started on host " << sending_hostname << endl;

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
				if (SendFrame(sock, &frame) != sizeof(frame))
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

			} while (Receive(sock, nullptr, nullptr, &ack) != INCOMING_PACKET || ack.number != sequence_number);

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

bool ReceiveFile(int sock, char * filename, char * receiving_hostname, int client_number, bool list)
{
	MessageFrame frame;
	Acknowledgment ack; ack.packet_type = FRAME_ACK;
	long byte_count = 0;
	int packetsSent = 0, packetsSentNeeded = 0;
	int bytes_received = 0, bytes_written = 0, bytes_written_total = 0;
	int sequence_number = client_number % 2;

	fout << "Receiver started on host " << receiving_hostname << endl;

	FILE * stream = fopen(filename, "w+b");

	if (stream != NULL)
	{
		while (1)
		{
			while (Receive(sock, &frame, nullptr, nullptr) != INCOMING_PACKET) { ; }

			bytes_received += sizeof(frame);

			if (frame.packet_type == HANDSHAKE)
			{
				cout << "Receiver: received handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
				fout << "Receiver: received handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
				if (SendRequest(sock, &handshake, &sa_in) != sizeof(handshake))
					err_sys("Error in sending packet.");
				cout << "Receiver: sent handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
				fout << "Receiver: sent handshake C" << handshake.client_number << " S" << handshake.server_number << endl; 
			}
			else if (frame.packet_type == FRAME)
			{
				cout << "Receiver: received frame " << (int)frame.snwseq << endl;
				fout << "Receiver: received frame " << (int)frame.snwseq << endl;

				if ((int)frame.snwseq != sequence_number)
				{
					ack.number = (int)frame.snwseq;
					if (SendFileAck(sock, &ack) != sizeof(ack))
						return false;
					cout << "Receiver: sent ACK " << ack.number << " again" << endl;
					packetsSent++;
					fout << "Receiver: sent ACK " << ack.number << " again" << endl;
				}
				else
				{
					ack.number = (int)frame.snwseq;
					if (SendFileAck(sock, &ack) != sizeof(ack))
						return false;
					cout << "Receiver: sent ACK " << ack.number << endl;
					fout << "Receiver: sent ACK " << ack.number << endl;
					packetsSent++;
					packetsSentNeeded++;

					byte_count = frame.buffer_length;
					bytes_written = fwrite(frame.buffer, sizeof(char), byte_count, stream);
					bytes_written_total += bytes_written;

					if (list)
					{
						int i;
						for (i = 0; i < frame.buffer_length; i++)
						{
							cout << frame.buffer[i];
						}
					}
					cout << endl;

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

void socketConnection()
{
	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		err_sys("socket() failed");

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(PORT1);

	if (bind(sock, (LPSOCKADDR)&sa, sizeof(sa)) < 0)
		err_sys("Socket binding error");
}

bool menu()
{
	cout << "Enter command     : "; cin >> direction;
	if (strncmp(direction, "quit", 4) == 0)
	{
		return false;
	}

	if (!strncmp(direction, "list", 4) == 0)
	{
		cout << "Enter file name   : "; cin >> filename; cout << endl;
	}

	cout << "Enter router host : "; cin >> remotehost;

	strcpy(handshake.hostname, hostname);
	strcpy(handshake.username, username);
	strcpy(handshake.filename, filename);

	return true;
}

void initiateConnection()
{
	struct hostent *rp;
	rp = gethostbyname(remotehost);
	memset(&sa_in, 0, sizeof(sa_in));
	memcpy(&sa_in.sin_addr, rp->h_addr, rp->h_length);
	sa_in.sin_family = rp->h_addrtype;
	sa_in.sin_port = htons(PORT2);
	sa_in_size = sizeof(sa_in);

	handshake.client_number = random;
	handshake.type = CLIENT_REQ;
	handshake.packet_type = HANDSHAKE;
}

void setHandshake(char* direction)
{

	(strcmp(direction, "get") == 0) ? handshake.direction = GET :
		((strcmp(direction, "put") == 0) ? handshake.direction = PUT :
			((strcmp(direction, "list") == 0) ? handshake.direction = LIST : 
				err_sys("Invalid direction. Use \"get\" or \"put\".")));
	
	if ((!FileExists(handshake.filename)) && (handshake.direction == PUT))
	{
			err_sys("File does not exist on client side.");
	}
}

void HandshakeFactory(Direction direction)
{
	switch (direction)
	{
	case GET:
		if (!ReceiveFile(sock, handshake.filename, hostname, handshake.client_number, false))
			err_sys("An error occurred while receiving the file.");
		break;
	case PUT:
		if (!SendFile(sock, handshake.filename, hostname, handshake.server_number))
			err_sys("An error occurred while sending the file.");
		break;
	case LIST:
		strcpy(handshake.filename, "List/list.txt");
		if (!ReceiveFile(sock, handshake.filename, hostname, handshake.client_number, true))
			err_sys("An error occurred while receiving the file.");
		break;
	default:
		break;
	}
}

void setHandshakeType(HandshakeType handshaketype)
{
	switch (handshaketype)
	{
	case FILE_NOT_EXIST:
		cout << "File does not exist!" << endl;
		fout << "Client: requested file does not exist!" << endl;
		break;

	case INVALID:
		cout << "Invalid request." << endl;
		fout << "Client: invalid request." << endl;
		break;

	case ACK_CLIENT_NUM:
		cout << "Client: received handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
		fout << "Client: received handshake C" << handshake.client_number << " S" << handshake.server_number << endl;

		handshake.type = ACK_SERVER_NUM;
		int sequence_number = handshake.server_number % 2;
		if (SendRequest(sock, &handshake, &sa_in) != sizeof(handshake))
			err_sys("Error in sending packet.");

		cout << "Client: sent handshake C" << handshake.client_number << " S" << handshake.server_number << endl;
		fout << "Client: sent handshake C" << handshake.client_number << " S" << handshake.server_number << endl;

		HandshakeFactory(handshake.direction);
		break;
	}
}

void waitForHandshake()
{
	do
	{
		if (SendRequest(sock, &handshake, &sa_in) != sizeof(handshake))
			err_sys("Error in sending packet.");

		cout << "Client: sent handshake C" << handshake.client_number << endl;
		fout << "Client: sent handshake C" << handshake.client_number << endl;

	} while (Receive(sock, nullptr, &handshake, nullptr) != INCOMING_PACKET);
}

unsigned long ResolveName(char name[])
{
	struct hostent *host;
	if ((host = gethostbyname(name)) == NULL)
		err_sys("gethostbyname() failed");
	return *((unsigned long *)host->h_addr_list[0]);
}

int main(int argc, char* argv[])
{
	// set timeout
	timeouts.tv_sec = 0;
	timeouts.tv_usec = 300000;

	// open log file
	fout.open("client_log.txt");
	
	char server[INPUT_LENGTH];
	unsigned long filename_length = (unsigned long)FILENAME_LENGTH;
	bool bContinue = true;

	if (WSAStartup(0x0202, &wsadata) != 0)
	{
		WSACleanup();
		err_sys("Error in starting WSAStartup()\n");
	}

	if (!GetUserName((LPWSTR)username, &filename_length))
		err_sys("Cannot get the user name");

	if (gethostname(hostname, (int)HOSTNAME_LENGTH) != 0)
		err_sys("Cannot get the host name");

	printf("User [%s] started client on host [%s]\n", username, hostname);
	printf("To quit, type \"quit\" as server name.\n\n");

	while (true)
	{

		socketConnection();

		srand((unsigned)time(NULL));
		random = rand() % 256;

		menu();

		if (bContinue)
		{
			initiateConnection();

			setHandshake(direction);

			waitForHandshake();

			setHandshakeType(handshake.type);

		}
		cout << "Closing client socket." << endl;
		fout << "Closing client socket." << endl;
		closesocket(sock);
	}

	fout.close();
	WSACleanup();
	return 0;
}
