#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdint.h>
#include "packet.h"
#include "serial.h"
#include "serialize.h"
#include "constants.h"
#include <string.h>

#define PORT_NAME "/dev/ttyACM0"
#define BAUD_RATE B9600

int exitFlag = 0;
sem_t _xmitSema;

void handleError(TResult error)
{
	switch (error)
	{
	case PACKET_BAD:
		printf("ERROR: Bad Magic Number\n");
		break;

	case PACKET_CHECKSUM_BAD:
		printf("ERROR: Bad checksum\n");
		break;

	default:
		printf("ERROR: UNKNOWN ERROR\n");
	}
}

void handleStatus(TPacket *packet)
{
	printf("\n ------- ALEX STATUS REPORT ------- \n\n");
	printf("Left Forward Ticks:\t\t%d\n", packet->params[0]);
	printf("Right Forward Ticks:\t\t%d\n", packet->params[1]);
	printf("Left Reverse Ticks:\t\t%d\n", packet->params[2]);
	printf("Right Reverse Ticks:\t\t%d\n", packet->params[3]);
	printf("Left Forward Ticks Turns:\t%d\n", packet->params[4]);
	printf("Right Forward Ticks Turns:\t%d\n", packet->params[5]);
	printf("Left Reverse Ticks Turns:\t%d\n", packet->params[6]);
	printf("Right Reverse Ticks Turns:\t%d\n", packet->params[7]);
	printf("Forward Distance:\t\t%d\n", packet->params[8]);
	printf("Reverse Distance:\t\t%d\n", packet->params[9]);
	printf("\n---------------------------------------\n\n");
}

void handleResponse(TPacket *packet)
{
	// The response code is stored in command
	switch (packet->command)
	{
	case RESP_OK:
		printf("Command OK\n");
		break;

	case RESP_STATUS:
		handleStatus(packet);
		break;

	default:
		printf("Arduino is confused\n");
	}
}

void handleErrorResponse(TPacket *packet)
{
	// The error code is returned in command
	switch (packet->command)
	{
	case RESP_BAD_PACKET:
		printf("Arduino received bad magic number\n");
		break;

	case RESP_BAD_CHECKSUM:
		printf("Arduino received bad checksum\n");
		break;

	case RESP_BAD_COMMAND:
		printf("Arduino received bad command\n");
		break;

	case RESP_BAD_RESPONSE:
		printf("Arduino received unexpected response\n");
		break;

	default:
		printf("Arduino reports a weird error\n");
	}
}

void handleMessage(TPacket *packet)
{
	printf("Message from Alex: %s\n", packet->data);
}

void handlePacket(TPacket *packet)
{
	switch (packet->packetType)
	{
	case PACKET_TYPE_COMMAND:
		// Only we send command packets, so ignore
		break;

	case PACKET_TYPE_RESPONSE:
		handleResponse(packet);
		break;

	case PACKET_TYPE_ERROR:
		handleErrorResponse(packet);
		break;

	case PACKET_TYPE_MESSAGE:
		handleMessage(packet);
		break;
	}
}

void sendPacket(TPacket *packet)
{
	char buffer[PACKET_SIZE];
	int len = serialize(buffer, packet, sizeof(TPacket));

	serialWrite(buffer, len);
}

void *receiveThread(void *p)
{
	char buffer[PACKET_SIZE];
	int len;
	TPacket packet;
	TResult result;
	int counter = 0;

	while (1)
	{
		len = serialRead(buffer);
		counter += len;
		if (len > 0)
		{
			result = deserialize(buffer, len, &packet);

			if (result == PACKET_OK)
			{
				counter = 0;
				handlePacket(&packet);
			}
			else if (result != PACKET_INCOMPLETE)
			{
				printf("PACKET ERROR\n");
				handleError(result);
			}
		}
	}
}

void flushInput()
{
	char c;

	while ((c = getchar()) != '\n' && c != EOF)
		;
}

void getParams(TPacket *commandPacket)
{
	printf("Enter distance/angle in cm/degrees (e.g. 50) and power in %% (e.g. 75) separated by space.\n");
	printf("E.g. 50 75 means go at 50 cm at 75%% power for forward/backward, or 50 degrees left or right turn at 75%%  power\n");
	scanf("%d %d", &commandPacket->params[0], &commandPacket->params[1]);
	flushInput();
}

void sendCommand(char command, int *params)
{
	TPacket commandPacket;

	commandPacket.packetType = PACKET_TYPE_COMMAND;

	switch (command)
	{
	case 'f':
	case 'F':
		// getParams(&commandPacket);
		commandPacket.command = COMMAND_FORWARD;
		commandPacket.params[0] = params[0];
		commandPacket.params[1] = params[1];
		sendPacket(&commandPacket);
		break;

	case 'b':
	case 'B':
		// getParams(&commandPacket);
		commandPacket.command = COMMAND_REVERSE;
		commandPacket.params[0] = params[0];
		commandPacket.params[1] = params[1];
		sendPacket(&commandPacket);
		break;

	case 'l':
	case 'L':
		// getParams(&commandPacket);
		commandPacket.command = COMMAND_TURN_LEFT;
		commandPacket.params[0] = params[0];
		commandPacket.params[1] = params[1];
		sendPacket(&commandPacket);
		break;

	case 'r':
	case 'R':
		// getParams(&commandPacket);
		commandPacket.command = COMMAND_TURN_RIGHT;
		commandPacket.params[0] = params[0];
		commandPacket.params[1] = params[1];
		sendPacket(&commandPacket);
		break;

	case 's':
	case 'S':
		commandPacket.command = COMMAND_STOP;
		sendPacket(&commandPacket);
		break;

	case 'c':
	case 'C':
		commandPacket.command = COMMAND_CLEAR_STATS;
		commandPacket.params[0] = 0;
		sendPacket(&commandPacket);
		break;

	case 'g':
	case 'G':
		commandPacket.command = COMMAND_GET_STATS;
		sendPacket(&commandPacket);
		break;

	case 'q':
	case 'Q':
		exitFlag = 1;
		break;
	/**
	case 'd':
	case 'D':
		commandPacket.command = COMMAND_GET_COLOUR;
		sendPacket(&commandPacket);
		break;
	*/
	default:
		printf("Bad command\n");
	}
}

int main()
{
	// Connect to the Arduino
	startSerial(PORT_NAME, BAUD_RATE, 8, 'N', 1, 3);

	// Sleep for two seconds
	printf("WAITING TWO SECONDS FOR ARDUINO TO REBOOT\n");
	sleep(2);
	printf("DONE\n");

	// Spawn receiver thread
	pthread_t recv;

	pthread_create(&recv, NULL, receiveThread, NULL);

	// Send a hello packet
	TPacket helloPacket;

	helloPacket.packetType = PACKET_TYPE_HELLO;
	sendPacket(&helloPacket);

	while (!exitFlag)
	{
		// char ch;
		printf("Command (f=forward, b=reverse, l=turn left, r=turn right, s=stop, c=clear stats, g=get stats q=exit)\n");
		// scanf("%c", &ch);
		// scan a string instead, and split the string by " "s to get the command and parameters

		char command[100];

		// scanf("%s", command);
		// do the above but dont end on a space, only end on newlines
		fgets(command, 100, stdin);
		char ch = command[0];

		int params[2] = {0, 0};

		// if command needs parameters, parse them
		if (ch == 'f' || ch == 'b' || ch == 'l' || ch == 'r')
		{
			int i = 0;
			char *p = command + 2;
			for (*p;
				 *p != '\0' && i < 2;
				 p++)
			{
				// printf("{%c} params[%d] = %d\n", *p, i, params[i]);
				if (*p == ' ')
				{
					i++;
					continue;
				}
				params[i] = params[i] * 10 + *p - '0';
			}
			// printf("Command: %c, Distance: %d, Power: %d\n", ch, params[0], params[1]);

			sendCommand(ch, params);
		}
		else
		{

			// Purge extraneous characters from input stream
			flushInput();

			sendCommand(ch, params);
		}
	}

	printf("Closing connection to Arduino.\n");
	endSerial();
}
