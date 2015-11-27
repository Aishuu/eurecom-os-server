#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define MAXMSG              58
#define MAXNAMESIZE         31
#define MAXTEAM             5

#define CONNECT_DELAY       15

#define RBT_MISS            0
#define RBT_NXT             1
#define RBT_EV3             2

#define GAM_CONNECTING      0
#define GAM_RUNNING         1

#define MSG_ACTION          0
#define MSG_ACK             1
#define MSG_LEAD            2
#define MSG_START           3
#define MSG_STOP            4
#define MSG_WAIT            5
#define MSG_CUSTOM          6

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
#define RESET "\033[0m"

const char * const playerColors [] = {
    KGRN,
    KYEL,
    KBLU,
    KMAG,
    KCYN
};

#define COL(i) playerColors[i % 5]

#define MAILBOX_SIZE        5

struct NXTMailbox {
    unsigned int messagesReady;
    unsigned int nextMessage;
    char mailbox [MAILBOX_SIZE][MAXMSG+1];
    unsigned int msgSize [MAILBOX_SIZE];
};

struct team {
    int sock;
    char name[MAXNAMESIZE+1];
    bdaddr_t address;
    char robotType;
    char active;
    char connected;
    unsigned char rank;
    struct NXTMailbox *mailbox;
};

FILE *out = NULL;
static volatile int running = 1;

void intHandler (int signo) {
    running = 0;
}

void replyToNXT (struct team *t, char mailbox) {
    if (t->robotType != RBT_NXT)
        return;

    // Note that we only have one mailbox on the server side
    // and we delete messages every time a MESSAGEREAD command
    // is received.

    char buf[66] = {0};
    buf[0] = 64;            // return packages have a fixed size
    buf[1] = 0x00;          // ...
    buf[2] = 0x02;          // This is a reply message
    buf[3] = 0x13;          // to a MESSAGEREAD command

    if (t->mailbox->messagesReady == 0) {
        // Mailbox is empty
        buf[4] = 0x40;      // Specified mailbox queue is empty
#ifdef DEBUG
        printf ("[DEBUG] Mailbox is empty !\n");
#endif
    } else {
        // Messages are pending
        buf[4] = 0x00;      // Everything is OK
        buf[5] = mailbox;
        unsigned int msgSize = t->mailbox->msgSize[t->mailbox->nextMessage];

        // Message must include the terminating null byte
        buf[6] = (char) ((msgSize+1) & 0xFF);

        memcpy (buf+7, t->mailbox->mailbox[t->mailbox->nextMessage], msgSize);

#ifdef DEBUG
        printf ("[DEBUG] Serving message of size %d '", msgSize);
        int i;
        for (i=7; i<7+msgSize; i++) {
            printf ("%02X", (unsigned char) buf[i]);
            if ((i-7) % 4 == 3)
                printf (" ");
        }
        printf ("' to team %s [%d]\n", t->name, t->mailbox->nextMessage);
#endif

        t->mailbox->nextMessage = (t->mailbox->nextMessage + 1) % MAILBOX_SIZE;
        t->mailbox->messagesReady --;
    }

    write (t->sock, buf, 66);
}

int read_from_client (struct team *t, char *buffer, int maxSize) {
    int nbytes;

    if (t->robotType == RBT_NXT) {
        unsigned char nxtBuf[2] = {0};
        read (t->sock, nxtBuf, 2);
        unsigned int s = (nxtBuf[1] << 8) | nxtBuf[0];
        maxSize = s < maxSize ? s : maxSize;
    }

    nbytes = read (t->sock, buffer, maxSize);
    if (nbytes <= 0)
        // Read error or End-of-file
        return -1;

    if (t->robotType == RBT_NXT)
        // Delete last '\0'
        nbytes --;


#ifdef DEBUG
    printf ("[DEBUG] received %d bytes : ", nbytes);
    int i;
    for (i=0; i<nbytes; i++)
        printf ("0x%02X ", (unsigned char) buffer[i]);
    printf ("\n");
#endif


    if (t->robotType == RBT_NXT && maxSize >= 5 && buffer[0] == 0x00 && buffer[1] == 0x13) {
        // Got a MESSAGEREAD command
        replyToNXT (t, buffer[3]);
        return 0;
    }

    return nbytes;
}

void write_to_client (struct team *t, const char *buf, size_t size) {
    if (t->robotType == RBT_EV3)
        write (t->sock, buf, size);
    else if (t->robotType == RBT_NXT) {
        int msgInd = (t->mailbox->messagesReady + t->mailbox->nextMessage) % MAILBOX_SIZE;
        memcpy (t->mailbox->mailbox[msgInd], buf, size);
        t->mailbox->msgSize[msgInd] = size;

#ifdef DEBUG
        printf ("[DEBUG] Adding message of size %ld to mailbox of team %s [%d]\n", size, t->name, msgInd);
#endif

        if (t->mailbox->messagesReady == MAILBOX_SIZE)
            t->mailbox->nextMessage = (t->mailbox->nextMessage + 1) % MAILBOX_SIZE;
        else
            t->mailbox->messagesReady ++;
    }
}

int load_teams_file (const char *filename, struct team *teams, int maxTeams) {
    printf ("Reading team file...                                                         ");
    FILE * teamsFile = fopen (filename, "r");
    if (teamsFile == NULL) {
        printf ("[" KRED "KO" RESET "]\n");
        fprintf (stderr, "Could not open file %s.\n", filename);
        exit (EXIT_FAILURE);
    }

    char buf[21+MAXNAMESIZE];
    memset (buf, 0, sizeof (buf));

    int i,j;
    char comment = 0, lineFollow = 0;
    for (i = 0, j = 0; fgets (buf, 21+MAXNAMESIZE, teamsFile); memset (buf, 0, sizeof (buf)), j++) {
        if (buf[0] == '\n') {
            lineFollow = 0;
            continue;
        }

        comment = (buf[0] == '#' || (comment && lineFollow));

        size_t l = strlen (buf);
        if (buf[l-1] == '\n') {
            lineFollow = 0;
            buf[--l] = '\0';
        } else
            lineFollow = 1;

        if (comment)
            continue;

        if (l < 21) {
            printf ("[" KRED "KO" RESET "]\n");
            fprintf (stderr, "Error in team file %s (l.%d)\n", filename, j);
            exit (EXIT_FAILURE);
        }

        if (buf[0] - '0' == RBT_NXT)
            teams[i].robotType = RBT_NXT;
        else if (buf[0] - '0' == RBT_EV3)
            teams[i].robotType = RBT_EV3;
        else {
            printf ("[" KRED "KO" RESET "]\n");
            fprintf (stderr, "Error in team file %s (l.%d)\n", filename, j);
            exit (EXIT_FAILURE);
        }

        buf[19] = '\0';
        str2ba (buf+2, &teams[i].address);

        strcpy (teams[i].name, buf+20);

        if (teams[i].robotType == RBT_NXT)
            teams[i].mailbox = (struct NXTMailbox *) malloc (sizeof (struct NXTMailbox));
        else
            teams[i].mailbox = NULL;

        i++;
    }

    fclose (teamsFile);

    printf ("[" KGRN "OK" RESET "]\n");

    return i;
}

void debug (const char *color, const char *fmt, ...) {
    va_list argp;

    va_start (argp, fmt);

    if (out != NULL) {
        vfprintf (out, fmt, argp);
        va_end (argp);
        va_start (argp, fmt);
    }

    printf ("%s", color);
    vprintf (fmt, argp);
    printf (RESET);
    fflush (stdout);

    va_end (argp);
}

void parseMessage (struct team *teams, int nbTeams, int sendingTeam, const unsigned char *buf, int nbbytes) {
    debug (KNRM, "[");
    debug (COL(sendingTeam),"%" STR(MAXNAMESIZE) "s", teams[sendingTeam].name);
    debug (KNRM, "] ");

    if (nbbytes < 5) {
        debug (KRED, "*** header is too short (%d bytes) ***\n", nbbytes);
        return;
    }

    if (buf[2] != sendingTeam) {
        debug (KRED, "*** mediocre spoofing attempt detected ***\n");
        return;
    }

    if (buf[3] >= nbTeams || !teams[buf[3]].active) {
        debug (KRED, "*** unkown or inactive receiver (%d) ***\n", buf[3]);
        return;
    }

    if (!teams[buf[3]].connected) {
        debug (KRED, "*** Team ");
        debug (COL(buf[3]), "%s", teams[buf[3]].name);
        debug (KRED, " is not connected. Message discarded ***\n");
        return;
    }

    uint16_t id = *((uint16_t *) buf);

    switch (buf[4]) {
        case MSG_ACTION:
            // ACTION
            if (nbbytes < 10) {
                debug (KRED, "*** ACTION message is too short (%d bytes) ***\n", nbbytes);
                return;
            }

            if (teams[buf[3]].rank != teams[sendingTeam].rank+1) {
                debug (KRED, "*** Can't send ACTION message to team ");
                debug (COL(buf[3]), "%s", teams[buf[3]].name);
                debug (KRED, " ***\n");
                return;
            }

            uint16_t angle = *((uint16_t *) &buf[5]);
            uint16_t speed = *((uint16_t *) &buf[8]);

            debug (KNRM, "id=%d dest=", id);
            debug (COL(buf[3]), "%s\n", teams[buf[3]].name);
            debug (KNRM, "         ACTION   angle=%d dist=%d speed=%d\n", angle, buf[7], speed);

            write_to_client (&teams[buf[3]], (char *) buf, 10);

            break;
        case MSG_ACK:
            // ACK
            if (nbbytes < 8) {
                debug (KRED, "*** ACK message is too short (%d bytes) ***\n", nbbytes);
                return;
            }

            if (teams[buf[3]].rank != teams[sendingTeam].rank+1 && teams[buf[3]].rank != teams[sendingTeam].rank-1) {
                debug (KRED, "*** Can't send ACK message to team ");
                debug (COL(buf[3]), "%s", teams[buf[3]].name);
                debug (KRED, " ***\n");
                return;
            }

            uint16_t idAck = *((uint16_t *) &buf[5]);

            debug (KNRM, "id=%d dest=", id);
            debug (COL(buf[3]), "%s\n", teams[buf[3]].name);
            debug (KNRM, "          ACK      idAck=%d status=%d\n", idAck, buf[7]);

            write_to_client (&teams[buf[3]], (char *) buf, 8);

            break;
        case MSG_LEAD:
            // LEAD
            if (teams[buf[3]].rank != teams[sendingTeam].rank+1) {
                debug (KRED, "*** Can't send LEAD message to team ");
                debug (COL(buf[3]), "%s", teams[buf[3]].name);
                debug (KRED, " ***\n");
                return;
            }

            debug (KNRM, "id=%d dest=", id);
            debug (COL(buf[3]), "%s\n", teams[buf[3]].name);
            debug (KNRM, "          LEAD\n");

            write_to_client (&teams[buf[3]], (char *) buf, 5);

            break;
        case MSG_START:
            // START
            debug (KRED, "*** Tried to START the game ***\n");
            return;
        case MSG_STOP:
            // STOP
            debug (KRED, "*** Tried to STOP the game ***\n");
            return;
        case MSG_WAIT:
            // WAIT
            if (nbbytes < 6) {
                debug (KRED, "*** WAIT message is too short (%d bytes) ***\n", nbbytes);
                return;
            }

            if (teams[buf[3]].rank != teams[sendingTeam].rank-1) {
                debug (KRED, "*** Can't send WAIT message to team ");
                debug (COL(buf[3]), "%s", teams[buf[3]].name);
                debug (KRED, " ***\n");
                return;
            }

            debug (KNRM, "id=%d dest=", id);
            debug (COL(buf[3]), "%s\n", teams[buf[3]].name);
            debug (KNRM, "          WAIT     seconds=%d\n", buf[5]);

            write_to_client (&teams[buf[3]], (char *) buf, 6);

            break;
        case MSG_CUSTOM:
            // CUSTOM
            if (teams[buf[3]].rank != teams[sendingTeam].rank+1 && teams[buf[3]].rank != teams[sendingTeam].rank-1) {
                debug (KRED, "*** Can't send CUSTOM message to team ");
                debug (COL(buf[3]), "%s", teams[buf[3]].name);
                debug (KRED, " ***\n");
                return;
            }

            debug (KNRM, "id=%d dest=", id);
            debug (COL(buf[3]), "%s\n", teams[buf[3]].name);
            debug (KNRM, "          CUSTOM   content=");
            int i;
            for (i=5; i<nbbytes; i++) {
                debug (KNRM, "%02X", (unsigned char) buf[i]);
                if ((i-5) % 4 == 3)
                    debug (KNRM, " ");
            }
            debug (KNRM, "\n");

            write_to_client (&teams[buf[3]], (char *) buf, nbbytes);

            break;
        default:
            debug (KRED, "*** unkown message type 0x%02X ***\n", (unsigned char) buf[4]);
            return;
    }
}

int main (int argc, char **argv) {

    struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };
    int serverSock, fdmax, i;
    socklen_t opt = sizeof(rem_addr);
    fd_set read_fd_set;

    char buf[MAXMSG+1] = { 0 };

    struct team teams [MAXTEAM];
    memset(teams, 0, MAXTEAM * sizeof(struct team));

    if (argc < 2 || argc > 3) {
        fprintf (stderr, "Usage: %s teamFile [ouputFile]\n", argv[0]);
        exit (EXIT_FAILURE);
    }

    if (argc == 3) {
        out = fopen (argv[2], "w");
        
        if (out == NULL) {
            fprintf (stderr, "Could not open file %s.\n", argv[2]);
            exit (EXIT_FAILURE);
        }
    }

    srand(time(NULL));

    printf ("\n\n");
    printf (KRED    "                                           )  (      (                            \n");
    printf (        "                                        ( /(  )\\ )   )\\ )                         \n");
    printf (        "       (     (  (     (           )     )\\())(()/(  (()/(   (  (    )     (  (    \n");
    printf (        "       )\\   ))\\ )(   ))\\ (  (    (     ((_)\\  /(_))  /(_)) ))\\ )(  /((   ))\\ )(   \n");
    printf (        "      ((_) /((_|()\\ /((_))\\ )\\   )\\  '   ((_)(_))   (_))  /((_|()\\(_))\\ /((_|()\\  \n");
    printf (RESET   "      | __" KRED "(_))( ((_|_)) ((_|(_)_((_))   " RESET "/ _ \\/ __|  / __|" KRED "(_))  ((_))((_|_))  ((_) \n");
    printf (RESET   "      | _|| || | '_/ -_) _/ _ \\ '  \\" KRED "() " RESET "| (_) \\__ \\  \\__ \\/ -_)| '_\\ V // -_)| '_| \n");
    printf (        "      |___|\\_,_|_| \\___\\__\\___/_|_|_|   \\___/|___/  |___/\\___||_|  \\_/ \\___||_|  \n");
    printf ("\n\n");

    // create server socket
    printf ("Creating server socket...                                                    ");
    fflush (stdout);
    serverSock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    // bind socket to port 1 of the first available local bluetooth adapter
    loc_addr.rc_family = AF_BLUETOOTH;
    loc_addr.rc_bdaddr = *BDADDR_ANY;
    loc_addr.rc_channel = (uint8_t) 1;
    bind(serverSock, (struct sockaddr *) &loc_addr, sizeof (loc_addr)); 

    // put socket into listening mode
    listen(serverSock, MAXTEAM);
    printf ("[" KGRN "OK" RESET "]\n");

    // load teams from file
    int nbTeams = load_teams_file (argv[1], teams, MAXTEAM);
    debug (KNRM, "  ... %d teams have been loaded...\n", nbTeams);

    // connect to server
    connect(teams[0].sock, (struct sockaddr *)&rem_addr, sizeof(rem_addr));

    // catch SIGINT to stop game
    if (signal (SIGINT, intHandler) == SIG_ERR) {
        fprintf (stderr, "Couldn't catch SIGINT.\n");
        exit (EXIT_FAILURE);
    }

    // start the contest
    while (running) {

        // print all teams
        printf ("   +--------------------------------------------+\n");
        printf ("   |" KRED " TEAMS " RESET "                                     |\n");
        printf ("   +--------------------------------------------+\n");
        for (i=0; i<nbTeams; i++)
            if (teams[i].robotType != RBT_MISS)
                printf ("   | %2d: %s%-" STR(MAXNAMESIZE) "s " RESET " (%3s) |\n", 
                        i,
                        COL(i),
                        teams[i].name,
                        teams[i].robotType == RBT_EV3 ? "EV3" : "NXT");
        printf ("   +--------------------------------------------+\n");

        // prompt for game composition
        printf ("Which teams are going to participate? (^D to end the contest)\n");

        char invalidInput;
        int rankCmp;

        do {
            for (i=0; i<nbTeams; i++) {
                teams[i].active = 0;
                teams[i].rank = 0;
                if (teams[i].robotType == RBT_NXT) {
                    teams[i].mailbox->messagesReady = 0;
                    teams[i].mailbox->nextMessage = 0;
                }
            }

            invalidInput = 0;

            printf ("> ");
            fflush (stdout);
            char *p = fgets (buf, MAXTEAM * 3 + 2, stdin);

            if (p == NULL)
                running = 0;
            else {
                rankCmp = 0;
                // get participating teams
                for (i=-1; *p && *p != '\n'; p++) {
                    if (*p == ' ') {
                        if (i != -1) {
                            if (i < nbTeams && teams[i].robotType != RBT_MISS && !teams[i].active) {
                                teams[i].active = 1;
                                teams[i].rank = rankCmp++;
                            } else {
                                invalidInput = 1;
                                printf ("Invalid team number: %d\n", i);
                            }
                            i = -1;
                        }
                    } else {
                        if (*p < '0' || *p > '9') {
                            invalidInput = 1;
                            printf ("Invalid input number: '%c'\n", *p);
                            break;
                        }

                        if (i == -1)
                            i = *p - '0';
                        else
                            i = i*10 + *p - '0';
                    }
                }

                if (i != -1) {
                    if (i < nbTeams && teams[i].robotType != RBT_MISS && !teams[i].active) {
                        teams[i].active = 1;
                        teams[i].rank = rankCmp++;
                    } else {
                        invalidInput = 1;
                        printf ("Invalid team number: %d\n", i);
                    }
                }

                if (rankCmp == 0)
                    invalidInput = 1;
            }
        } while (invalidInput);

        if (running) {
            int rep = 0;
            while (rep != '0') {
                printf ("Rank is:\n");
                int j;
                for (i=0; i<rankCmp; i++)
                    for (j=0; j<nbTeams; j++)
                        if (teams[j].active && teams[j].rank == i) {
                            printf ("%d: %s%s" RESET "\n", i, COL(j), teams[j].name);
                            break;
                        }
                printf ("What do you want to do?   (default: 0)\n");
                printf ("      0: keep that\n");
                printf ("      1: randomize all\n");
                printf ("      2: randomize all but leader\n");
                printf ("> ");
                fflush (stdout);
                char *p = fgets (buf, 2, stdin);
                if (p == NULL || buf[0] == '\n')
                    break;
                if (p[0] < '0' || p[0] > '2')
                    continue;

                rep = p[0];

                if (rep == '1') {
                    for (i=0; i<nbTeams; i++)
                        if (teams[i].active) {
                            char found;
                            unsigned char rnd;
                            do {
                                rnd = (unsigned char) (rand () % rankCmp);
                                found = 0;
                                for (j=0; j<i; j++)
                                    if (teams[j].active && teams[j].rank == rnd) {
                                        found = 1;
                                        break;
                                    }
                            } while (found);

                            teams[i].rank = rnd;
                        }
                } else if (rep == '2') {
                    for (i=0; i<nbTeams; i++)
                        if (teams[i].active && teams[i].rank != 0) {
                            char found;
                            unsigned char rnd;
                            do {
                                rnd = (unsigned char) (rand () % (rankCmp-1)) + 1;
                                found = 0;
                                for (j=0; j<i; j++)
                                    if (teams[j].active && teams[j].rank == rnd) {
                                        found = 1;
                                        break;
                                    }
                            } while (found);

                            teams[i].rank = rnd;
                        }
                }
            }

            debug (KRED, "Starting game with teams ");
            int j;
            for (i=0; i<rankCmp; i++)
                for (j=0; j<nbTeams; j++)
                    if (teams[j].active && teams[j].rank == i) {
                        if (i != 0)
                            debug (KRED, ", ");
                        debug (COL(j), "%s", teams[j].name);
                        break;
                    }
            debug (KRED, ".\n");
        } else
            break;

        time_t startTime = time (NULL) + CONNECT_DELAY;
        char state = GAM_CONNECTING;

        while (running) {

            fdmax = serverSock;
            FD_ZERO (&read_fd_set);
            FD_SET (serverSock, &read_fd_set);

            for (i=0; i<nbTeams; i++)
                if (teams[i].connected) {
                    FD_SET (teams[i].sock, &read_fd_set);
                    if (teams[i].sock > fdmax)
                        fdmax = teams[i].sock;
                }

            int selectRet;
            time_t now = time (NULL);

            if (now >= startTime && state == GAM_CONNECTING) {
                printf (KRED "Game starts NOW !\n" RESET);

                buf[0] = 0x00;                      // ID of start message
                buf[1] = 0x00;                      // is 0000
                buf[2] = 0xFF;                      // server ID is 0xFF
                buf[4] = MSG_START;                 // This is a START message
                buf[6] = (char) (0xFF & rankCmp);   // length of the snake

                char first = 1;
                for (i=0; i<nbTeams; i++) {
                    if (teams[i].active) {
                        if (teams[i].connected) {
                            buf[3] = (char) (0xFF & i);             // receiver
                            buf[5] = teams[i].rank;                 // rank
                            buf[7] = 0xFF;
                            buf[8] = 0xFF;
                            int j;
                            for (j=0; j<nbTeams; j++)
                                if (teams[j].active) {
                                    if (teams[j].rank == teams[i].rank - 1)
                                        buf[7] = (char) (0xFF & j); // previous
                                    if (teams[j].rank == teams[i].rank + 1)
                                        buf[8] = (char) (0xFF & j); // next
                                }

                            write_to_client (&teams[i], buf, 9);
                        } else {
                            if (first)
                                first = 0;
                            else
                                printf (KRED ", " RESET);
                            printf ("%s%s" RESET, COL(i), teams[i].name);
                        }
                    }
                }

                if (!first)
                    printf (KRED " failed to connect.\n" RESET);

                state = GAM_RUNNING;
            }
            
            if (now < startTime) {
                struct timeval tv;
                tv.tv_sec = startTime - now;
                tv.tv_usec = 0;
                selectRet = select (fdmax+1, &read_fd_set, NULL, NULL, &tv);
            } else
                selectRet = select (fdmax+1, &read_fd_set, NULL, NULL, NULL);

            if (selectRet < 0) {
                if (running) {
                    fprintf (stderr, "Error when select.\n");
                    exit (EXIT_FAILURE);
                }
            } else {

                if (FD_ISSET (serverSock, &read_fd_set)) {
                    // accept one connection
                    int client = accept(serverSock, (struct sockaddr *)&rem_addr, &opt);


                    for (i=0; i<nbTeams; i++)
                        if (memcmp (&teams[i].address, &rem_addr.rc_bdaddr, sizeof (bdaddr_t)) == 0) {
                            if (teams[i].active) {
                                teams[i].sock = client;
                                teams[i].connected = 1;
                                debug (KRED, "Team ");
                                debug (COL(i), "%s", teams[i].name);
                                debug (KRED, " is now connected.\n");
                                if (state == GAM_RUNNING) {
                                    buf[0] = 0x00;                          // ID of start message
                                    buf[1] = 0x00;                          // is 0000
                                    buf[2] = 0xFF;                          // server ID is 0xFF
                                    buf[3] = (char) (0xFF & i);             // receiver
                                    buf[4] = MSG_START;                     // This is a START message
                                    buf[5] = teams[i].rank;                 // rank
                                    buf[6] = (char) (0xFF & rankCmp);       // length of the snake
                                    buf[7] = 0xFF;
                                    buf[8] = 0xFF;
                                    int j;
                                    for (j=0; j<nbTeams; j++)
                                        if (teams[j].active) {
                                            if (teams[j].rank == teams[i].rank - 1)
                                                buf[7] = (char) (0xFF & j); // previous
                                            if (teams[j].rank == teams[i].rank + 1)
                                                buf[8] = (char) (0xFF & j); // next
                                        }
                                    write_to_client (&teams[i], buf, 9);
                                }
                            } else {
                                debug (KRED, "Team ");
                                debug (COL(i), "%s", teams[i].name);
                                debug (KRED, " tried to connect while not taking part in this game!\n");
                                close (client);
                            }

                            break;
                        }

                    if (i == nbTeams) {
                        ba2str(&rem_addr.rc_bdaddr, buf );
                        debug (KRED, "Unknown connection from address %s.\n", buf);
                        close (client);
                    }
                }

                for (i = 0; i <= nbTeams; ++i)
                    if (teams[i].connected && FD_ISSET (teams[i].sock, &read_fd_set)) {
                        memset(buf, 0, sizeof(buf));
                        int nbbytes;
                        if ((nbbytes = read_from_client (&teams[i], buf, MAXMSG)) < 0) {
                            close (teams[i].sock);
                            teams[i].connected = 0;
                            debug (KRED, "Team ");
                            debug (COL(i), "%s", teams[i].name);
                            debug (KRED, " has disconnected.\n");
                        } else if (nbbytes != 0 && state == GAM_RUNNING)
                            parseMessage (teams, nbTeams, i, (unsigned char *) buf, nbbytes);
                    }
            }
        }

        printf ("\n");
        debug (KRED, "End of this game.\n\n");
        running = 1;

        buf[0] = 0x01;                      // ID of stop message
        buf[1] = 0x00;                      // is 0001
        buf[2] = 0xFF;                      // server ID is 0xFF
        buf[4] = MSG_STOP;                  // This is a STOP message


        for (i = 0; i < nbTeams; i++) {
            if (teams[i].connected) {
                buf[3] = (char) (0xFF & i);             // receiver
                write_to_client (&teams[i], buf, 5);

                close (teams[i].sock);
                teams[i].connected = 0;
            }

            if (teams[i].active)
                teams[i].active = 0;
        }
    }

    printf ("\n");
    debug (KRED, "End of the contest.\n");

    for (i=0; i<nbTeams; i++)
        if (teams[i].robotType == RBT_NXT)
            free (teams[i].mailbox);

    if (out)
        fclose (out);

    close(serverSock);
    return 0;
}
