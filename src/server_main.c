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

void remove_user(int user_idx, User users[MAX_CLIENTS + 1]) {
    memset(&users[user_idx], 0, sizeof(User));
};

int broadcast_msg(struct pollfd *fds, int user_idx, char *msg) {
    for (int i = 1; i <= MAX_CLIENTS; i++) {
        int fd = fds[i].fd;

        if (i == user_idx || fd < 0) {
            continue;
        }

        send(fd, msg, strlen(msg), 0);
        break;
    }
    return 0;
};

int recv_packet(int sockfd, struct pollfd fds[MAX_CLIENTS + 1],
                User users[MAX_CLIENTS + 1], int sender_idx) {
    MessageHeader hdr;
    // TODO: this isn't logging when user disconnects
    if (recv(sockfd, &hdr, sizeof(hdr), MSG_WAITALL) <= 0) {
        // Tell other users that someone left
        // TODO: use broadcast_msg
        for (int i = 1; i <= MAX_CLIENTS; i++) {
            int fd = fds[i].fd;
            printf("Trying to send 'left' message to %d", i);

            if (fd < 0) {
                continue;
            }

            int len = snprintf(NULL, 0, "%s left", users[sender_idx].name);
            char *leave_msg = malloc(len + 1);
            if (!leave_msg)
                return 1;
            snprintf(leave_msg, len + 1, "%s left", users[sender_idx].name);
            send(fd, leave_msg, strlen(leave_msg), 0);
            free(leave_msg);
        }
        close(sockfd);
        fds[sender_idx].fd = -1;
        remove_user(sender_idx, users);
        return 0;
    }

    hdr.length = ntohl(hdr.length); // convert network to local

    char *body = malloc(hdr.length + 1);
    if (!body)
        return 0;

    if (recv(sockfd, body, hdr.length, MSG_WAITALL) <= 0) {
        free(body);
        return 0;
    }

    switch (hdr.msg_type) {
    case MSG_SET_NAME: {
        free(users[sender_idx].name);
        users[sender_idx].name = strdup(body);
        char *template = "%s joined";
        int len = snprintf(NULL, 0, template, body);
        char *msg = malloc(len + 1);
        if (!msg)
            return 1;
        snprintf(msg, len + 1, template, body);
        broadcast_msg(fds, sender_idx, msg);
        free(msg);
        break;
    }
    case MSG_CHAT: {
        // TODO: share this string-building logic in a helper file
        char *template = "Message from %s: %s";
        char *username = users[sender_idx].name;

        int len = snprintf(NULL, 0, template, username, body);
        char *msg = malloc(len + 1);
        if (!msg)
            return 1;
        snprintf(msg, len + 1, template, username, body);
        broadcast_msg(fds, sender_idx, msg);
        free(msg);
        break;
    }
    case MSG_DISCONNECT: {
        printf("%s left\n", body);
        close(sockfd);
        fds[sender_idx].fd = -1;
        remove_user(sender_idx, users);
        break;
    }
    default:
        printf("Unknown type %d\n", hdr.msg_type);
    }

    free(body);
    return 0;
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
            printf("New connection!");
            new_fd =
                accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            if (new_fd < 0) {
                perror("accept");
                continue;
            }

            // Get their name
            char *ask_for_name = "Hi there and welcome. What's your name?\n";
            send(new_fd, ask_for_name, strlen(ask_for_name), 0);

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
