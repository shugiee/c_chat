#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <protocol.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 18000
#define BUFFER_SIZE 1024

void send_packet(int sockfd, uint8_t type, const char *body) {
    MessageHeader hdr;
    hdr.version = 1;
    hdr.msg_type = type;
    hdr.flags = 0;
    hdr.length = htonl(strlen(body)); // convert to network byte order

    // write header + body
    send(sockfd, &hdr, sizeof(hdr), 0);
    send(sockfd, body, strlen(body), 0);
}

int main(void) {
    int sockfd;
    struct sockaddr_in server_addr;
    char read_buffer[BUFFER_SIZE];
    char write_buffer[BUFFER_SIZE];

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
        0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Client connected to server at %s:%d\n", SERVER_IP, SERVER_PORT);

    // Send message
    while (1) {
        if (!fgets(write_buffer, sizeof(write_buffer), stdin)) {
            break;
        }
        send_packet(sockfd, 1, write_buffer);

        // Receive echo
        int bytes = recv(sockfd, read_buffer, sizeof(read_buffer) - 1, 0);
        if (bytes <= 0) {
            printf("Server closed connection\n");
            break;
        }
        read_buffer[bytes] = '\0';
        printf("Received from server: %s\n", read_buffer);
    }
    return 0;
};
