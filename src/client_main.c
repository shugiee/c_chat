#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <protocol.h>

// TODO: clear input on submit

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 18000
#define BUFFER_SIZE 1024

void make_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
};

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
        const char *msg = "Client exiting";
        send_packet(sockfd, 99, msg);
        printf("Sent disconnect message to server\n");
        close(sockfd);
    }

    exit(0);
};

int main(void) {
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

    make_nonblocking(sockfd);
    make_nonblocking(STDIN_FILENO);

    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    bool has_registered = false;

    printf("Client connected to server at %s:%d\n", SERVER_IP, SERVER_PORT);

    // Send message
    while (1) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            perror("poll");
            break;
        }

        // Register
        if (fds[0].revents & POLLIN) {
            ssize_t n =
                read(STDIN_FILENO, write_buffer, sizeof(write_buffer) - 1);

            if (n > 0) {
                // Terminal uses enter to submit; don't include newline it
                // causes!
                if (write_buffer[n - 1] == '\n')
                    write_buffer[n - 1] = '\0';
                else
                    write_buffer[n] = '\0';

                // TODO: split these up; one method for registration, another
                // for chat messages
                int msg_type = has_registered ? MSG_CHAT : MSG_SET_NAME;
                send_packet(sockfd, msg_type, write_buffer);
                if (!has_registered)
                    has_registered = true;
            };
        };

        // Receive message
        if (fds[1].revents & POLLIN) {
            ssize_t n = recv(sockfd, read_buffer, sizeof(read_buffer) - 1, 0);
            if (n <= 0) {
                printf("Server disconnected\n");
                break;
            }

            read_buffer[n] = '\0';
            printf("%s\n", read_buffer);
            fflush(stdout);
        };
    }
    close(sockfd);
    return 0;
};
