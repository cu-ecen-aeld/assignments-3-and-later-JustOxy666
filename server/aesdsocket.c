// #define _GNU_SOURCE
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
#include <time.h>

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

#define  S_TO_MS(a)         (a * 1000U)
#define NS_TO_MS(a)         ((U16)(a / 1000000U))
#define US_TO_MS(a)         (a * 1000U)
#define TIMESPEC_TO_S(a,b)  ((U16)(a + (U16)(b / 1000000000U)))

/* ------------------------------------------------------------------------------- */
#define _XOPEN_SOURCE               (700)
#define SOCKET_DOMAIN               (PF_INET)
#define SOCKET_TYPE                 (SOCK_STREAM)
#define DAEMON_ARG                  ("-d")
#define SOCKET_DATA_FILEPATH        ("/var/tmp/aesdsocketdata")
#define SOCKET_PORT                 ("9000")
#define SOCKET_INC_CONNECT_MAX      (50U)
#define DATA_BLOCK_SIZE             (512U)
#define NUM_THREADS                 (128)
#define TIMESTAMP_PRINT_DELAY_MS    (10U * 1000U)

/* ------------------------------------------------------------------------------- */
/* PRIVATE TYPES */
typedef enum

{
    FALSE,
    TRUE
} Boolean;

typedef enum
{
    FAIL = -1,
    PASS = 0
} Result;

typedef struct
{
    int conf_fd;
    struct sockaddr_in *client_addr;
    char* client_ip;
} task_params;


/* GLOBAL VARIABLES */

struct addrinfo *servinfo;
int listen_fd;
Boolean is_daemon = FALSE;
pthread_t threads[NUM_THREADS];
task_params t_params[NUM_THREADS];
pthread_mutex_t file_mutex;
Boolean timestamp_thread_exit;

/**
 * Flag that indicates whether any client started sending to socket
 * Controls when timestamping thread starts outputting to file
 */
Boolean client_started_sending; 

/* ------------------------------------------------------------------------------- */
/* PPRIVATE FUNCTIONS PROTOTYPES */
void signalHandler(int);
void setup(void);
void parse_args(int, char**);
void teardown(void);
Boolean allocateMemory(U8 **buffer, U16 datablock_size);
void printClientIpAddress(Boolean open_connection, task_params* t_arg);

int acceptConnection(struct sockaddr_in* client_addr, int listen_fd);
Boolean readClientDataToFile(int configured_fd);
Boolean sendDataBackToClient(int configured_fd);
Boolean aesdsocket_task(void*);
void timestamp_task(void*);
U16 getTimespecDiffMs(struct timespec t1, struct timespec t2);
/* ------------------------------------------------------------------------------- */

void signalHandler(int signal_number)
{
#ifdef DEBUG_ON
    printf("caught signal %d\n", signal_number);
#endif /* DEBUG_ON */
    syslog(LOG_INFO, "Caught signal, exiting");
    teardown();
}

void parse_args(int argc, char** argv)
{
    if (argc == 2)
    {
        if (strcmp(argv[1], DAEMON_ARG) == 0)
        {
#ifdef DEBUG_ON           
            printf("Running in daemon mode\n");
#endif /* DEBUG_ON */
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

    client_started_sending = FALSE;

    /* Init mutex */
    pthread_mutex_init(&file_mutex, NULL);

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
    timestamp_thread_exit = TRUE;
    for(int i = 0; i < NUM_THREADS; i++)
    {
        close(t_params[i].conf_fd);
        printClientIpAddress(FALSE, &t_params[i]);
        pthread_join(threads[i], NULL);
    }

    if (remove(SOCKET_DATA_FILEPATH) == FAIL)
    {
        printf("remove: %s\n", strerror(errno));
    }
    
    freeaddrinfo(servinfo);
    pthread_mutex_destroy(&file_mutex);
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

void printClientIpAddress(Boolean open_connection, task_params* t_arg)
{
    struct sockaddr_in *sock_addr = t_arg->client_addr;
    if (open_connection == TRUE)
    {
        t_arg->client_ip = inet_ntoa(sock_addr->sin_addr);
#ifdef DEBUG_ON
        printf("Accepted connection from %s\n", t_arg->client_ip);
#endif /* DEBUG_ON */
        syslog(LOG_INFO, "Accepted connection from %s", t_arg->client_ip);
    }
    else
    {
        if ((t_arg->client_ip) != NULL)
        {
#ifdef DEBUG_ON
            printf("Closed connection from %s\n", t_arg->client_ip);
#endif /* DEBUG_ON */
            syslog(LOG_INFO, "Closed connection from %s", t_arg->client_ip);
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

Boolean readClientDataToFile(int configured_fd)
{
    Boolean result = TRUE;
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
                data_block_end = TRUE;
                result = FALSE;
            }

            if (client_started_sending == FALSE)
            {
#ifdef DEBUG_ON
                printf("readClientDataToFile(): Started timestamping now\n");
#endif /* DEBUG_ON */
                client_started_sending = TRUE;
            }

            if (strcmp((char*)&buf[internal_cntr], "\n") == PASS)
            {
                /* \n is two symbols */
                internal_cntr++; 
                data_block_end = TRUE;
                break;
            }
        }

        /* ------------- ENTER CRITICAL SECTION -------------- */
        pthread_mutex_lock(&file_mutex);

        if ((fstream = fopen((char*)SOCKET_DATA_FILEPATH, "a")) == NULL)
        {
            printf("fstream: %s\n", strerror(errno));
            data_block_end = TRUE;
            result = FALSE;
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
        pthread_mutex_unlock(&file_mutex);
        /* ------------- EXIT CRITICAL SECTION -------------- */
    }

    return result;
}

Boolean sendDataBackToClient(int configured_fd)
{
    Boolean result = TRUE;
    FILE* fstream;
    U8* buf = NULL;
    Boolean data_block_end = FALSE;
    int internal_cntr, counter = 0; /* total bytes counter */

    while (data_block_end == FALSE)
    {
        /* Send data back to client */
        allocateMemory(&buf, DATA_BLOCK_SIZE);

        /* ------------- ENTER CRITICAL SECTION -------------- */
        pthread_mutex_lock(&file_mutex);
        if ((fstream = fopen((char*)SOCKET_DATA_FILEPATH, "r+")) == NULL)
        {
            printf("fstream: %s\n", strerror(errno));
            data_block_end = TRUE;
            result = FALSE;
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
            data_block_end = TRUE;
            result = FALSE;
        }

        free(buf);
        fflush(fstream);
        fclose(fstream);
        pthread_mutex_unlock(&file_mutex);
        /* ------------- EXIT CRITICAL SECTION -------------- */
    }

    return result;
}

Boolean aesdsocket_task(void* arg)
{
    Boolean result = TRUE;
    task_params* arguments = (task_params*)arg;

    printClientIpAddress(TRUE, arguments);
    result &= readClientDataToFile(arguments->conf_fd);
    result &= sendDataBackToClient(arguments->conf_fd);

    /* Data block complete, close current connection */
    close(arguments->conf_fd);
    printClientIpAddress(FALSE, arguments);

    return result;
}

U16 getTimespecDiffMs(struct timespec t1, struct timespec t2)
{
    S32 diff_msec;
    U16 result = 0U;

    diff_msec = (S_TO_MS(t2.tv_sec) - S_TO_MS(t1.tv_sec)) + (NS_TO_MS(t2.tv_nsec) - NS_TO_MS(t1.tv_nsec));

    if (diff_msec > 0)
    {
        result = (U16)diff_msec;
    }

    return result;
}

void timestamp_task(void*)
{
    FILE *fstream;
    struct timespec start, now, realtime;
    struct tm realtime_tm;
    char timestamp[30] = "timestamp:"; /* timestamp[10] is a start of actual timestamp */

    while ((timestamp_thread_exit == FALSE))
    {
        usleep(US_TO_MS(50U));
        if (client_started_sending == TRUE)
        {
            clock_gettime(CLOCK_MONOTONIC_RAW, &start);
            memset(&now, 0, sizeof(struct timespec));
            while ((getTimespecDiffMs(start, now) < TIMESTAMP_PRINT_DELAY_MS) &&
                (timestamp_thread_exit == FALSE))
            {
                usleep(US_TO_MS(250U));
                clock_gettime(CLOCK_MONOTONIC_RAW, &now);
            }

            clock_gettime(CLOCK_REALTIME, &realtime);
            memset(&realtime_tm, 0, sizeof(struct tm));
            strftime(&timestamp[10], sizeof(timestamp), "%y/%m/%d %H:%M:%S", gmtime(&realtime.tv_sec)); /* year, month, day, hour (in 24 hour format) minute and second */
    #ifdef DEBUG_ON
            printf("%s\n", timestamp);
    #endif /* DEBUG_ON */

            /* ------------- ENTER CRITICAL SECTION -------------- */
            pthread_mutex_lock(&file_mutex);

            if ((fstream = fopen((char*)SOCKET_DATA_FILEPATH, "a")) == NULL)
            {
                printf("fstream: %s\n", strerror(errno));
            }

            /* Copy bytes from buf to file stream */
            if (fwrite(timestamp, sizeof(char), sizeof(timestamp), fstream) == 0)
            {
                printf("ERROR: Nothing is written to %s", SOCKET_DATA_FILEPATH);
            }

            fwrite("\n", sizeof(char), 1, fstream);

            /* Free memory */
            fflush(fstream);
            fclose(fstream);
            pthread_mutex_unlock(&file_mutex);
            /* ------------- EXIT CRITICAL SECTION -------------- */
        }
    }
}

/**
 * 
 *      MAIN FUNCTION
 * 
*/
int main(int argc, char** argv)
{
    int conf_fd;
    struct sockaddr_in client_addr;
    int i = 1;

    /* Handle argument(s) */
    parse_args(argc, argv);
    freeaddrinfo(servinfo);

    /* Setup things and get socket file descriptor */
    setup();
    pthread_create(&threads[0], NULL, (void*)&timestamp_task, NULL);

    while(1)
    {
        /* Accept incoming connection */
        conf_fd = acceptConnection(&client_addr, listen_fd);
        t_params[i].client_addr = &client_addr;
        t_params[i].conf_fd = conf_fd;
        pthread_create(&threads[i], NULL, (void*)aesdsocket_task, (void*)&t_params[i]);
        i++;
    }

    teardown();
    return 0;
}
