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

#define CONNECT_DELAY       5

#define RBT_MISS            0
#define RBT_NXT             1
#define RBT_EV3             2

#define GAM_CONNECTING      0
#define GAM_RUNNING         1

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

struct team {
    int sock;
    char name[MAXNAMESIZE+1];
    bdaddr_t address;
    char robotType;
    char active;
    char connected;
};

FILE *out = NULL;
static volatile int running = 1;

void intHandler (int signo) {
    running = 0;
}

int read_from_client (int filedes, char *buffer, int maxSize) {
    int nbytes;

    nbytes = read (filedes, buffer, maxSize-1);
    if (nbytes < 0) {
        /* Read error. */
        fprintf (stderr, KRED "Failed to read from client %d\n" RESET, filedes);
        return -1;
    } else if (nbytes == 0)
        /* End-of-file. */
        return -1;

    return nbytes;
}

void write_to_client (struct team *t, const char *buf, size_t size) {
    if (t->robotType == RBT_EV3)
        write (t->sock, buf, size);
    else if (t->robotType == RBT_NXT) {
        char *bbuf = (char *) malloc ((size+6) * sizeof (char));
        bbuf[0] = (size+4)>>8;
        bbuf[1] = (size+4)&0xFF;
        bbuf[2] = 0x24;
        bbuf[3] = 0x32;
        bbuf[4] = 0;
        bbuf[5] = size;
        memcpy (bbuf+6, buf, size);
        write (t->sock, bbuf, size+6);
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

        i++;
    }

    fclose (teamsFile);

    printf ("[" KGRN "OK" RESET "]\n");

    return i;
}

void debug (const char *color, const char *fmt, ...) {
    va_list argp;

    va_start (argp, fmt);

    if (out != NULL)
        vfprintf (out, fmt, argp);

    printf ("%s", color);
    vprintf (fmt, argp);
    printf (RESET);
    fflush (stdout);

    va_end (argp);
}

int main(int argc, char **argv) {

    struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };
    int serverSock, fdmax, i;
    socklen_t opt = sizeof(rem_addr);
    fd_set read_fd_set;

    char buf[MAXMSG] = { 0 };

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
        do {
            invalidInput = 0;

            printf ("> ");
            fflush (stdout);
            char *p = fgets (buf, MAXTEAM * 3 + 2, stdin);

            if (p == NULL)
                running = 0;
            else {
                // get participating teams
                for (i=-1; *p && *p != '\n'; p++) {
                    if (*p == ' ') {
                        if (i != -1) {
                            if (i < nbTeams && teams[i].robotType != RBT_MISS && !teams[i].active)
                                teams[i].active = 1;
                            else {
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
                    if (i < nbTeams && teams[i].robotType != RBT_MISS && !teams[i].active)
                        teams[i].active = 1;
                    else {
                        invalidInput = 1;
                        printf ("Invalid team number: %d\n", i);
                    }
                }
            }
        } while (invalidInput);

        if (running) {
            debug (KRED, "Starting game with teams ");
            for (i=0; i<nbTeams; i++)
                if (teams[i].active) {
                    if (i != 0)
                        debug (KRED, ", ");
                    debug (COL(i), "%s", teams[i].name);
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

                strcpy (buf, "START");

                char first = 1;
                for (i=0; i<nbTeams; i++) {
                    if (teams[i].active) {
                        if (teams[i].connected) {
                            write_to_client (&teams[i], buf, 5);
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
                        if ((nbbytes = read_from_client (teams[i].sock, buf, MAXMSG)) < 0) {
                            close (teams[i].sock);
                            teams[i].connected = 0;
                            debug (KRED, "Team ");
                            debug (COL(i), "%s", teams[i].name);
                            debug (KRED, " has disconnected.\n");
                        } else {
                            if (state == GAM_RUNNING) {
                                debug (KNRM, "[");
                                debug (COL(i), "%s", teams[i].name);
                                debug (KNRM, "] %s\n", buf);
                                int j;
                                for (j = 0; j < nbTeams; ++j)
                                    if (i != j && teams[j].connected)
                                        write_to_client (&teams[j], buf, nbbytes);
                            }
                        }
                    }

                for (i = 0; i < nbTeams; i++)
                    if (teams[i].active && !teams[i].connected && teams[i].robotType == RBT_NXT) {
                        int client = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
                        if (client > 0) {
                            rem_addr.rc_family = AF_BLUETOOTH;
                            rem_addr.rc_channel = (uint8_t) 1;
                            rem_addr.rc_bdaddr = teams[i].address;

                            if (connect(teams[0].sock, (struct sockaddr *)&rem_addr, sizeof(rem_addr)) == 0) {
                                teams[i].sock = client;
                                teams[i].connected = 1;
                                debug (KRED, "Team ");
                                debug (COL(i), "%s", teams[i].name);
                                debug (KRED, " is now connected.\n");
                            }
                        }
                    }
            }
        }

        debug (KRED, "\nEnd of this game.\n\n");
        running = 1;

        for (i = 0; i < nbTeams; i++) {
            if (teams[i].connected) {
                teams[i].connected = 0;
                close (teams[i].sock);
            }

            if (teams[i].active)
                teams[i].active = 0;
        }
    }

    debug (KRED, "\nEnd of the contest.\n");

    if (out)
        fclose (out);

    close(serverSock);
    return 0;
}
