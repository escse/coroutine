#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <fcntl.h>
#include <cstring>

#include <iostream>

#include "coroutine.h"

using namespace coroutine;

int start(int port) {
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    int addrlen = sizeof(address);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    bind(fd, (struct sockaddr *)&address, sizeof(address));
    return fd;
}


void echo(void *data) {
    int sock = *(int*) data;
    char buffer[1024] = {0}; 
    const char *hello = "Hello from server\n";
    const char *bye = "GoodBye from server\n";
    printf("start connection with %d\n", sock);
    send(sock, hello, strlen(hello), 0);
    while (1) {
        int len = co_recv(sock , buffer, 1024, 0); 
        if (len <= 0) break;
        buffer[len] = 0;
        if (strcmp(buffer, "quit\n") == 0) {
            printf("quit received from %d\n", sock);
            send(sock, bye, strlen(bye), 0);
            break; 
        }
        printf("receive from %d : %s", sock, buffer);
        send(sock, buffer, strlen(buffer), 0);
    }
    printf("close connection with %d\n", sock);
    close(sock);
}

void server(void *params) {
    int port = *(int *)params;
    char buf[1024];
    int listens = start(port);
    listen(listens, 3);
    while (1) {
        struct sockaddr_in address;
        bzero(&address, sizeof(address));
        int addrlen;
        int client_s = co_accept(listens, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        go(echo,  new int(client_s));
    }
}


int main(int argc, const char** argv) {
    int port = 8080;
    if (argc > 1) port = atoi(argv[1]);
    go(server, new int(port));
    Schedule::instance()->run();
    return 0;
}