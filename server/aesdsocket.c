#include <sys/types.h>
#include <sys/socket.h>
#include <syslog.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h> /* gethints() */
#include <arpa/inet.h> /* get IP */

/* ------------------------------------------------------------------------------- */
#define _XOPEN_SOURCE           (700)
#define SOCKET_DOMAIN           (PF_INET)
#define SOCKET_TYPE             (SOCK_STREAM)
#define DAEMON_ARG              ("-d")
#define SOCKET_DATA_FILEPATH    ("/var/tmp/aesdsocketdata")
#define SOCKET_PORT             ("9000")
#define SOCKET_INC_CONNECT_MAX  (50U)
#define DATA_BLOCK_SIZE         (512U)

/* ------------------------------------------------------------------------------- */
/* PRIVATE TYPES */

typedef enum
{
    FALSE = 0U,
    TRUE = 1U
} Boolean;

typedef enum
{
    FAIL = -1,
    PASS = 0
} Result;

/* GLOBAL VARIABLES */

struct addrinfo *servinfo;
char *client_ip;
int listen_fd;
Boolean is_daemon = FALSE;

/* ------------------------------------------------------------------------------- */
/* PPRIVATE FUNCTIONS PROTOTYPES */
void signalHandler(int);
void setup(void);
void parse_args(int, char**);
void teardown(void);

void signalHandler(int signal_number)
{
    printf("caught signal %d\n", signal_number);
    syslog(LOG_INFO, "Caught signal, exiting");
    teardown();
}

void parse_args(int argc, char** argv)
{
    if (argc == 2)
    {
        if (strcmp(argv[1], DAEMON_ARG) == 0)
        {
            printf("Running in daemon mode\n");
            is_daemon = TRUE;
        }
        else
        {
            printf("Invalid argument!\n");
            exit(0);
        }
    }
    else if (argc > 2)
    {
        printf("Too many arguments!\n");
        exit(-1);
    }
    else
    {
        // all good
    }
}

void setup(void)
/**
 * @brief Setups syslog, signal handling and socket
 * 
 * @returns File descriptor of socket
 */
{
    struct sigaction signal_action;
    struct addrinfo hints;
    pid_t pid;

    /* Init syslog */
    openlog(NULL, LOG_NDELAY, LOG_USER);
    
    /* Setup SIGINT & SIGTERM callbacks */
    memset(&signal_action, 0, sizeof(signal_action));
    signal_action.sa_handler = signalHandler;
    sigaction(SIGTERM, &signal_action, NULL);
    sigaction(SIGINT, &signal_action, NULL);

    /* Open socket */
    if ((listen_fd = socket(SOCKET_DOMAIN, SOCKET_TYPE, 0)) == FAIL)
    {
        printf("Open socket error: %s\n", strerror(errno));
        exit(-1);
    }

    /* Setup to allow reusing socket and port*/
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == FAIL)
    {
        printf("setsockopt ADDR: %s\n", strerror(errno));
        exit(-1);
    }

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int)) == FAIL)
    {
        printf("setsockopt PORT: %s\n", strerror(errno));
        exit(-1);
    }

    /* Setup addrinfo struct */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCKET_TYPE;
    if (getaddrinfo(NULL, SOCKET_PORT, &hints, &servinfo) != PASS)
    {
        printf("getaddrinfo: %s\n", strerror(errno));
        exit(-1);
    }

    /* Bind addrinfo struct to socket listen_fd */
    if (bind(listen_fd, servinfo->ai_addr, servinfo->ai_addrlen) == FAIL)
    {
        printf("bind: %s\n", strerror(errno));
        exit(-1);
    }

    if (is_daemon == TRUE)
    {
        if ((pid = fork()) == FAIL)
        {
            printf("fork: %s\n", strerror(errno));
            exit(-1);
        }

        if (pid > 0)
        {
            exit(0);
        }

        if (setsid() < 0)
        {
            exit(-1);
        }
    }

    /* Listen for incoming connections */
    if (listen(listen_fd, SOCKET_INC_CONNECT_MAX) == FAIL)
    {
        printf("listen: %s\n", strerror(errno));
        exit(-1);
    }
}

void teardown(void)
{
    close(listen_fd);
    if (remove(SOCKET_DATA_FILEPATH) == FAIL)
    {
        printf("remove: %s\n", strerror(errno));
    }

    if (client_ip != NULL)
    {
        printf("Closed connection from %s\n", client_ip);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    freeaddrinfo(servinfo);
}


int main(int argc, char** argv)
{
    int conf_fd, sock_read_res, total_cntr, int_cntr;
    FILE* fstream;
    struct sockaddr_in client_addr;
    socklen_t client_addr_size;
    Boolean data_block_end = FALSE;

    char *buf;

    /* Handle argument(s) */
    parse_args(argc, argv);
    freeaddrinfo(servinfo);
    

    /* Setup things and get socket file descriptor */
    setup();
    client_addr_size = sizeof(client_addr);

    while(1)
    {
        /* Accept incoming connection */
        memset(&client_addr, 0, client_addr_size);
        if ((conf_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_size)) == FAIL)
        {
            printf("accept: %s\n", strerror(errno));
            exit(-1);
        }

        client_ip = inet_ntoa(client_addr.sin_addr);
        printf("Accepted connection from %s\n", client_ip);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        data_block_end = FALSE;        
        while(data_block_end == FALSE)
        {
            buf = (char*)calloc(DATA_BLOCK_SIZE, sizeof(char));
            if (buf == NULL)
            {
                printf("Calloc returned NULL!\n");
            }

            for (int_cntr = 0; int_cntr < DATA_BLOCK_SIZE; int_cntr++)
            {
                if ((sock_read_res = recv(conf_fd, &buf[int_cntr], sizeof(char), 0)) == FAIL)
                {
                    printf("recv: %s\n", strerror(errno));
                    exit(-1);
                }

                if (strcmp(&buf[int_cntr], "\n") == PASS)
                {
                    /* \n is two symbols */
                    int_cntr++; 
                    data_block_end = TRUE;
                    break;
                }
            }

            if ((fstream = fopen((char*)SOCKET_DATA_FILEPATH, "a")) == NULL)
            {
                printf("fstream: %s\n", strerror(errno));
                exit(-1);
            }

            /* Copy bytes from buf to file stream */
            if (fwrite(buf, sizeof(char), int_cntr, fstream) == 0)
            {
                printf("ERROR: Nothing is written to %s", SOCKET_DATA_FILEPATH);
            }

            /* Free memory */
            free(buf);
            fflush(fstream);
            fclose(fstream);
            
        }

        total_cntr = 0; /* total bytes total_cntr */
        data_block_end = FALSE;        
        while (data_block_end == FALSE)
        {
            /* Send data back to client */
            buf = (char*)calloc(DATA_BLOCK_SIZE, sizeof(char));
            if (buf == NULL)
            {
                printf("Calloc returned NULL!\n");
            }

            if ((fstream = fopen((char*)SOCKET_DATA_FILEPATH, "r+")) == NULL)
            {
                printf("fstream: %s\n", strerror(errno));
                exit(-1);
            }

            /* Put file pointer before next block */
            fseek(fstream, total_cntr, SEEK_SET);

            for (int_cntr = 0; int_cntr < DATA_BLOCK_SIZE; int_cntr++)
            {
                total_cntr++;
                if (fread(&buf[int_cntr], sizeof(char), sizeof(char), fstream) == 0)
                {
                    data_block_end = TRUE;
                    break;
                }
            }

            if ((sock_read_res = send(conf_fd, buf, int_cntr, 0)) == FAIL)
            {
                printf("recv: %s\n", strerror(errno));
                exit(-1);
            }

            free(buf);
            fflush(fstream);
            fclose(fstream);
        }

        /* Data block complete, close current connection */
        close(conf_fd);

        if (client_ip != NULL)
        {
            printf("Closed connection from %s\n", client_ip);
            syslog(LOG_INFO, "Closed connection from %s", client_ip);
        }
    }

    teardown();
    return 0;
}
