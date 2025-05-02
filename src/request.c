#include "io_helper.h"
#include "request.h"
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAXBUF (8192)

static request_t* request_buffer;
int buffer_size = 0;  
static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t buffer_not_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t buffer_not_empty = PTHREAD_COND_INITIALIZER;

int num_threads = DEFAULT_THREADS;
int buffer_max_size = DEFAULT_BUFFER_SIZE;
int scheduling_algo = DEFAULT_SCHED_ALGO;	


void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXBUF], body[MAXBUF];
    
    sprintf(body, ""
	    "<!doctype html>\r\n"
	    "<head>\r\n"
	    "  <title>CYB-3053 WebServer Error</title>\r\n"
	    "</head>\r\n"
	    "<body>\r\n"
	    "  <h2>%s: %s</h2>\r\n" 
	    "  <p>%s: %s</p>\r\n"
	    "</body>\r\n"
	    "</html>\r\n", errnum, shortmsg, longmsg, cause);
    
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Type: text/html\r\n");
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
    write_or_die(fd, buf, strlen(buf));
    
    write_or_die(fd, body, strlen(body));
    
    close_or_die(fd);
}


void request_read_headers(int fd) {
    char buf[MAXBUF];
    
    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n")) {
	readline_or_die(fd, buf, MAXBUF);
    }
    return;
}


int request_parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;
    
    if (!strstr(uri, "cgi")) { 
	strcpy(cgiargs, "");
	sprintf(filename, ".%s", uri);
	if (uri[strlen(uri)-1] == '/') {
	    strcat(filename, "index.html");
	}
	return 1;
    } else { 
	ptr = index(uri, '?');
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	} else {
	    strcpy(cgiargs, "");
	}
	sprintf(filename, ".%s", uri);
	return 0;
    }
}


void request_get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) 
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) 
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) 
	strcpy(filetype, "image/jpeg");
    else 
	strcpy(filetype, "text/plain");
}


void request_serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXBUF], buf[MAXBUF];
    
    request_get_filetype(filename, filetype);
    srcfd = open_or_die(filename, O_RDONLY, 0);
    
    
    srcp = mmap_or_die(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close_or_die(srcfd);
    
    sprintf(buf, ""
	    "HTTP/1.0 200 OK\r\n"
	    "Server: OSTEP WebServer\r\n"
	    "Content-Length: %d\r\n"
	    "Content-Type: %s\r\n\r\n", 
	    filesize, filetype);
       
    write_or_die(fd, buf, strlen(buf));
    
    write_or_die(fd, srcp, filesize);
    munmap_or_die(srcp, filesize);
}


void* thread_request_serve_static(void* arg) {
    while (1) {
        request_t request = get_next_request();
        
        request_serve_static(request.fd, request.filename, request.filesize);
        
        close_or_die(request.fd);
    }
    return NULL;
}


void request_handle(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char filename[MAXBUF], cgiargs[MAXBUF];
    
    readline_or_die(fd, buf, MAXBUF);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method:%s uri:%s version:%s\n", method, uri, version);

    if (strcasecmp(method, "GET")) {
        request_error(fd, method, "501", "Not Implemented", "server does not implement this method");
        return;
    }
    request_read_headers(fd);
    
    is_static = request_parse_uri(uri, filename, cgiargs);
    
    if (stat(filename, &sbuf) < 0) {
        request_error(fd, filename, "404", "Not found", "server could not find this file");
        return;
    }
    
    if (is_static) {
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            request_error(fd, filename, "403", "Forbidden", "server could not read this file");
            return;
        }
        
        char real_path[MAXBUF];
        char root_dir[MAXBUF] = ".";
        if (realpath(filename, real_path) == NULL) {
            request_error(fd, filename, "403", "Forbidden", "Invalid file path");
            return;
        }
        
        if (strncmp(real_path, root_dir, strlen(root_dir)) != 0) {
            request_error(fd, filename, "403", "Forbidden", "Directory traversal attempt detected");
            return;
        }
        
        add_request(fd, filename, sbuf.st_size);
        
    } else {
        request_error(fd, filename, "501", "Not Implemented", "server does not serve dynamic content request");
    }
}

void init_request_buffer() {
    request_buffer = (request_t*)malloc(buffer_max_size * sizeof(request_t));
    if (request_buffer == NULL) {
        perror("Failed to allocate request buffer");
        exit(1);
    }
}

void add_request(int fd, char* filename, int filesize) {
    pthread_mutex_lock(&buffer_mutex);
    
    while (buffer_size >= buffer_max_size) {
        pthread_cond_wait(&buffer_not_full, &buffer_mutex);
    }
    
    switch (scheduling_algo) {
        case 0: 
            request_buffer[buffer_size].fd = fd;
            strcpy(request_buffer[buffer_size].filename, filename);
            request_buffer[buffer_size].filesize = filesize;
            request_buffer[buffer_size].arrival_time = time(NULL);
            buffer_size++;
            break;
            
        case 1: 
            {
                int insert_pos = 0;
                while (insert_pos < buffer_size && 
                       request_buffer[insert_pos].filesize <= filesize) {
                    insert_pos++;
                }
                for (int i = buffer_size; i > insert_pos; i--) {
                    request_buffer[i] = request_buffer[i-1];
                }
                request_buffer[insert_pos].fd = fd;
                strcpy(request_buffer[insert_pos].filename, filename);
                request_buffer[insert_pos].filesize = filesize;
                request_buffer[insert_pos].arrival_time = time(NULL);
                buffer_size++;
            }
            break;
            
        case 2: 
            {
                int insert_pos = rand() % (buffer_size + 1);
                for (int i = buffer_size; i > insert_pos; i--) {
                    request_buffer[i] = request_buffer[i-1];
                }
                request_buffer[insert_pos].fd = fd;
                strcpy(request_buffer[insert_pos].filename, filename);
                request_buffer[insert_pos].filesize = filesize;
                request_buffer[insert_pos].arrival_time = time(NULL);
                buffer_size++;
            }
            break;
    }
    
    pthread_cond_signal(&buffer_not_empty);
    pthread_mutex_unlock(&buffer_mutex);
}

request_t get_next_request() {
    pthread_mutex_lock(&buffer_mutex);
    
    while (buffer_size == 0) {
        pthread_cond_wait(&buffer_not_empty, &buffer_mutex);
    }
    
    request_t request = request_buffer[0];
    
    for (int i = 0; i < buffer_size - 1; i++) {
        request_buffer[i] = request_buffer[i + 1];
    }
    buffer_size--;
    
    pthread_cond_signal(&buffer_not_full);
    pthread_mutex_unlock(&buffer_mutex);
    
    return request;
}
