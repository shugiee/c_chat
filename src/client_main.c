#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <protocol.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 18000
#define BUFFER_SIZE 1024

// Global so that the handle_sigint can use it
int sockfd = -1;

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

void handle_sigint(int sig) {
    (void)sig; // unused

    if (sockfd >= 0) {
        // Send "disconnect" message
        uint8_t type = 99;
        const char *msg = "Client exiting";
        MessageHeader hdr = {1, type, 0, htonl(strlen(msg))};

        send(sockfd, &hdr, sizeof(hdr), 0);
        send(sockfd, &msg, strlen(msg), 0);

        printf("Sent disconnect message to server\n");
        close(sockfd);
    }

    exit(0);
};

int main(void) {
    int sockfd;
    struct sockaddr_in server_addr;
    char read_buffer[BUFFER_SIZE];
    char write_buffer[BUFFER_SIZE];

    // Register disconnect sentinel
    signal(SIGINT, handle_sigint);

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
        // Receive echo
        int bytes = recv(sockfd, read_buffer, sizeof(read_buffer) - 1, 0);
        if (bytes <= 0) {
            printf("Server closed connection\n");
            break;
        }
        read_buffer[bytes] = '\0';
        printf("%s\n", read_buffer);

        if (!fgets(write_buffer, sizeof(write_buffer), stdin)) {
            break;
        }
        send_packet(sockfd, 1, write_buffer);
    }
    return 0;
};
