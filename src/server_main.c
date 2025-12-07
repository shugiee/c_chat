#include <arpa/inet.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <protocol.h>

#define PORT 18000
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

typedef struct {
    int fd;
    char *name;
} User;

void recv_packet(int sockfd, struct pollfd fds[MAX_CLIENTS + 1],
                 User users[MAX_CLIENTS + 1], int i) {
    MessageHeader hdr;
    if (recv(sockfd, &hdr, sizeof(hdr), MSG_WAITALL) <= 0) {
        close(sockfd);
        fds[i].fd = -1;
        return;
    }

    hdr.length = ntohl(hdr.length); // convert network to local

    char *body = malloc(hdr.length + 1);
    if (!body)
        return;

    if (recv(sockfd, body, hdr.length, MSG_WAITALL) <= 0) {
        free(body);
        return;
    }
    body[hdr.length] = '\0';

    switch (hdr.msg_type) {
    case 1:
        users[i].name = body;
        printf("User %d set name to %s\n", i, users[i].name);
        break;
    case 2:
        printf("CHAT_MSG: %s\n", body);
        break;
    default:
        printf("Unknown type %d\n", hdr.msg_type);
    }

    free(body);
}

int main(void) {
    User users[MAX_CLIENTS + 1] = {0};

    int listen_fd, new_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Create listening socket
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    // Reuse address/port
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
        0) {
        perror("bind");
        close(listen_fd);
        exit(1);
    }

    // Listen
    if (listen(listen_fd, 5) < 0) {
        perror("listen");
        close(listen_fd);
        exit(1);
    }

    printf("Server listening on port %d\n", PORT);

    // Prepare poll fds
    struct pollfd fds[MAX_CLIENTS + 1];
    fds[0].fd = listen_fd;
    // Tell poll that we care about events that let us read
    fds[0].events = POLLIN; // Ready to accept connections

    // Initialize all of the client fds to -1
    for (int i = 1; i <= MAX_CLIENTS; i++) {
        fds[i].fd = -1;
    }

    // Main loop
    while (1) {
        int activity = poll(fds, MAX_CLIENTS + 1, -1);
        if (activity < 0) {
            perror("poll");
            break;
        }

        // Register new connections
        if (fds[0].revents & POLLIN) {
            new_fd =
                accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            if (new_fd < 0) {
                perror("accept");
                continue;
            }

            // Get their name
            char *ask_for_name = "Hi there and welcome. What's your name?\n";
            send(new_fd, ask_for_name, strlen(ask_for_name), 0);

            // Tell other users that someone joined
            for (int i = 1; i <= MAX_CLIENTS; i++) {
                int fd = fds[i].fd;
                if (fd < 0) {
                    continue;
                }

                const char *join_msg = "New client connected";
                send(fd, join_msg, strlen(join_msg), 0);
            }

            // Register user
            for (int i = 1; i <= MAX_CLIENTS; i++) {
                if (fds[i].fd < 0) {
                    fds[i].fd = new_fd;
                    fds[i].events = POLLIN;

                    // Track the user
                    users[i].fd = new_fd;
                    users[i].name = NULL;

                    break;
                }
            }
        }

        // Handle incoming messages
        for (int i = 1; i <= MAX_CLIENTS; i++) {
            int fd = fds[i].fd;

            // If there's no client
            if (fd < 0)
                continue;

            if (fds[i].revents & POLLIN) {
                recv_packet(fd, fds, users, i);
            }
        }
    }

    close(listen_fd);
    return 0;
}
