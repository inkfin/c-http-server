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

#define BUFFER_SIZE 1500
#define MAX_PTHREAD_NUM 8

#define REQ_USER_AGENT "GET /user-agent "
#define REQ_ECHO "GET /echo/"
#define REQ_ROOT "GET / "

/*** constants ***/

char const* const reply_200 = "HTTP/1.1 200 OK\r\n\r\n";
char const* const reply_404 = "HTTP/1.1 404 Not Found\r\n\r\n";

char const* const fmt_reply_200 =
    /* Statue line */
    "HTTP/1.1 200 OK\r\n"
    /* Headers */
    "Content-Type: text/plain\r\n"
    "Content-Length: %d\r\n"
    "\r\n"
    /* Response body */
    "%s";

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
            char *pc_start, *pc_end;
            pc_start = strstr(sz_recv_buf + strlen(REQ_USER_AGENT), "User-Agent: ");
            pc_end = strstr(pc_start, "\r\n");
            if (pc_start != NULL && pc_end != NULL) {
                char sz_temp_buf[BUFFER_SIZE];

                pc_start += strlen("User-Agent: ");
                memcpy(sz_temp_buf, pc_start, (pc_end - pc_start));
                sz_temp_buf[pc_end - pc_start] = '\0';
                sprintf(sz_send_buf, fmt_reply_200, (int)strlen(sz_temp_buf), sz_temp_buf);
                sz_send_message = sz_send_buf;
            } else {
                printf("parse error: can't find header User-Agent\n");
                return NULL;
            }
        } else if (strncmp(sz_recv_buf, REQ_ECHO, strlen(REQ_ECHO)) == 0) {
            // first char after request str
            char const* const sz_echo_str = strtok(sz_recv_buf + strlen(REQ_ECHO), " ");
            if (sz_echo_str) {
                sprintf(sz_send_buf, fmt_reply_200, (int)strlen(sz_echo_str), sz_echo_str);
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

int main()
{
    // Disable output buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    int server_fd, client_fd;
    socklen_t client_addr_len;
    struct sockaddr_in client_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        printf("Socket creation failed: %s...\n", strerror(errno));
        return 1;
    }

    // Since the tester restarts your program quite often, setting SO_REUSEADDR
    // ensures that we don't run into 'Address already in use' errors
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        printf("SO_REUSEADDR failed: %s \n", strerror(errno));
        return 1;
    }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(4221),
        .sin_addr = { htonl(INADDR_ANY) },
    };

    if (bind(server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) != 0) {
        printf("Bind failed: %s \n", strerror(errno));
        return 1;
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        printf("Listen failed: %s \n", strerror(errno));
        return 1;
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

    return 0;
}
