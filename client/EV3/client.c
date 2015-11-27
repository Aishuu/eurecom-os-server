#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

#define SERV_ADDR   "00:1E:0A:00:02:E1"     // Whatever the address of the server is

#define BY_SSH

#define MSG_ACTION  0
#define MSG_ACK     1
#define MSG_LEAD    2
#define MSG_START   3
#define MSG_STOP    4
#define MSG_WAIT    5
#define MSG_CUSTOM  6

#define TEAM_ID     1

void debug (const char *fmt, ...) {
    va_list argp;

#ifdef BY_SSH
    va_start (argp, fmt);

    vprintf (fmt, argp);

    va_end (argp);
#endif

}

unsigned char rank = 0;
unsigned char length = 0;
unsigned char previous = 0xFF;
unsigned char next = 0xFF;

int s;

uint16_t msgId = 0;

int read_from_server (int sock, char *buffer, size_t maxSize) {
    int bytes_read = read (sock, buffer, maxSize);

    if (bytes_read <= 0) {
        fprintf (stderr, "Server unexpectedly closed connection...\n");
        close (s);
        exit (EXIT_FAILURE);
    }

    printf ("[DEBUG] received %d bytes\n", bytes_read);

    return bytes_read;
}

void leader () {
    char string[58];

    printf ("I'm the leader...\n");

    // Send ACTION message
    *((uint16_t *) string) = msgId++;
    string[2] = TEAM_ID;
    string[3] = next;
    string[4] = MSG_ACTION;

    string[5] = 90;          // angle 90 degree
    string[6] = 0x00;

    string[7] = 10;          // dist 10cm

    string[8] = 20;          // speed 20mm/s
    string[9] = 0x00;

    write(s, string, 10);
}

void follower () {
    char string[58];
    printf ("I'm a follower...\n");

    // Get message
    read_from_server (s, string, 58);
    char type = string[4];

    switch (type) {
        case MSG_ACTION:
            // Send ACK message
            string[3] = string[2];      // reply to sender
            string[2] = TEAM_ID;
            string[4] = MSG_ACK;

            string[5] = string[0];      // ID of the message to acknowledge
            string[6] = string[1];

            *((uint16_t *) string) = msgId++;

            string[7] = 0x00;           // status OK
            
            write(s, string, 8);

            break;
        case MSG_STOP:
            return;
        case MSG_LEAD:
            leader ();
            break;
        default:
            printf ("Ignoring message %d\n", type);
    }
}


int main(int argc, char **argv) {
    struct sockaddr_rc addr = { 0 };
    int status;

    // allocate a socket
    s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    // set the connection parameters (who to connect to)
    addr.rc_family = AF_BLUETOOTH;
    addr.rc_channel = (uint8_t) 1;
    str2ba (SERV_ADDR, &addr.rc_bdaddr);

    // connect to server
    status = connect(s, (struct sockaddr *)&addr, sizeof(addr));

    // if connected
    if( status == 0 ) {
        char string[58];

        // Wait for START message
        read_from_server (s, string, 9);
        if (string[4] == MSG_START) {
            printf ("Received start message!\n");
            rank = (unsigned char) string[5];
            length = (unsigned char) string[6];
            previous = (unsigned char) string[7];
            next = (unsigned char) string[8];
        }

        if (rank == 0)
            leader ();
        else
            follower ();

        close (s);

        sleep (5);

    } else {
        fprintf (stderr, "Failed to connect to server...\n");
        sleep (2);
        exit (EXIT_FAILURE);
    }

    close(s);
    return 0;
}
