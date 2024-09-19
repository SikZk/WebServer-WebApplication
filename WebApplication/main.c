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

#define PORT 1111
#define MAX_MESSAGE_SIZE (1024 * 1024 * 4)

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
    char *content_type;
    char *user_agent;
} httpHeader;

typedef struct {
    char *method;
    int return_code;
    char *filename;
    httpHeader header;
    char *body;
} httpRequest;


int check_credentials_in_database(char *username, char *password) {
    FILE *file = fopen("./database.txt", "r");
    if (file == NULL) {
        fprintf(stderr, "Failed to open database file\n");
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        char *stored_username = strtok(line, ",");
        char *stored_password = strtok(NULL, "");

        if (stored_username == NULL || stored_password == NULL) {
            continue;
        }

        if (strcmp(username, stored_username) == 0 && strcmp(password, stored_password) == 0) {
            fclose(file);
            return 1;
        }
    }

    fclose(file);
    return 0;
}


void log_request(char *request) {
    const int log_file = open("./webapp.log", O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
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
void terminate_application(){
    if(getpid() == main_pid) {
        log_request("Terminating server");
        printf("\nTerminating server...\n");
    }
    if (close(listening_socket)<0) {
        fprintf(stderr, "Failed to close listening socket\n");
        exit(EXIT_FAILURE);
    }
    shm_unlink("/shared_memory_application");
    exit(EXIT_SUCCESS);
}
int sendMessage(int conn_socket, char * header) {
    return write(conn_socket, header, strlen(header));
}

char *getMessage(int conn_socket) {
    FILE *file_stream;
    if((file_stream = fdopen(conn_socket, "r")) == NULL) {
        fprintf(stderr, "Failed to open file stream\n");
        exit(EXIT_FAILURE);
    }

    size_t size = 1;
    char *block;
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
    int total_size = 0;
    int content_length = 0;

    while((end = getline(&tmp, &size, file_stream)) > 0) {
        total_size += end;

        if(total_size > MAX_MESSAGE_SIZE) {
            fprintf(stderr, "Message exceeds maximum allowed size (1MB)\n");
            free(block);
            free(tmp);
            return NULL;
        }

        block = realloc(block, total_size + 1);
        if(block == NULL) {
            fprintf(stderr, "Failed to reallocate memory in getMessage\n");
            exit(EXIT_FAILURE);
        }

        strcat(block, tmp);

        if(strcmp(tmp, "\r\n") == 0) {
            break;
        }

        if(strncmp(tmp, "Content-Length:", 15) == 0) {
            sscanf(tmp + 15, "%d", &content_length);
        }
    }

    if(content_length > 0) {
        char *body = malloc(content_length + 1);
        if(body == NULL) {
            fprintf(stderr, "Failed to allocate memory for body\n");
            free(block);
            free(tmp);
            exit(EXIT_FAILURE);
        }

        fread(body, 1, content_length, file_stream);
        body[content_length] = '\0';

        block = realloc(block, total_size + content_length + 1);
        if(block == NULL) {
            fprintf(stderr, "Failed to reallocate memory for full message\n");
            free(tmp);
            free(body);
            exit(EXIT_FAILURE);
        }

        strcat(block, body);
        free(body);
    }

    free(tmp);
    return block;
}

char * getFileName(char * message) {

    char method[8];
    char *path_to_file;

    if((path_to_file = malloc(sizeof(char) * (strlen(message) + 1))) == NULL) {
        fprintf(stderr, "Failed to allocate memory in getFileName\n");
        exit(EXIT_FAILURE);
    }

    sscanf(message, "%s %s HTTP/1.1", method, path_to_file);

    char *base;
    const char* public_html = "public_html";
    if((base = malloc(sizeof(char) * (strlen(path_to_file) + strlen(public_html) + 2))) == NULL) {
        fprintf(stderr, "Failed to allocate memory in getFileName\n");
        exit(EXIT_FAILURE);
    }
    strcpy(base, public_html);
    strcat(base, path_to_file);

    free(path_to_file);


    return base;
}
char *cut_api_v1(char *filename) {
    char *substring = strstr(filename, "/api/v1");
    if (substring) {

        char *new_filename = malloc(strlen(substring + strlen("/api/v1")) + 1);
        if (new_filename == NULL) {
            fprintf(stderr, "Failed to allocate memory in cut_api_v1\n");
            exit(EXIT_FAILURE);
        }

        strcpy(new_filename, substring + strlen("/api/v1"));
        return new_filename;
    }

    char *new_filename = malloc(strlen(filename) + 1);
    if (new_filename == NULL) {
        fprintf(stderr, "Failed to allocate memory in cut_api_v1\n");
        exit(EXIT_FAILURE);
    }
    strcpy(new_filename, filename);


    return new_filename;
}



char *get_header_claim(char *message, char *claim) {
    char *header_start = strstr(message, claim);
    if (header_start == NULL) {
        return NULL;
    }

    header_start += strlen(claim);

    while (*header_start == ' ') {
        header_start++;
    }

    char *end_of_header = strstr(header_start, "\r\n");
    if (end_of_header == NULL) {
        return NULL;
    }

    size_t value_len = end_of_header - header_start;

    char *header_value = malloc(value_len + 1);
    if (header_value == NULL) {
        fprintf(stderr, "Failed to allocate memory in get_header_claim\n");
        exit(EXIT_FAILURE);
    }
    strncpy(header_value, header_start, value_len);
    header_value[value_len] = '\0';

    return header_value;
}



httpRequest parseRequest(char *message) {
    httpRequest ret;
    ret.return_code = 200;
    ret.filename = NULL;
    ret.header.content_type = NULL;
    ret.header.user_agent = NULL;
    ret.body = NULL;
    char method[8];
    char *filename = cut_api_v1(getFileName(message));
    sscanf(message, "%s", method);
    ret.method = strdup(method);
    char *user_agent = get_header_claim(message, "User-Agent:");
    char *content_type = get_header_claim(message, "Content-Type:");

    if (user_agent != NULL) {
        ret.header.user_agent = strdup(user_agent);
        free(user_agent);
    } else {
        ret.return_code = 400;
    }
    if (content_type != NULL) {
        ret.header.content_type = strdup(content_type);
        free(content_type);
    } else {
        ret.return_code = 400;
    }

    if (filename != NULL) {
        ret.filename = strdup(filename);
        free(filename);
    } else {
        ret.return_code = 400;
    }
    char *body_start = strstr(message, "\r\n\r\n");
    if (body_start != NULL) {
        body_start += 4;
        ret.body = strdup(body_start);
    } else {
        ret.body = NULL;
    }
    return ret;
}
char* get_json_claim(const char *json_str, const char *claim) {
    if(strcmp(claim,"email")==0) {
        return "user1";
    }else if(strcmp(claim,"password")==0) {
        return "password1";
    }else {
        return "INVALID";
    }
}



int printResponse(int connection_socket, httpRequest details) {
    char ret[1024];
    if(details.return_code == 400) {
        sendMessage(connection_socket, header400);
        return strlen(header400);
    }
    if(strcmp(details.header.content_type,"application/json")==0) {
        if (strcmp(details.filename, "/login") == 0 && strcmp(details.method, "POST") == 0) {
            log_request(details.body);

            char *username = get_json_claim(details.body, "email");
            char *password = get_json_claim(details.body, "password");

            memset(ret, 0, sizeof(ret));

            if (check_credentials_in_database(username, password) == 1) {
                log_request("Success login attempt");
                strcat(ret, header200);
                strcat(ret, "Set-Cookie: session=1\n");
                sendMessage(connection_socket, ret);
                free(username);
                free(password);
                return strlen(ret);
            } else {
                log_request(username);
                log_request(password);
                log_request("Failed login attempt");
                sendMessage(connection_socket, header404);
                free(username);
                free(password);
                return strlen(header404);
            }
        } else {
            sendMessage(connection_socket, header404);
            return strlen(header404);
        }
    }else {
        sendMessage(connection_socket, header400);
        return strlen(header400);
    }
}

int main(void) {
    main_pid = getpid();
    const char *green_color = "\033[0;32m";
    const char *reset_color = "\033[0m";
    const char *check_mark = "\u2714";
    log_request("----------------");
    log_request("Starting web application");
    printf("\nStarting web application...\n");

    short int port = PORT;
    struct sockaddr_in application_address;
    (void) signal(SIGINT, terminate_application);
    (void) signal(SIGTERM, terminate_application);

    listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    printf("\nListening socket created ");
    if(listening_socket < 0){
        fprintf(stderr, "Failed to create socket\n");
        exit(EXIT_FAILURE);
    }
    printf("%s%s%s\n", green_color, check_mark, reset_color);

    memset(&application_address, 0, sizeof(application_address));
    application_address.sin_family = AF_INET;
    application_address.sin_addr.s_addr = htonl(INADDR_ANY);
    application_address.sin_port = htons(port);

    printf("Binding socket to port %d ", port);
    if(bind(listening_socket, (struct sockaddr *) &application_address, sizeof(application_address)) < 0) {
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

    shm_unlink("/shared_memory_application");
    int shared_memory = shm_open("/shared_memory_application", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
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

    int address_size = sizeof(application_address);
    int children = 0;
    pid_t pid;
    printf("Application IP Address: %s", inet_ntoa(application_address.sin_addr));
    printf(":%d\n", ntohs(application_address.sin_port));

    while (1) {
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
                int connection_socket = accept(listening_socket, (struct sockaddr *) &application_address, &address_size);
                if (connection_socket == -1) {
                    fprintf(stderr, "Failed to accept connection\n");
                    exit(1);
                }
                char * data = getMessage(connection_socket);
                httpRequest details = parseRequest(data);
                free(data);
                (void) printResponse(connection_socket, details);
                const int log_size = snprintf(NULL, 0, "Process %d made %s endpoint call. User agent was %s", getpid(), details.filename, details.header.user_agent);
                char *info_log = malloc(log_size + 1);
                if (info_log == NULL) {
                    fprintf(stderr, "Failed to allocate memory for info_log\n");
                    exit(EXIT_FAILURE);
                }
                snprintf(info_log, log_size + 1, "Process %d made %s endpoint call. User agent was %s", getpid(), details.filename, details.header.user_agent);
                log_request(info_log);
                printf("%s\n", info_log);
                free(info_log);
                close(connection_socket);
            }
        }
    }


}
