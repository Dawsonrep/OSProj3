#pragma once

#define DEFAULT_BUFFER_SIZE 64
#define DEFAULT_THREADS 4
#define DEFAULT_SCHED_ALGO 0		// 0 - FIFO, 1 - SFF, 2 - RANDOM

// Request structure to store HTTP request information
typedef struct {
    int fd;                 // Client socket file descriptor
    char filename[8192];    // Requested file path
    int filesize;          // Size of the requested file
    time_t arrival_time;   // Time when request arrived (for FIFO)
} request_t;

extern int buffer_max_size;
extern int buffer_size;
extern int scheduling_algo;
extern int num_threads;

// Function declarations
void request_handle(int fd);
void* thread_request_serve_static(void* arg);
void init_request_buffer(void);
void add_request(int fd, char* filename, int filesize);
request_t get_next_request(void);
void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void request_read_headers(int fd);
int request_parse_uri(char *uri, char *filename, char *cgiargs);
void request_get_filetype(char *filename, char *filetype);
void request_serve_static(int fd, char *filename, int filesize);
