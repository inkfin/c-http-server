#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zlib.h>

/*** defines ***/

/* safe buffer size of a package */
#define BUFFER_SIZE 1500
#define MAX_PTHREAD_NUM 32

#define REQ_USER_AGENT "/user-agent"
#define REQ_FILE "/files/"
#define REQ_ECHO "/echo/"
#define REQ_ROOT "/"

/*** constants ***/

char const* const reply_200
    = "HTTP/1.1 200 OK\r\n\r\n";
char const* const reply_201 = "HTTP/1.1 201 Created\r\n\r\n";
char const* const reply_404 = "HTTP/1.1 404 Not Found\r\n\r\n";

char const* const fmt_reply_200 =
    /* Statue line */
    "HTTP/1.1 200 OK\r\n"
    /* Headers */
    "Content-Type: %s\r\n"
    "Content-Length: %s\r\n"
    "%s"
    "\r\n"
    /* Response body */
    "%s";

#define fill_fmt_reply_200(out_buf, content_type, content_length, extra_headers, body) \
    sprintf(out_buf, fmt_reply_200, content_type, content_length, extra_headers, body);

/*** enums ***/

typedef enum {
    REQ_TYPE_UNDEF,
    REQ_TYPE_GET,
    REQ_TYPE_POST,
    REQ_TYPE_ENUM_LENGTH,
} REQ_TYPE;

typedef enum {
    HTTP_VUNDEF,
    HTTP_V11, /* HTTP/1.1 */
} HTTP_VERSION;

int const ENCODING_TYPE_UNDEF = 0x0;
int const ENCODING_TYPE_GZIP = 0x1;

/*** structs ***/

typedef struct {
    /* request line */
    int req_type;
    char* request;
    int http_ver;
    /* header */
    char* host;
    char* user_agent;
    char* accept;
    int accept_encoding;
    char* content_type;
    size_t content_length;
    /* request body */
    char* body;
} headerData;

typedef struct {
    int client_fd;
} tParams;

struct gArgs {
    char* file_path;
} g_args = { 0 };

/*** free ***/

void g_free_resource()
{
    free(g_args.file_path);
}

void free_header_data(headerData* data)
{
    if (data == NULL)
        return;
    if (data->request != NULL)
        free(data->request);
    if (data->host != NULL)
        free(data->host);
    if (data->user_agent != NULL)
        free(data->user_agent);
    if (data->accept != NULL)
        free(data->accept);
    if (data->content_type != NULL)
        free(data->content_type);
    if (data->body != NULL)
        free(data->body);
    free(data);
}

/*** args ***/

int parse_args(int argc, char* argv[])
{
    if (argc <= 1) {
        // no arguments early return
        return 0;
    }
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--", 2) == 0) {
            if (strcmp(argv[i] + 2, "directory") == 0 && i + 1 < argc) {
                g_args.file_path = malloc(strlen(argv[i + 1]) + 1);
                memcpy(g_args.file_path, argv[i + 1], strlen(argv[i + 1]));
                g_args.file_path[strlen(argv[i + 1])] = '\0';
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
        fread(buffer, sizeof(char), buf_size, fp);
        if (ferror(fp) != 0) {
            fputs("[Error] read_file", stderr);
            return -1;
        }
        fclose(fp);
    }
    return 0;
}

int write_file(char* file_path, char* buffer)
{
    FILE* fp = fopen(file_path, "w");
    if (fp != NULL) {
        fprintf(fp, "%s", buffer);
        fclose(fp);
    }
    return 0;
}

/*** file compression ***/

int compress_to_gzip(const char* input, int input_size, char* output, int output_size)
{
    z_stream zs = {
        .zalloc = Z_NULL,
        .zfree = Z_NULL,
        .opaque = Z_NULL,
        .avail_in = (uInt)input_size,
        .next_in = (Bytef*)input,
        .avail_out = (uInt)output_size,
        .next_out = (Bytef*)output,
    };
    deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);
    deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    return zs.total_out;
}

size_t compress_body(char* request, int accept_encoding)
{
    size_t new_size;

    // insert header
    {
        char append_str[] = "Content-Encoding: gzip\r\n";

        char* p_header_end = strstr(request, "\r\n\r\n") + 2; // to insert location
        size_t body_len = strlen(request) - (p_header_end - request);
        char tmp_str[body_len + 1];

        // insert Content-Encoding
        strcpy(tmp_str, p_header_end);
        strcpy(p_header_end, append_str);
        strcpy(p_header_end + strlen(append_str), tmp_str);
        printf("[INFO][compress_body] HTTP request with Content-Encoding:\n%s<end>\n", request);
    }

    // compression
    {
        char* p_body_start = strstr(request, "\r\n\r\n") + 4;
        size_t body_start_off = p_body_start - request;
        size_t body_len = strlen(p_body_start) - body_start_off;

        char request_header[BUFFER_SIZE + 1];
        char compressed_body[BUFFER_SIZE - body_start_off + 1];
        strncpy(request_header, request, body_start_off); // copy from beg to body

        printf("[INFO][compress_body] HTTP body before compression <length=%lu>\n", strlen(p_body_start));
        size_t compressed_size;
        if (accept_encoding & ENCODING_TYPE_GZIP) {
            compressed_size = compress_to_gzip(p_body_start, strlen(p_body_start), compressed_body, (BUFFER_SIZE - body_start_off));
        }
        printf("[INFO][compress_body] HTTP body after compression  <length=%lu>\n", compressed_size);

        // put Content-Length
        sprintf(request, request_header, compressed_size);
        printf("[INFO][compress_body] Append Content-Encoding to response:\n%s<end>\n", request);

        // copy compressed body back to send buffer
        p_body_start = strstr(request, "\r\n\r\n") + 4;
        body_start_off = p_body_start - request;
        memset(p_body_start, 0, BUFFER_SIZE - body_start_off);
        memcpy(p_body_start, compressed_body, compressed_size);
        printf("[INFO][compress_body] finished copy compressed body\n");

        new_size = body_start_off + compressed_size;
    }

    return new_size;
}

/*** header parser ***/

#define MATCH_STRING(_str) strncmp(p_type_beg, _str, p_type_end - p_type_beg) == 0

int parse_header(char const* const header_beg, char** end_ptr, headerData* data)
{
    char const* p_line_beg = header_beg;

    while (*p_line_beg != '\r' && *p_line_beg != '\n') {
        char const* const p_line_end = strstr(p_line_beg, "\r\n");
        char const* const p_type_beg = p_line_beg;
        char const* const p_type_end = strchr(p_type_beg, ':');

        p_line_beg = p_type_end + 2; // skip ": "

        if (MATCH_STRING("Host")) {
            data->host = malloc(sizeof(char) * (p_line_end - p_line_beg + 1));
            memcpy(data->host, p_line_beg, (p_line_end - p_line_beg));
            data->host[p_line_end - p_line_beg] = '\0';
            printf("data->host = |%s|\n", data->host);
        } else if (MATCH_STRING("User-Agent")) {
            data->user_agent = malloc(sizeof(char) * (p_line_end - p_line_beg + 1));
            memcpy(data->user_agent, p_line_beg, (p_line_end - p_line_beg));
            data->user_agent[p_line_end - p_line_beg] = '\0';
            printf("data->user_agent = |%s|\n", data->user_agent);
        } else if (MATCH_STRING("Accept")) {
            data->accept = malloc(sizeof(char) * (p_line_end - p_line_beg + 1));
            memcpy(data->accept, p_line_beg, (p_line_end - p_line_beg));
            data->accept[p_line_end - p_line_beg] = '\0';
            printf("data->accept = |%s|\n", data->accept);
        } else if (MATCH_STRING("Accept-Encoding")) {
            char tmp_str[p_line_end - p_line_beg + 1];
            memcpy(tmp_str, p_line_beg, (p_line_end - p_line_beg));
            tmp_str[p_line_end - p_line_beg] = '\0';
            char* token = strtok(tmp_str, ", ");
            printf("data->accept_encoding = ");
            while (token != NULL) {
                if (strcmp(token, "gzip") == 0) {
                    data->accept_encoding |= ENCODING_TYPE_GZIP;
                    printf("ENCODING_TYPE_GZIP | ");
                }
                token = strtok(NULL, ", ");
            }
            if (data->accept_encoding == ENCODING_TYPE_UNDEF) {
                printf("ENCODING_TYPE_UNDEF");
            }
            printf("\n");
        } else if (MATCH_STRING("Content-Type")) {
            data->content_type = malloc(sizeof(char) * (p_line_end - p_line_beg + 1));
            memcpy(data->content_type, p_line_beg, (p_line_end - p_line_beg));
            data->content_type[p_line_end - p_line_beg] = '\0';
            printf("data->content_type = |%s|\n", data->content_type);
        } else if (MATCH_STRING("Content-Length")) {
            int tmp_errno = errno;
            errno = 0;
            char* ptr;
            data->content_length = strtoul(p_line_beg, &ptr, 10);
            if (errno != 0) {
                printf("[ERROR][parse_header] content_length error: %s\n", strerror(errno));
                errno = tmp_errno;
                return -1;
            }
            errno = tmp_errno;
            printf("data->content_length = |%lu|\n", data->content_length);
        } else {
            char tstr[32];
            size_t tstr_len = (p_line_end - p_line_beg > 31) ? 31 : p_line_end - p_line_beg;
            memcpy(tstr, p_line_beg, tstr_len);
            tstr[tstr_len] = '\0';
            printf("[WARNING][parse_header] unhandled header type: %s\n", tstr);
        }

        p_line_beg = p_line_end + 2;
    }
    *end_ptr = (char*)p_line_beg;
    return 0;
}

#undef MATCH_STRING

/* (*unsafe) get alloc copy of request */
headerData* parse_request(char const* const request)
{
    headerData* data = malloc(sizeof(headerData));
    if (data == NULL) {
        puts("[ERROR][parse_request] malloc for headerData failed!");
        return NULL;
    }
    *data = (headerData) { 0 };

    char const* p_beg = request;
    /* req_type */
    {
        char const* const p_end = strchr(p_beg, ' ');
        if (p_end == NULL) {
            puts("[ERROR][parse_request] can't locate req_type");
            goto HANDLE_ERROR;
        }
        if (strncmp(p_beg, "GET", p_end - p_beg) == 0) {
            data->req_type = REQ_TYPE_GET;
            puts("data->req_type = REQ_TYPE_GET");
        } else if (strncmp(p_beg, "POST", p_end - p_beg) == 0) {
            data->req_type = REQ_TYPE_POST;
            puts("data->req_type = REQ_TYPE_POST");
        } else {
            puts("[WARNING][parse_request] undefined REQ_TYPE_POST!");
        }
        p_beg = p_end + 1;
    }
    /* request */
    {
        char const* const p_end = strchr(p_beg, ' ');
        if (p_end == NULL) {
            puts("[ERROR][parse_request] can't locate request");
            goto HANDLE_ERROR;
        }
        data->request = malloc(sizeof(char) * (p_end - p_beg + 1));
        memcpy(data->request, p_beg, p_end - p_beg);
        data->request[p_end - p_beg] = '\0';
        printf("data->request = |%s|\n", data->request);
        p_beg = p_end + 1;
    }
    /* http_ver */
    {
        char const* const p_end = strstr(p_beg, "\r\n");
        if (p_end == NULL) {
            puts("[ERROR][parse_request] can't locate http_ver");
            goto HANDLE_ERROR;
        }
        if (strncmp(p_beg, "HTTP/1.1", strlen("HTTP/1.1")) == 0) {
            data->http_ver = HTTP_V11;
        } else {
            puts("[ERROR][parse_request] http version undefined!");
            data->http_ver = HTTP_VUNDEF;
        }
        p_beg = p_end + 2;
    }
    /* headers */
    {
        char* end_ptr = NULL;
        if (parse_header(p_beg, &end_ptr, data) != 0) {
            goto HANDLE_ERROR;
        }
        p_beg = end_ptr;
    }
    /* body */
    {
        char const* const p_body_beg = strstr(request, "\r\n\r\n") + strlen("\r\n\r\n");
        data->body = malloc(sizeof(char) * (strlen(p_body_beg) + 1));
        memcpy(data->body, p_body_beg, strlen(p_body_beg));
        data->body[strlen(p_body_beg)] = '\0';
        printf("data->body: \n%s<end>\n\n", data->body);
    }

    return data;

HANDLE_ERROR:
    free_header_data(data);
    return NULL;
}

/*** threads ***/

/* pthread func that handle one client request */
void* handle_connection(void* p_tparams)
{
    int client_fd = ((tParams*)p_tparams)->client_fd;

    while (1) {
        char sz_recv_buf[BUFFER_SIZE + 1];
        char sz_send_buf[BUFFER_SIZE + 1];
        char const* sz_send_message = reply_404;

        ssize_t const recv_numbytes = recv(client_fd, sz_recv_buf, BUFFER_SIZE, 0);
        if (recv_numbytes == -1) {
            printf("Recv failed: %s \n", strerror(errno));
            break;
        } else if (recv_numbytes == 0) {
            printf("Connection %d closed\n", client_fd);
            break;
        } else {
            printf("Received message success:\n"
                   "<length=%ld>\n"
                   "/***content-beg***/\n"
                   "%s<end>\n"
                   "/***content-end***/\n",
                recv_numbytes, sz_recv_buf);
        }

        headerData* palloc_hd = parse_request(sz_recv_buf);
        if (palloc_hd != NULL) {

            char sz_content_length[30] = "\%lu";
            int b_need_compress = 0;
            if (palloc_hd->accept_encoding != ENCODING_TYPE_UNDEF) {
                b_need_compress = 1;
            }

            if (palloc_hd->req_type == REQ_TYPE_GET) {
                /* GET */
                if (strcmp(palloc_hd->request, REQ_USER_AGENT) == 0) {
                    if (!b_need_compress) {
                        sprintf(sz_content_length, "%lu", strlen(palloc_hd->user_agent));
                    }
                    fill_fmt_reply_200(sz_send_buf, "text/plain", sz_content_length, "", palloc_hd->user_agent);
                    sz_send_message = sz_send_buf;
                } else if (strncmp(palloc_hd->request, REQ_FILE, strlen(REQ_FILE)) == 0) {
                    if (g_args.file_path == NULL) {
                        printf("[ERROR][REQ_GET_FILE]: target files requires path arguments '--directory'\n");
                        sz_send_message = reply_404;
                    } else {
                        char const* const p_beg = palloc_hd->request + strlen(REQ_FILE);
                        LOCAL_STR_COPY(p_beg, file_name);
                        LOCAL_STR_CONCAT(g_args.file_path, file_name, sz_full_path);
                        printf("[INFO][REQ_GET_FILE] load from file full path: %s\n", sz_full_path);
                        size_t buf_size;
                        if (file_exists(sz_full_path) && (buf_size = file_size(sz_full_path)) != (size_t)-1) {
                            char sz_temp_buf[buf_size + 1];
                            read_file(sz_full_path, sz_temp_buf, buf_size);
                            sz_temp_buf[buf_size] = '\0';
                            printf("=== read content: ===\n%s\n=====================\n", sz_temp_buf);

                            if (!b_need_compress) {
                                sprintf(sz_content_length, "%lu", strlen(sz_temp_buf));
                            }
                            fill_fmt_reply_200(sz_send_buf, "application/octet-stream", sz_content_length, "", sz_temp_buf);
                            sz_send_message = sz_send_buf;
                        } else {
                            printf("[ERROR][REQ_GET_FILE]: file `%s` doesn't exists\n", file_name);
                            sz_send_message = reply_404;
                        }
                    }
                } else if (strncmp(palloc_hd->request, REQ_ECHO, strlen(REQ_ECHO)) == 0) {
                    char const* const sz_echo_str = palloc_hd->request + strlen(REQ_ECHO);
                    if (!b_need_compress) {
                        sprintf(sz_content_length, "%lu", strlen(sz_echo_str));
                    }
                    fill_fmt_reply_200(sz_send_buf, "text/plain", sz_content_length, "", sz_echo_str);
                    sz_send_message = sz_send_buf;
                } else if (strcmp(palloc_hd->request, REQ_ROOT) == 0) {
                    sz_send_message = reply_200;
                } else {
                    sz_send_message = reply_404;
                }
            } else if (palloc_hd->req_type == REQ_TYPE_POST) {
                /* POST */
                if (strncmp(palloc_hd->request, REQ_FILE, strlen(REQ_FILE)) == 0) {
                    if (strncmp(palloc_hd->content_type, "application/octet-stream", strlen("application/octet-stream")) == 0) {
                        /* request write to file */
                        if (g_args.file_path == NULL) {
                            printf("[ERROR][REQ_POST_FILE]: post files requires path arguments '--directory'\n");
                            sz_send_message = reply_404;
                        } else {
                            char const* const p_beg = palloc_hd->request + strlen(REQ_FILE);
                            LOCAL_STR_COPY(p_beg, file_name);
                            LOCAL_STR_CONCAT(g_args.file_path, file_name, sz_full_path);
                            printf("[INFO][REQ_POST_FILE]: target file full path: %s\ncontent: \n|%s|\n", sz_full_path, palloc_hd->body);
                            write_file(sz_full_path, palloc_hd->body);
                            sz_send_message = reply_201;
                        }
                    } else {
                        sz_send_message = reply_404;
                    }
                } else {
                    sz_send_message = reply_404;
                }
            } else {
                puts("[ERROR]: Request type undefined");
                sz_send_message = reply_404;
            }

            // http compression
            if (b_need_compress) {
                // only if send message has body
                size_t new_size = strlen(palloc_hd->body);
                if (sz_send_message == sz_send_buf) {
                    new_size = compress_body(sz_send_buf, palloc_hd->accept_encoding);
                    printf("[INFO] sz_send_message is:\n%s<end>\n", sz_send_message);
                }

                // send
                const int sent_numbytes = send(client_fd, sz_send_message, new_size, 0);
                if (sent_numbytes == -1) {
                    printf("Send failed: %s \n", strerror(errno));
                } else {
                    printf("Send message success:\n"
                           "/***content-beg***/\n"
                           "%s<end>\n"
                           "/***content-end***/\n",
                        sz_send_message);
                }
            } else {
                // dont' need compression
                const int sent_numbytes = send(client_fd, sz_send_message, strlen(sz_send_message), 0);
                if (sent_numbytes == -1) {
                    printf("Send failed: %s \n", strerror(errno));
                } else {
                    printf("Send message success:\n"
                           "/***content-beg***/\n"
                           "%s<end>\n"
                           "/***content-end***/\n",
                        sz_send_message);
                }
            }
        }
        free_header_data(palloc_hd);
    }

    close(client_fd);
    free(p_tparams);

    pthread_exit(NULL);
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
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

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

    // create pthread

    while (1) {
        if ((client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len)) == -1) {
            printf("Create client failed: %s \n", strerror(errno));
            break;
        } else {
            tParams* p_tparams = malloc(sizeof(tParams));
            p_tparams->client_fd = client_fd;

            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, handle_connection, (void*)p_tparams) != 0) {
                perror("Tread creation failed\n");
                close(client_fd);
                free(p_tparams);
                continue;
            }
            printf("Client %d connected\n", client_fd);
            pthread_detach(thread_id);
        }
    }

    close(server_fd);

    g_free_resource();
    exit(EXIT_SUCCESS);

SAFE_RETURN:
    close(server_fd);

    g_free_resource();
    exit(EXIT_FAILURE);
}
