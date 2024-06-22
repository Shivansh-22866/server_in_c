#include "proxy_parse.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#define MAX_CLIENTS 10
#define MAX_BYTES 4096

struct cache_element {
    char* data;
    int len;
    char* url;
    time_t lru_time_track;
    struct cache_element* next;
};

struct cache_element* find(char* url);
int add_cache_element(char* data, int size, char* url);
void remove_cache_element();

int port_number = 8080;
int proxy_socket_id;

pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock; 

struct cache_element* head;
int cache_size;

int connectRemoteServer(char* host, int port_num) {
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(remoteSocket<0) {
        printf("Socket creation failed\n");
        return -1;
    }

    struct hostent* host = gethostByName(host_addr);
    if(host == NULL) {
        fprintf(stderr, "Host not found\n");
        return -1;
    }

    struct sockaddr_in server_addr;
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);

    bcopy((char *)&host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);
    if(connect(remoteSocket, (struct sockaddr *)&server_addr, (size_t)sizeof(server_addr) < 0)) {
        fprintf(stderr, "Error in connecting \n");
        return -1;
    }

    return remoteSocket;
}

int handle_request(int clientSocketId, ParsedRequest *request, char* tempReq) {
    char *buff = (char *)malloc(sizeof(char) * MAX_BYTES);
    strcpy(buff, "GET ");
    strcpy(buff, request->path);
    strcat(buff, " ");
    strcat(buff, request->version);
    strcat(buff, "\r\n");
    size_t len = strlen(buff);

    if(ParsedHeader_set(request, "Connection", "close") < 0) {
        printf("Set connection header key is not working");
    }

    if(ParsedHeader_get(request, "Host") == NULL) {
        if(ParsedHeader_set(request, "Host", request->host) < 0) {
            printf("Set host header key is not working");
        }
    }

    if(ParsedRequest_unparse_headers(request, buff->len, (size_t)MAX_BYTES-len)<0) {
        printf("Unparse headers is not working");
    }

    int server_port = 80;
    if(request->port != NULL) {
        server_port = atoi(request->port);
    }
    int remoteSocketId = connectRemoteServer(request->host, server_port);
    if(remoteSocket<0) {
        return -1;
    }

    int bytes_send = send(remoteSocketId, buff, strlen(buff), 0);
    bzero(buff, MAX_BYTES);

    bytes_send = recv(remoteSocketId, buff, MAX_BYTES-1, 0);
    char * temp_buffer =(char *)malloc(sizeof(char)*MAX_BYTES);
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    while(bytes_send > 0) {
        bytes_send = send(clientSocketId, buff, bytes_send, 0);
        for(int i=0;i<bytes_send/sizeof(char);i++) {
            temp_buffer[temp_buffer_index] = buff[i];
            temp_buffer_index++;
        }
        temp_buffer_size += MAX_BYTES;
        temp_buffer = (char*)realloc(temp_buffer, temp_buffer_size);
        if(bytes_send < 0) {
            perror("Error in sending data to the client\n");
            break;
        }
        bzero(buff, MAX_BYTES);
        bytes_send = recv(remoteSocketId, buff, MAX_BYTES-1, 0);
    }
    temp_buffer[temp_buffer_index] = '\0';
    free(buff);
    add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
    free(temp_buffer);
    close(remoteSocketId);
    return 0;

}

int checkHTTPversion(char* msg) {
    int version = -1;

    if(strcmp(msg, "HTTP/1.1", 8) == 0) {
        version - 1;
    }

    else if(strcmp(msg, "HTTP/1.0", 8) == 0) {
        version = 1;
    }

    else {
        version = -1;
    }

    return version;
}

int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

	switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: MyServerN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
				  printf("400 Bad Request\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: MyServerN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  printf("403 Forbidden\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: MyServerN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  printf("404 Not Found\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: MyServerN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  //printf("500 Internal Server Error\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: MyServerN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  printf("501 Not Implemented\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: MyServerN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  printf("505 HTTP Version Not Supported\n");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}

void* thread_fn(void* socketNew) {
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, p);
    printf("Semaphore value is: %d\n", p);

    int *t = (int*) socketNew;
    int socket = *t;
    int bytes_send_client, len;

    char *buffer = (char*)malloc(MAX_BYTES, sizeof(char));
    bzero(buffer, MAX_BYTES);
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);

    while(bytes_send_client > 0) {
        len = strlen(buffer);
        if(substr(buffer, "\r\n\r\n") == NULL) {
            bytes_send_client = recv(socket, buffer+len, MAX_BYTES+len, 0);
        } else {
            break;
        }
    }

    char *tempReq = (char *)malloc(strlen(buffer)*sizeof(char)+1);
    for(int i=0;i<strlen(buffer);i++) {
        tempReq[i] = buffer[i];
    }

    struct cache_element* temp = find(tempReq);
    if(temp != NULL) {
        int size = temp->len/sizeof(char);
        int pos = 0;
        char response[MAX_BYTES];
        while(pos<size) {
            bzero(response, MAX_BYTES);
            for(int i=0;i<MAX_BYTES;i++) {
                response[i] = temp->data[i];
                pos++;
            }
            send(socket, response, MAX_BYTES, 0);
        }
        printf("Data retrieved from cache \n");
        printf("%s\n\n", response);
    }
    else if(bytes_send_client > 0) {
        len = strlen(buffer);
        ParsedRequest *request = ParsedRequest_create();

        if(ParsedRequest_parse(request, buffer, len) < 0) {
            printf("parsing failed \n");
        } else {
            bzero(buffer, MAX_BYTES);
            if(!strcmp(request->method, "GET")) {
                if(request->host && request->path && checkHTTPversion(request->version)==1) {
                    bytes_send_client = handle_request(socket, request, tempReq);
                    if(bytes_send_client == -1) {
                        sendErrorMessage(socket, 500);
                    }
                }
                else {
                    sendErrorMessage(socket, 500);
                }
            }
            else {
                printf("This code only supports GET requests");
            }
        }
        ParsedRequest_destroy(request);
    } else if(bytes_send_client == 0) {
        printf("Client disconnected \n");
    }
    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    sem_post(&semaphore);
    sem_getvalue(&semaphore, p);
    printf("Semaphore post value is: %d\n", p);
    free(tempReq);
    return NULL;
}

int main(int argc, char* argv[]) {
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;
    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);
    if(argc == 2) {
        port_number = atoi(argv[1]);
    }
    else {
        printf("Too few args");
        exit(1);
    }

    printf("Starting proxy at port: %d\n", port_number);
    proxy_socket_id = socket(AF_INET, SOCK_STREAM, 0);
    if(proxy_socket_id < 0) {
        perror("Failed to create socket\n");
        exit(1);
    }

    int reuse = 1;
    if(setsockopt(proxy_socket_id, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse))<0) {
        perror("SetSockOpt failed \n");
        exit(1);
    }

    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if(bind(proxy_socket_id, (struct sockaddr*)&server_addr, sizeof(server_addr))<0) {
        perror("Bind failed\n");
        exit(1);
    }
    printf("Binding on port %d \n", port_number);
    int listen_status = listen(proxy_socket_id, MAX_CLIENTS);
    if(listen_status < 0) {
        perror("Listen failed\n");
        exit(1);
    }

    int i = 0;
    int Connected_socketId[MAX_CLIENTS];

    while(1) {
        bzero((char*)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);
        client_socketId = accept(proxy_socket_id, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
        if(client_socketId<0) {
            printf("Not able to connect to client");
            exit(1);
        }

        else {
            Connected_socketId[i] = client_socketId;
        }

        struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        printf("Client is connect to port %d with IP address %s\n", ntohs(client_addr.sin_port), str);

        pthread_create(&tid[i], NULL, thread_fn, (void *)&Connected_socketId[i]);
        i++;
    }
    close(proxy_socket_id);
    return 0;
} 