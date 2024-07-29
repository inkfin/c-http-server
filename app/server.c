#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*** defines ***/

/* safe buffer size of a package */
#define BUFFER_SIZE 1500
#define MAX_PTHREAD_NUM 8

#define REQ_USER_AGENT "GET /user-agent "
#define REQ_FILE "GET /files/"
#define REQ_ECHO "GET /echo/"
#define REQ_ROOT "GET / "

/*** constants ***/

char const* const reply_200 = "HTTP/1.1 200 OK\r\n\r\n";
char const* const reply_404 = "HTTP/1.1 404 Not Found\r\n\r\n";

char const* const fmt_reply_200 =
    /* Statue line */
    "HTTP/1.1 200 OK\r\n"
    /* Headers */
    "Content-Type: %s\r\n"
    "Content-Length: %lu\r\n"
    "\r\n"
    /* Response body */
    "%s";

/*** structs ***/

struct g_args {
    char* file_path;
} g_args = { 0 };

/*** free ***/

void free_resource()
{
}

/*** args ***/

int parse_args(int argc, char* argv[])
{
    if (argc <= 1) {
        // no arguments early return
        return 0;
    }
    for (size_t i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--", 2) == 0) {
            if (strcmp(argv[i] + 2, "directory") == 0 && i + 1 < argc) {
                g_args.file_path = argv[i + 1];
                ++i;
            }
        }
    }
    return 0;
}

/*** I/O ***/

#define LOCAL_STR_N_COPY(_src, n, buffer_name) \
    char buffer_name[n + 1];                   \
    memcpy(buffer_name, _src, n);              \
    buffer_name[n] = '\0';

#define LOCAL_STR_COPY(_src, buffer_name) \
    LOCAL_STR_N_COPY(_src, strlen(_src), buffer_name)

#define LOCAL_STR_CONCAT(_s1, _s2, buffer_name)      \
    char buffer_name[strlen(_s1) + strlen(_s2) + 1]; \
    strcat(buffer_name, _s1);                        \
    strcat(buffer_name + strlen(_s1), _s2);          \
    buffer_name[strlen(_s1) + strlen(_s2)] = '\0';

int file_exists(char* file_path)
{
    return access(file_path, F_OK) == 0;
}

size_t file_size(char* file_path)
{
    size_t buf_size = -1;
    FILE* fp = fopen(file_path, "r");
    if (fp != NULL) {
        if (fseek(fp, 0L, SEEK_END) == 0) {
            buf_size = ftell(fp);
        }
        fclose(fp);
    }
    return buf_size;
}

int read_file(char* file_path, char* buffer, size_t buf_size)
{
    FILE* fp = fopen(file_path, "r");
    if (fp != NULL) {
        size_t new_len = fread(buffer, sizeof(char), buf_size, fp);
        if (ferror(fp) != 0) {
            fputs("[Error] read_file", stderr);
            return -1;
        }
        fclose(fp);
    }
    return 0;
}

/*** threads ***/

/* pthread func that handle one client request */
void* handle_connection(void* pclient_fd)
{
    int client_fd = *(int*)pclient_fd;
    while (1) {
        char sz_recv_buf[BUFFER_SIZE];
        char sz_send_buf[BUFFER_SIZE];
        char const* sz_send_message;

        const int recv_numbytes = recv(client_fd, sz_recv_buf, BUFFER_SIZE - 1, 0);
        if (recv_numbytes == -1) {
            printf("Recv failed: %s \n", strerror(errno));
            return NULL;
        } else if (recv_numbytes == 0) {
            printf("Connection closed\n");
            break;
        } else {
            printf("Received message success:\n"
                   "<length=%d>\n"
                   "/***content-beg***/\n"
                   "%s<end>\n"
                   "/***content-end***/\n",
                recv_numbytes, sz_recv_buf);
        }

        if (strncmp(sz_recv_buf, REQ_USER_AGENT, /* prefix length */ strlen(REQ_USER_AGENT)) == 0) {
            char const* psz_beg = strstr(sz_recv_buf + strlen(REQ_USER_AGENT), "User-Agent: ");
            char const* const psz_end = strstr(psz_beg, "\r\n");
            if (psz_beg != NULL && psz_end != NULL) {
                psz_beg += strlen("User-Agent: ");
                char sz_temp_buf[BUFFER_SIZE];
                memcpy(sz_temp_buf, psz_beg, (psz_end - psz_beg));
                sz_temp_buf[psz_end - psz_beg] = '\0';
                sprintf(sz_send_buf, fmt_reply_200, "text/plain", strlen(sz_temp_buf), sz_temp_buf);
                sz_send_message = sz_send_buf;
            } else {
                printf("parse error: can't find header User-Agent\n");
                sz_send_message = reply_404;
            }
        } else if (strncmp(sz_recv_buf, REQ_FILE, strlen(REQ_FILE)) == 0) {
            char const* const psz_beg = strstr(sz_recv_buf, REQ_FILE) + strlen(REQ_FILE);
            char const* const psz_end = strstr(psz_beg, " ");
            if (psz_end != NULL && g_args.file_path != NULL) {
                LOCAL_STR_N_COPY(psz_beg, psz_end - psz_beg, file_name);
                LOCAL_STR_CONCAT(g_args.file_path, file_name, sz_full_path);
                printf("[REQ_FILE]: try load from file: %s\n", file_name);
                size_t buf_size;
                if (file_exists(sz_full_path) && (buf_size = file_size(sz_full_path)) != (size_t)-1) {
                    printf("[REQ_FILE] load from file full path: %s\n", sz_full_path);
                    char sz_temp_buf[buf_size + 1];
                    read_file(sz_full_path, sz_temp_buf, buf_size);
                    sz_temp_buf[buf_size] = '\0';
                    printf("<Length:%lu>\n=== read content: ===\n%s\n=====================\n", buf_size, sz_temp_buf);
                    sprintf(sz_send_buf, fmt_reply_200, "application/octet-stream", strlen(sz_temp_buf), sz_temp_buf);
                    sz_send_message = sz_send_buf;
                } else {
                    printf("[REQ_FILE]: file `%s` doesn't exists\n", file_name);
                    sz_send_message = reply_404;
                }
            } else {
                printf("[REQ_FILE]: get files requires path arguments '--directory'\n");
                sz_send_message = reply_404;
            }
        } else if (strncmp(sz_recv_buf, REQ_ECHO, strlen(REQ_ECHO)) == 0) {
            // first char after request str
            char const* const sz_echo_str = strtok(sz_recv_buf + strlen(REQ_ECHO), " ");
            if (sz_echo_str) {
                sprintf(sz_send_buf, fmt_reply_200, "text/plain", strlen(sz_echo_str), sz_echo_str);
                sz_send_message = sz_send_buf;
            }
        } else if (strncmp(sz_recv_buf, REQ_ROOT, strlen(REQ_ROOT)) == 0) {
            sz_send_message = reply_200;
        } else {
            sz_send_message = reply_404;
        }

        const int sent_numbytes = send(client_fd, sz_send_message, strlen(sz_send_message), 0);
        if (sent_numbytes == -1) {
            printf("Send failed: %s \n", strerror(errno));
            return NULL;
        } else {
            printf("Send message success:\n"
                   "/***content-beg***/\n"
                   "%s<end>\n"
                   "/***content-end***/\n",
                sz_send_message);
        }
    }

    return NULL;
}

/*** exec ***/

int main(int argc, char* argv[])
{
    // Disable output buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    parse_args(argc, argv);

    int server_fd,
        client_fd;
    socklen_t client_addr_len;
    struct sockaddr_in client_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        printf("Socket creation failed: %s...\n", strerror(errno));
        goto SAFE_RETURN;
    }

    // Since the tester restarts your program quite often, setting SO_REUSEADDR
    // ensures that we don't run into 'Address already in use' errors
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        printf("SO_REUSEADDR failed: %s \n", strerror(errno));
        goto SAFE_RETURN;
    }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(4221),
        .sin_addr = { htonl(INADDR_ANY) },
    };

    if (bind(server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) != 0) {
        printf("Bind failed: %s \n", strerror(errno));
        goto SAFE_RETURN;
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        printf("Listen failed: %s \n", strerror(errno));
        goto SAFE_RETURN;
    }

    printf("Waiting for a client to connect...\n");
    client_addr_len = sizeof(client_addr);

    // create pthread
    pthread_t arr_pthread[MAX_PTHREAD_NUM];
    int arr_pthread_ret[MAX_PTHREAD_NUM];
    int pthread_idx = 0;

    while (1) {
        if ((client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len)) == -1) {
            printf("Create client failed: %s \n", strerror(errno));
            break;
        } else {
            arr_pthread_ret[pthread_idx] = pthread_create(&arr_pthread[pthread_idx], NULL, handle_connection, (void*)&client_fd);
            ++pthread_idx;
            printf("Client connected\n");
        }
    }

    for (int i = 0; i < MAX_PTHREAD_NUM; ++i) {
        pthread_join(arr_pthread[i], NULL);
    }

    for (int i = 0; i < MAX_PTHREAD_NUM; ++i) {
        printf("pthread[%d] returns %d", i, arr_pthread_ret[i]);
    }

    close(server_fd);

SAFE_RETURN:
    free_resource();

    return 0;
}
