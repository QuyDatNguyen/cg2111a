
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdint.h>
#include "Alex/packet.h"
#include "Alex/constants.h"
#include "serial.h"
#include "serialize.h"

// #define PORT_NAME "/dev/ttyACM1"
#define PORT_NAME_WITHOUT_NUM "/dev/ttyACM"
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
    printf("\033[1m\033[36m🛜 Status Report\033[m\n");
    printf(" Left Forward Ticks:\t\t%d\n", packet->params[0]);
    printf(" Right Forward Ticks:\t\t%d\n", packet->params[1]);
    printf(" Left Reverse Ticks:\t\t%d\n", packet->params[2]);
    printf(" Right Reverse Ticks:\t\t%d\n", packet->params[3]);
    printf(" Left Forward Ticks Turns:\t%d\n", packet->params[4]);
    printf(" Right Forward Ticks Turns:\t%d\n", packet->params[5]);
    printf(" Left Reverse Ticks Turns:\t%d\n", packet->params[6]);
    printf(" Right Reverse Ticks Turns:\t%d\n", packet->params[7]);
    printf(" Forward Distance:\t\t%d\n", packet->params[8]);
    printf(" Reverse Distance:\t\t%d\n", packet->params[9]);
    printf(" Target Ticks:\t\t\t%d\n", packet->params[10]);
    printf(" Delta Ticks:\t\t\t%d\n", packet->params[11]);
    printf(" Delta Dist:\t\t\t%d\n\n", packet->params[12]);
}

int map(int value, int fromLow, int fromHigh, int toLow, int toHigh)
{
    return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
}

void handleColor(TPacket *packet)
{
    // printf("\n ------- COLOR REPORT ------- \n\n");
    int rgb[3];
    rgb[0] = map(packet->params[0], 8, 80, 255, 0);
    rgb[1] = map(packet->params[1], 8, 80, 255, 0);
    rgb[2] = map(packet->params[2], 8, 80, 255, 0);

    printf("\033[1m\033[32m🎨 Detected Color:\033[m\n");
    printf(" \033[38;2;%d;%d;%dm██████\033[m %3d,%3d,%3d [raw: %3d, %3d, %3d]\n",
           rgb[0], rgb[1], rgb[2], rgb[0], rgb[1], rgb[2],
           packet->params[0], packet->params[1], packet->params[2]);
}

void handleDistance(TPacket *packet)
{
    // draw a little diagram with box characters to show the distance, min being 0 cm and max being 20 cm
    // the diagram can be 20 characters long

    int distance = packet->params[0];
    int distanceInCm = distance / 10;

    printf("\n      \\    /\\\n       )  ( ')\n      (  /  )\njgs    \\(__)|");
    for (int i = 0; i < 20; i++)
    {
        if (i < distanceInCm)
        {
            printf("─");
        }
        else if (i == distanceInCm)
        {
            printf("┤");
        }
        else
        {
            printf(" ");
        }
    }
    if (distanceInCm >= 20)
    {
        printf("⋯");
    }
    printf("\n            0    5   10   15   20 cms\n");
    double cms = distance / 10.0;
    // printf("\033[1m\033[31m📏 Detected distance:\033[m %d mm\n", packet->params[0]);
    // show in cm with 1 decimal pt instead
    printf("\033[1m\033[31m📏 Detected distance:\033[m %.1f cm\n", cms);
}

void handleResponse(TPacket *packet)
{
    // The response code is stored in command
    switch (packet->command)
    {
    case RESP_OK:
        // printf("Command OK\n");
        // green checkmark
        printf("\033[32m✔︎ \033[m");
        printf("\033[30mCommand OK\n\033[m");
        break;

    case RESP_STATUS:
        handleStatus(packet);
        break;
    case RESP_COLOR:
        handleColor(packet);
        break;
    case RESP_TOO_CLOSE:
        printf("\033[1m\033[33m⚠️ Notice\n\033[m");
        printf("Stopped because Alex is getting too close!\n");
        break;
    case RESP_IR_DISTANCE:
        handleDistance(packet);
        break;

    default:
        printf("? Arduino is confused\n");
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

void getParams(TPacket *commandPacket, bool angle)
{
    if (angle)
    {
        printf("\033[1m\033[35mℹ️ Angle / Power?\033[m ");
        printf("\033[30meg. 90 75\033[m ");
    }
    else
    {
        printf("\033[1m\033[35mℹ️ Distance / Power?\033[m ");
        printf("\033[30meg. 50 75\033[m ");
    }
    scanf("%d %d", &commandPacket->params[0], &commandPacket->params[1]);
    flushInput();
}

void sendCommand(char command)
{
    TPacket commandPacket;

    commandPacket.packetType = PACKET_TYPE_COMMAND;

    switch (command)
    {
    case 'f':
    case 'F':
        getParams(&commandPacket, false);
        commandPacket.command = COMMAND_FORWARD;
        sendPacket(&commandPacket);
        break;

    case 'b':
    case 'B':
        getParams(&commandPacket, false);
        commandPacket.command = COMMAND_REVERSE;
        sendPacket(&commandPacket);
        break;

    case 'l':
    case 'L':
        getParams(&commandPacket, true);
        commandPacket.command = COMMAND_TURN_LEFT;
        sendPacket(&commandPacket);
        break;

    case 'r':
    case 'R':
        getParams(&commandPacket, true);
        commandPacket.command = COMMAND_TURN_RIGHT;
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
    case 'd':
    case 'D':
        commandPacket.command = COMMAND_GET_COLOUR;
        sendPacket(&commandPacket);
        break;
    case 'u':
    case 'U':
        commandPacket.command = COMMAND_GET_IR;
        sendPacket(&commandPacket);
        break;
    default:
        printf("Bad command\n");
    }
}

void showControls()
{
    printf("\n");
    // "Controls" in yellow text
    // "Welcome" in bold text
    printf("\033[1m\033[33m> Controls\033[m\n");
    printf("   F      🛑 [S]top Robot    🎨 [D]etect Color \n");
    printf(" L   R    📊 [G]et Stats     📏 [U]ltrasonic Measurement \n");
    printf("   B      🗑️  [C]Clear Stats  🟥 [Q]uit \n");
    printf("\n");
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("[!] No port provided! Run the command as following, or modify the C code to remove this part\n\t./alex-pi <port number>\n\teg.\n\t./alex-pi 1", argv[0]);
        return 1;
    }

    char *port = argv[1];
    // char *port = "0"; // For testing
    char PORT_NAME[20];
    sprintf(PORT_NAME, "%s%s", PORT_NAME_WITHOUT_NUM, port);

    printf("4. Connecting to %s... ", PORT_NAME);

    // Connect to the Arduino
    startSerial(PORT_NAME, BAUD_RATE, 8, 'N', 1, 5);

    // Sleep for two seconds
    printf("   Connected! (hopefully)\n");
    printf("5. Waiting 2 seconds for Arduino to reboot... ");
    sleep(2);
    printf("aaaand done!\n");

    // Spawn receiver thread
    pthread_t recv;

    pthread_create(&recv, NULL, receiveThread, NULL);

    // Send a hello packet
    TPacket helloPacket;

    helloPacket.packetType = PACKET_TYPE_HELLO;
    sendPacket(&helloPacket);

    while (!exitFlag)
    {
        char ch;
        showControls();
        // scanf("%c", &ch);
        // above but without requiring an enter
        // something like getch() but cross platform
        ch = getchar();

        // Purge extraneous characters from input stream
        flushInput();

        sendCommand(ch);
    }

    printf("Closing connection to Arduino.\n");
    endSerial();
}