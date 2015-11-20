#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

#define SERV_ADDR   "00:1E:0A:00:02:E1"

int read_from_server (int sock, char *buffer, size_t maxSize) {
    int bytes_read = read (sock, buffer, maxSize-1);

    if (bytes_read <= 0) {
        fprintf (stderr, "Server unexpectedly closed connection...\n");
        exit (EXIT_FAILURE);
    }

    buffer[bytes_read] = '\0';
    return bytes_read;
}

int main(int argc, char **argv) {
    struct sockaddr_rc addr = { 0 };
    int s, status;

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
        char string[100];

        // Wait for START message
        read_from_server (s, string, 5);
        printf ("%s\n", string);

        // Send hello message
        write(s,"Hello from EV3", 14);

        // Get hello message
        read_from_server (s, string, 58);
        printf ("%s\n", string);

#ifdef BY_SSH
        while (1){
           scanf("%s", string);
           write(s, string, 20);
        }
#endif

    } else {
        fprintf (stderr, "Failed to connect to server...\n");
        exit (EXIT_FAILURE);
    }

    close(s);
    return 0;
}
