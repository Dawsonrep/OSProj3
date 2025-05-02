//
//	Main webserver code file (with main() fuction)
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "request.h"
#include "io_helper.h"

char default_root[] = ".";


int main(int argc, char *argv[]) {
    int c;
    char *root_dir = default_root;
    int port = 10000;
        
    while ((c = getopt(argc, argv, "hd:p:t:b:s:")) != -1)
        switch (c) {
            case 'd':
                root_dir = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 't':
                num_threads = atoi(optarg);
                break;
            case 'b':
                buffer_max_size = atoi(optarg);
                break;
            case 's':
                scheduling_algo = atoi(optarg);
                break;
            case 'h':
                fprintf(stdout, "usage: wserver [-d basedir] [-p port] [-t threads] [-b buffersize] [-s schedalg (0 - FIFO, 1 - SFF, 2 - Random)]\n");
                exit(0);
            default:
                fprintf(stderr, "usage: wserver [-d basedir] [-p port] [-t threads] [-b buffersize] [-s schedalg (0 - FIFO, 1 - SFF, 2 - Random)]\n");
                exit(1);
        }

    chdir_or_die(root_dir);

    srand(time(NULL));

    init_request_buffer();

    pthread_t thread_pool[num_threads];
    for(int i = 0; i < num_threads; i++) {
        if (pthread_create(&thread_pool[i], NULL, thread_request_serve_static, NULL) != 0) {
            perror("Failed to create thread");
            exit(1);
        }
    }
    
    int listen_fd = open_listen_fd_or_die(port);
    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        int conn_fd = accept_or_die(listen_fd, (sockaddr_t *) &client_addr, (socklen_t *) &client_len);
        
        request_handle(conn_fd);
    }
    
    return 0;
}
