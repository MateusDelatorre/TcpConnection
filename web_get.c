//importing Berkeley sockets APIS headres11
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>//thread for storing erros


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#define ISVALIDSOCKET(s) ((s) >= 0)
#define CLOSESOCKET(s) close(s)
#define SOCKET int
#define GETSOCKETERRNO() (errno)

#define TIMEOUT 5.0

void parse_url(char *url, char **hostname, char **port, char** path) {
    printf("URL: %s\n", url);

    char *p;
    p = strstr(url, "://");//tries to get the protocol

    char *protocol = 0;
    if (p) {
        protocol = url;
        *p = 0;
        p += 3;
    } else {
        p = url;
    }

    //verify if the protocol is right
    if (protocol) {
        if (strcmp(protocol, "http")) {
            fprintf(stderr,
                    "Unknown protocol '%s'. Only 'http' is supported.\n",
                    protocol);
            exit(1);
        }
    }

    //goes to the hostname, in some magic way
    *hostname = p;
    while (*p && *p != ':' && *p != '/' && *p != '#') ++p;

    *port = "80";
    if (*p == ':') {
        *p++ = 0;
        *port = p;
    }
    while (*p && *p != '/' && *p != '#') ++p;

    *path = p;
    if (*p == '/') {
        *path = p + 1;
    }
    *p = 0;

    ++p;
    while (*p && *p != '#') ++p;
    if (*p == '#') *p = 0;

    printf("hostname: %s\n", *hostname);
    printf("port: %s\n", *port);
    printf("path: %s\n", *path);
}


void send_request(SOCKET s, char *hostname, char *port, char *path) {
    char buffer[2048];
    //print the string on screen and save it to buffer
    sprintf(buffer, "GET /%s HTTP/1.1\r\n", path);
    sprintf(buffer + strlen(buffer), "Host: %s:%s\r\n", hostname, port);
    sprintf(buffer + strlen(buffer), "Connection: close\r\n");
    sprintf(buffer + strlen(buffer), "User-Agent: honpwc web_get 1.0\r\n");
    sprintf(buffer + strlen(buffer), "\r\n");

    send(s, buffer, strlen(buffer), 0);//send data using the s socket
    printf("Sent Headers:\n%s", buffer);
}


SOCKET connect_to_host(char *hostname, char *port) {
    printf("Configuring remote address...\n");
    struct addrinfo hints; //struct with infos about a hostname
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;//tcp ip socket
    struct addrinfo *peer_address;//server info
    //use DNS to get info about server
    if (getaddrinfo(hostname, port, &hints, &peer_address)) {
        fprintf(stderr, "getaddrinfo() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    printf("Remote address is: ");
    char address_buffer[100];
    char service_buffer[100];
    getnameinfo(peer_address->ai_addr, peer_address->ai_addrlen, address_buffer, sizeof(address_buffer), service_buffer, sizeof(service_buffer), NI_NUMERICHOST);//get info about server
    printf("%s %s\n", address_buffer, service_buffer);

    printf("Creating socket...\n");
    SOCKET server;
    server = socket(peer_address->ai_family, peer_address->ai_socktype, peer_address->ai_protocol);//creates and initializes a new socket
    if (!ISVALIDSOCKET(server)) {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    printf("Connecting...\n");
    //the connect funcion is used to set the remote address, port and  establishes a connection.
    if (connect(server, peer_address->ai_addr, peer_address->ai_addrlen)) {
        fprintf(stderr, "connect() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }
    freeaddrinfo(peer_address);//free the memory

    printf("Connected.\n\n");

    return server;
}



int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: web_get url\n");
        return 1;
    }
    char *url = argv[1];

    char *hostname, *port, *path;
    parse_url(url, &hostname, &port, &path);

    SOCKET server = connect_to_host(hostname, port);
    send_request(server, hostname, port, path);

    const clock_t start_time = clock();//system clock, used to define time

    #define RESPONSE_SIZE 32768
    char response[RESPONSE_SIZE+1];
    char *p = response, *q;
    char *end = response + RESPONSE_SIZE;
    char *body = 0;

    enum {length, chunked, connection};
    int encoding = 0;
    int remaining = 0;

    while(1) {

        //Checks for timeout
        if ((clock() - start_time) / CLOCKS_PER_SEC > TIMEOUT) {
            fprintf(stderr, "timeout after %.2f seconds\n", TIMEOUT);
            return 1;
        }

        //check if the response is null
        if (p == end) {
            fprintf(stderr, "out of buffer space\n");
            return 1;
        }

        //data to use on tcp socket
        fd_set reads;
        FD_ZERO(&reads);
        FD_SET(server, &reads);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;

        //get data from server
        if (select(server+1, &reads, 0, 0, &timeout) < 0) {
            fprintf(stderr, "select() failed. (%d)\n", GETSOCKETERRNO());
            return 1;
        }

        //see if it is new data
        if (FD_ISSET(server, &reads)) {
            int bytes_received = recv(server, p, end - p, 0);//get data received on the server socket
            //check if the data was a close connection
            if (bytes_received < 1) {
                if (encoding == connection && body) {
                    printf("%.*s", (int)(end - body), body);
                }

                printf("\nConnection closed by peer.\n");
                break;
            }

            p += bytes_received;
            *p = 0;

            //prints the HTTP response
            if (!body && (body = strstr(response, "\r\n\r\n"))) {
                *body = 0;
                body += 4;

                printf("Received Headers:\n%s\n", response);//print the entire header

                q = strstr(response, "\nContent-Length: ");
                //check the lenght type
                if (q) {
                    encoding = length;
                    q = strchr(q, ' ');
                    q += 1;
                    remaining = strtol(q, 0, 10);

                } else {
                    q = strstr(response, "\nTransfer-Encoding: chunked");
                    if (q) {
                        encoding = chunked;
                        remaining = 0;
                    } else {
                        encoding = connection;
                    }
                }
                printf("\nReceived Body:\n");
            }

            //print the body
            if (body) {
                //check the lenght type
                if (encoding == length) {
                    if (p - body >= remaining) {
                        printf("%.*s", remaining, body);
                        break;
                    }
                } else if (encoding == chunked) {
                    //
                    do {
                        //Waiting to new chunk length
                        if (remaining == 0) {
                            //chunk length finish
                            if ((q = strstr(body, "\r\n"))) {
                                remaining = strtol(body, 0, 16);//get the length
                                if (!remaining) goto finish;//chunk end with 0
                                body = q + 2;
                            } else {
                                break;
                            }
                        }
                        //check if there is remaining chunk
                        if (remaining && p - body >= remaining) {
                            printf("%.*s", remaining, body);
                            body += remaining + 2;
                            remaining = 0;
                        }
                    } while (!remaining);
                }
            } //if (body)
        } //if FDSET
    } //end while(1)
finish:

    printf("\nClosing socket...\n");
    close(server);//terminate the TCP connection and close the socket

    printf("Finished.\n");
    return 0;
}

