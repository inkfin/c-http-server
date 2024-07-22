#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1500
const char *const reply_200 = "HTTP/1.1 200 OK\r\n\r\n";
const char *const reply_404 = "HTTP/1.1 404 Not Found\r\n\r\n";

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

    int bytes_recv, bytes_sent;
    char buffer[BUFFER_SIZE];
    int live = 2;
    while (live--)
    {
        bytes_recv = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_recv == -1)
        {
            printf("Recv failed: %s \n", strerror(errno));
            return 1;
        }
        else if (bytes_recv == 0)
        {
            printf("Connection closed\n");
        }
        else
        {
            printf("Received message success\nlength=%d\n---content-beg---\n%s---content-end---\n", bytes_recv, buffer);
        }

        if (strncmp(buffer, "GET / ", 6) == 0)
        {
            if ((bytes_sent = send(client_fd, reply_200, strlen(reply_200), 0)) == -1)
            {
                printf("Send failed: %s \n", strerror(errno));
                return 1;
            }
        }
        else
        {
            if ((bytes_sent = send(client_fd, reply_404, strlen(reply_404), 0)) == -1)
            {
                printf("Send failed: %s \n", strerror(errno));
                return 1;
            }
        }
    }

    close(server_fd);

    return 0;
}
