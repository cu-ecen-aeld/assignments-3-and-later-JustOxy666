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
#include <pthread.h>

/* ------------------------------------------------------------------------------- */
/* typedef */

typedef signed char        S8;
typedef unsigned char      U8;
typedef signed short       S16;
typedef unsigned short     U16;
typedef signed long        S32;
typedef unsigned long      U32;
typedef signed long long   S64;
typedef unsigned long long U64;

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
int listen_fd;
Boolean is_daemon = FALSE;
char *client_ip;

/* ------------------------------------------------------------------------------- */
/* PPRIVATE FUNCTIONS PROTOTYPES */
void signalHandler(int);
void setup(void);
void parse_args(int, char**);
void teardown(void);
Boolean allocateMemory(U8 **buffer, U16 datablock_size);
void printClientIpAddress(Boolean open_connection,struct sockaddr_in* client_addr);

int acceptConnection(struct sockaddr_in* client_addr, int listen_fd);
void readClientDataToFile(int configured_fd);
void sendDataBackToClient(int configured_fd);
/* ------------------------------------------------------------------------------- */

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
    
    printClientIpAddress(FALSE, NULL);
    freeaddrinfo(servinfo);
}

Boolean allocateMemory(U8 **buffer, U16 datablock_size)
{
    Boolean result = TRUE;
    *buffer = (U8*)calloc(datablock_size, sizeof(char));
    if (*buffer == NULL)
    {
        printf("allocateMemory(): calloc returned NULL!\n");
        result = FALSE;
    }

    return result;
}

void printClientIpAddress(Boolean open_connection,struct sockaddr_in* client_addr)
{
    if (open_connection == TRUE)
    {
        client_ip = inet_ntoa(client_addr->sin_addr);
        printf("Accepted connection from %s\n", client_ip);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
    }
    else
    {
        if (client_ip != NULL)
        {
            printf("Closed connection from %s\n", client_ip);
            syslog(LOG_INFO, "Closed connection from %s", client_ip);
        }
    }
}

int acceptConnection(struct sockaddr_in* client_addr, int listen_fd)
{
    int configured_fd;
    socklen_t client_addr_size;

    client_addr_size = sizeof(*client_addr);
    memset(client_addr, 0, client_addr_size);
    if ((configured_fd = accept(listen_fd, (struct sockaddr*)client_addr, &client_addr_size)) == FAIL)
    {
        printf("accept: %s\n", strerror(errno));
        exit(-1);
    }

    return configured_fd;
}

void readClientDataToFile(int configured_fd)
{
    FILE* fstream;
    U8* buf = NULL;
    Boolean data_block_end = FALSE;
    int internal_cntr = 0;

    while(data_block_end == FALSE)
    {
        allocateMemory(&buf, DATA_BLOCK_SIZE);

        for (internal_cntr = 0; internal_cntr < DATA_BLOCK_SIZE; internal_cntr++)
        {
            if (recv(configured_fd, &buf[internal_cntr], sizeof(char), 0) == FAIL)
            {
                printf("recv_read: %s\n", strerror(errno));
                printf("configured_fd: %d\n", configured_fd);
                exit(-1);
            }

            if (strcmp((char*)&buf[internal_cntr], "\n") == PASS)
            {
                /* \n is two symbols */
                internal_cntr++; 
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
        if (fwrite(buf, sizeof(char), internal_cntr, fstream) == 0)
        {
            printf("ERROR: Nothing is written to %s", SOCKET_DATA_FILEPATH);
        }

        /* Free memory */
        free(buf);
        fflush(fstream);
        fclose(fstream);
    }
}

void sendDataBackToClient(int configured_fd)
{
    FILE* fstream;
    U8* buf = NULL;
    Boolean data_block_end = FALSE;
    int internal_cntr, counter = 0; /* total bytes counter */

    while (data_block_end == FALSE)
    {
        /* Send data back to client */
        allocateMemory(&buf, DATA_BLOCK_SIZE);

        if ((fstream = fopen((char*)SOCKET_DATA_FILEPATH, "r+")) == NULL)
        {
            printf("fstream: %s\n", strerror(errno));
            exit(-1);
        }

        /* Put file pointer before next block */
        fseek(fstream, counter, SEEK_SET);
        for (internal_cntr = 0; internal_cntr < DATA_BLOCK_SIZE; internal_cntr++)
        {
            counter++;
            if (fread(&buf[internal_cntr], sizeof(char), sizeof(char), fstream) == 0)
            {
                data_block_end = TRUE;
                break;
            }
        }

        if (send(configured_fd, buf, internal_cntr, 0) == FAIL)
        {
            printf("recv_send: %s\n", strerror(errno));
            exit(-1);
        }

        free(buf);
        fflush(fstream);
        fclose(fstream);
    }
}


int main(int argc, char** argv)
{
    int conf_fd;
    struct sockaddr_in client_addr;

    /* Handle argument(s) */
    parse_args(argc, argv);
    freeaddrinfo(servinfo);

    /* Setup things and get socket file descriptor */
    setup();

    while(1)
    {
        /* Accept incoming connection */
        conf_fd = acceptConnection(&client_addr, listen_fd);
        printClientIpAddress(TRUE, &client_addr);
        readClientDataToFile(conf_fd);
        sendDataBackToClient(conf_fd);

        /* Data block complete, close current connection */
        close(conf_fd);
        printClientIpAddress(FALSE, NULL);
    }

    teardown();
    return 0;
}
