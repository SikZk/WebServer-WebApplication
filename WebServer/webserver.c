#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>

#define PORT 1234
#define MAX_MESSAGE_SIZE (1024 * 1024 * 4)  // 4MB in bytes

int listening_socket;
int main_pid;
char *header200 = "HTTP/1.0 200 OK\nServer: CS241Serv v0.1\nContent-Type: text/html\n\n";
char *header400 = "HTTP/1.0 400 Bad Request\nServer: CS241Serv v0.1\nContent-Type: text/html\n\n";
char *header404 = "HTTP/1.0 404 Not Found\nServer: CS241Serv v0.1\nContent-Type: text/html\n\n";


typedef struct {
    pthread_mutex_t mutex_lock;
    int total_bytes;
} shared_variables;
typedef struct {
    int return_code;
    char *filename;
} httpRequest;


void log_request(char *request) {
    const int log_file = open("./webserver.log", O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
    if (log_file < 0) {
        fprintf(stderr, "Failed to open log file\n");
        exit(EXIT_FAILURE);
    }

    const time_t now = time(NULL);
    const struct tm *t = localtime(&now);
    char date_buffer[64];
    strftime(date_buffer, sizeof(date_buffer), "%Y-%m-%d %H:%M:%S", t);

    char buffer[strlen(request) + strlen(date_buffer) + 7];
    snprintf(buffer, sizeof(buffer), "[%s] - %s\n", date_buffer, request);
    write(log_file, buffer, strlen(buffer));
    close(log_file);
}
void terminate_server(){
    if(getpid() == main_pid) {
        log_request("Terminating server");
        printf("\nTerminating server...\n");
    }
    if (close(listening_socket)<0) {
        fprintf(stderr, "Failed to close listening socket\n");
        exit(EXIT_FAILURE);
    }
    shm_unlink("/shared_memory");
    exit(EXIT_SUCCESS);
}

char *getMessage(int conn_socket) {
    FILE *file_stream;
    if((file_stream = fdopen(conn_socket, "r")) == NULL) {
        fprintf(stderr, "Failed to open file stream\n");
        exit(EXIT_FAILURE);
    }

    size_t size = 1;
    char *block;

    // Allocate initial memory for the block
    if((block = malloc(sizeof(char) * size)) == NULL) {
        fprintf(stderr, "Failed to allocate memory in getMessage\n");
        exit(EXIT_FAILURE);
    }
    *block = '\0';

    char *tmp;
    if((tmp = malloc(sizeof(char) * size)) == NULL) {
        fprintf(stderr, "Failed to allocate memory in getMessage\n");
        exit(EXIT_FAILURE);
    }
    *tmp = '\0';

    int end;
    int old_size = 1;
    int total_size = 0;  // Track total bytes read

    while((end = getline(&tmp, &size, file_stream)) > 0) {
        if(strcmp(tmp, "\r\n") == 0) {
            break;  // End of HTTP header
        }

        total_size += end;  // Add the length of the current line
        if(total_size > MAX_MESSAGE_SIZE) {
            fprintf(stderr, "Message exceeds maximum allowed size (1MB)\n");
            free(block);
            free(tmp);
            return NULL;  // Return NULL if message exceeds 1MB
        }

        block = realloc(block, total_size + 1);  // Allocate space for the new line
        if(block == NULL) {
            fprintf(stderr, "Failed to reallocate memory in getMessage\n");
            exit(EXIT_FAILURE);
        }

        strcat(block, tmp);  // Concatenate the new line to the message
    }

    free(tmp);  // Free temporary buffer
    return block;  // Return the final message
}

char * getFileName(char * message) {
    char * path_to_file;
    if( (path_to_file = malloc(sizeof(char) * strlen(message))) == NULL) {
        fprintf(stderr, "Failed to allocate memory in getFileName\n");
        exit(EXIT_FAILURE);
    }
    sscanf(message, "GET %s HTTP/1.1", path_to_file);
    char *base;
    if( (base = malloc(sizeof(char) * (strlen(path_to_file) + 18))) == NULL) {
        fprintf(stderr, "Failed to allocate memory in getFileName\n");
        exit(EXIT_FAILURE);
    }


    char* ph = "public_html";
    strcpy(base, ph);
    strcat(base, path_to_file);
    free(path_to_file);
    return base;
}

httpRequest parseRequest(char *message) {
    httpRequest ret;
    char* filename;
    if((filename = malloc(sizeof(char) * strlen(message))) == NULL) {
        fprintf(stderr, "Failed to allocate memory in parseRequest\n");
        exit(EXIT_FAILURE);
    }
    filename = getFileName(message);

    // gives three dots to disable feature, because it stops my frontend app from working
    char *anti_dir_traversal = "...";
    char *check_dir_traversal = strstr(filename, anti_dir_traversal);
    int *check_if_users_wants_index = strcmp(filename, "public_html/");
    FILE *exists = fopen(filename, "r");

    if(check_dir_traversal != NULL) {
        ret.return_code = 400;
        ret.filename = "400.html";
    }else if(check_if_users_wants_index == 0) {
        ret.return_code = 200;
        ret.filename = "public_html/index.html";
    }else if(exists != NULL) {
        ret.return_code = 200;
        ret.filename = filename;
        fclose(exists);
    }else {
        ret.return_code = 404;
        ret.filename = "404.html";
    }
    return ret;
}

int sendMessage(int conn_socket, char * header) {
    return write(conn_socket, header, strlen(header));
}

int printHeader(int conn_socket, int return_code) {
    switch(return_code) {
        case 200:
            sendMessage(conn_socket, header200);
            return strlen(header200);
        break;
        case 400:
            sendMessage(conn_socket, header400);
            return strlen(header400);
        break;
        case 404:
            sendMessage(conn_socket, header404);
            return strlen(header404);
        break;
        default:return NULL;
    }
}

int printFile(int conn_socket, char * filename) {
    FILE *read;
    if( (read = fopen(filename, "r")) == NULL) {
        fprintf(stderr, "Failed to open file\n");
        exit(EXIT_FAILURE);
    }
    int total_size;
    struct stat st;
    stat(filename, &st);
    total_size = st.st_size;

    size_t size = 1;
    char *temp;
    if( (temp = malloc(sizeof(char) * size)) == NULL) {
        fprintf(stderr, "Failed to allocate memory in printFile\n");
        exit(EXIT_FAILURE);
    }
    int end;
    while( (end = getline( &temp, &size, read)) > 0) {
        sendMessage(conn_socket, temp);
    }
    sendMessage(conn_socket, "\n");
    free(temp);

    return total_size;
}

int record_total_bytes(int bytes_sent, shared_variables * memory_pointer) {
    pthread_mutex_lock(&(*memory_pointer).mutex_lock);
    (*memory_pointer).total_bytes += bytes_sent;
    pthread_mutex_unlock(&(*memory_pointer).mutex_lock);
    return (*memory_pointer).total_bytes;
}

int main() {
    main_pid = getpid();
    const char *green_color = "\033[0;32m";
    const char *reset_color = "\033[0m";
    const char *check_mark = "\u2714";
    log_request("----------------");
    log_request("Starting server");
    printf("\nStarting server...\n");


    short int port = PORT;
    struct sockaddr_in server_addr;
    (void) signal(SIGINT, terminate_server);
    (void) signal(SIGTERM, terminate_server);

    listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    printf("\nListening socket created ");
    if(listening_socket < 0){
        fprintf(stderr, "Failed to create socket\n");
        exit(EXIT_FAILURE);
    }
    printf("%s%s%s\n", green_color, check_mark, reset_color);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    printf("Binding socket to port %d ", port);
    if(bind(listening_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Failed to bind socket\n");
        exit(EXIT_FAILURE);
    }
    printf("%s%s%s\n", green_color, check_mark, reset_color);

    printf("Change socket to listen mode ");
    if( listen(listening_socket, 10) == -1) {
        fprintf(stderr, "Failed to listen on socket\n");
        exit(EXIT_FAILURE);
    }
    printf("%s%s%s\n", green_color, check_mark, reset_color);

    shm_unlink("/shared_memory");
    int shared_memory = shm_open("/shared_memory", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    printf("Creating shared memory ");
    if(shared_memory == -1) {
        fprintf(stderr, "Failed to create shared memory errno is: %s \n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    ftruncate(shared_memory, sizeof(shared_variables));
    shared_variables *memory_pointer = mmap(NULL, sizeof(shared_variables), PROT_READ | PROT_WRITE, MAP_SHARED, shared_memory, 0);
    if(memory_pointer == MAP_FAILED) {
        fprintf(stderr, "Failed to map shared memory\n");
        exit(EXIT_FAILURE);
    }

    pthread_mutex_init(&(*memory_pointer).mutex_lock, NULL);
    (*memory_pointer).total_bytes = 0;
    printf("%s%s%s\n", green_color, check_mark, reset_color);

    int address_size = sizeof(server_addr);

    int children = 0;
    pid_t pid;
    printf("Server IP Address: %s", inet_ntoa(server_addr.sin_addr));
    printf(":%d\n", ntohs(server_addr.sin_port));
    while(1) {
        if(children <= 10) {
            pid = fork();
            children++;
        }
        if(pid == -1) {
            fprintf(stderr, "Failed to fork\n");
            exit(1);
        }
        if(pid == 0) {
            while(1) {
                int conn_socket = accept(listening_socket, (struct sockaddr *) &server_addr, &address_size);
                if (conn_socket == -1) {
                    fprintf(stderr, "Failed to accept connection\n");
                    exit(1);
                }

                char * header = getMessage(conn_socket);
                httpRequest details = parseRequest(header);
                free(header);

                int header_size = printHeader(conn_socket, details.return_code);

                int page_size = printFile(conn_socket, details.filename);

                int total_data = record_total_bytes(header_size + page_size, memory_pointer);

                const int log_size = snprintf(NULL, 0, "Process %d served a request of %d bytes. Total bytes sent %d\n", getpid(), header_size + page_size, total_data);
                char *info_log = malloc(log_size + 1); // Allocate memory for the log string
                if (info_log == NULL) {
                    fprintf(stderr, "Failed to allocate memory for info_log\n");
                    exit(EXIT_FAILURE);
                }
                snprintf(info_log, log_size + 1, "Process %d served a request of %d bytes. Total bytes sent %d", getpid(), header_size + page_size, total_data);
                log_request(info_log);
                printf("%s\n", info_log);

                close(conn_socket);
            }
        }
    }
}
