#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*** defines ***/

#define BUFFER_SIZE 1500

#define REQ_USER_AGENT "GET /user-agent "
#define REQ_ECHO "GET /echo/"
#define REQ_ROOT "GET / "

/*** constants ***/

char const *const reply_200 = "HTTP/1.1 200 OK\r\n\r\n";
char const *const reply_404 = "HTTP/1.1 404 Not Found\r\n\r\n";

char const *const fmt_reply_200 =
    /* Statue line */
    "HTTP/1.1 200 OK\r\n"
    /* Headers */
    "Content-Type: text/plain\r\n"
    "Content-Length: %d\r\n"
    "\r\n"
    /* Response body */
    "%s";

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
    if (server_fd == -1)
    {
        printf("Socket creation failed: %s...\n", strerror(errno));
        return 1;
    }

    // Since the tester restarts your program quite often, setting SO_REUSEADDR
    // ensures that we don't run into 'Address already in use' errors
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        printf("SO_REUSEADDR failed: %s \n", strerror(errno));
        return 1;
    }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(4221),
        .sin_addr = {htonl(INADDR_ANY)},
    };

    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
    {
        printf("Bind failed: %s \n", strerror(errno));
        return 1;
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0)
    {
        printf("Listen failed: %s \n", strerror(errno));
        return 1;
    }

    printf("Waiting for a client to connect...\n");
    client_addr_len = sizeof(client_addr);

    if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len)) == -1)
    {
        printf("Create client failed: %s \n", strerror(errno));
        return 1;
    }
    printf("Client connected\n");

    int recv_numbytes, sent_numbytes;
    char pc_recv_buf[BUFFER_SIZE];
    char pc_send_buf[BUFFER_SIZE];
    char pc_temp_buf[BUFFER_SIZE];
    char *pc_echo_str;
    char const *pc_reply_str;
    while (1)
    {
        recv_numbytes = recv(client_fd, pc_recv_buf, BUFFER_SIZE - 1, 0);
        if (recv_numbytes == -1)
        {
            printf("Recv failed: %s \n", strerror(errno));
            return 1;
        }
        else if (recv_numbytes == 0)
        {
            printf("Connection closed\n");
            break;
        }
        else
        {
            printf("Received message success:\n"
                   "<length=%d>\n"
                   "/***content-beg***/\n"
                   "%s<end>\n"
                   "/***content-end***/\n",
                   recv_numbytes, pc_recv_buf);
        }

        if (strncmp(pc_recv_buf, REQ_USER_AGENT, /* prefix length */ strlen(REQ_USER_AGENT)) == 0)
        {
            char *pc_start, *pc_end;
            pc_start = strstr(pc_recv_buf + strlen(REQ_USER_AGENT), "User-Agent: ");
            pc_end = strstr(pc_start, "\r\n");
            if (pc_start != NULL && pc_end != NULL)
            {
                pc_start += strlen("User-Agent: ");
                memcpy(pc_temp_buf, pc_start, (pc_end - pc_start));
                pc_temp_buf[pc_end - pc_start] = '\0';
                sprintf(pc_send_buf, fmt_reply_200, (int)strlen(pc_temp_buf), pc_temp_buf);
                pc_reply_str = pc_send_buf;
            }
            else
            {
                printf("parse error: can't find header User-Agent\n");
                return 1;
            }
        }
        else if (strncmp(pc_recv_buf, REQ_ECHO, strlen(REQ_ECHO)) == 0)
        {
            if ((pc_echo_str = strtok(/* first char after request str */ pc_recv_buf + strlen(REQ_ECHO), " ")))
            {
                sprintf(pc_send_buf, fmt_reply_200, (int)strlen(pc_echo_str), pc_echo_str);
                pc_reply_str = pc_send_buf;
            }
        }
        else if (strncmp(pc_recv_buf, REQ_ROOT, strlen(REQ_ROOT)) == 0)
        {
            pc_reply_str = reply_200;
        }
        else
        {
            pc_reply_str = reply_404;
        }

        if ((sent_numbytes = send(client_fd, pc_reply_str, strlen(pc_reply_str), 0)) == -1)
        {
            printf("Send failed: %s \n", strerror(errno));
            return 1;
        }
        else
        {
            printf("Send message success:\n"
                   "/***content-beg***/\n"
                   "%s<end>\n"
                   "/***content-end***/\n",
                   pc_reply_str);
        }
    }

    close(server_fd);

    return 0;
}
