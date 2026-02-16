#include <arpa/inet.h>
#include <libpq-fe.h>
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

int load_history(PGconn *conn, MessageBody **messages) {
    PGresult *res = PQexecParams(
        conn, "SELECT sender, content FROM messages ORDER BY sent_at", 0, NULL,
        NULL, NULL, NULL, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "SELECT failed: %s\n", PQerrorMessage(conn));
        PQclear(res);
        return 1;
    }

    int rows = PQntuples(res);

    MessageBody *results = malloc(rows * sizeof(MessageBody));

    for (int row = 0; row < rows; row++) {
        strncpy(results[row].sender_name, PQgetvalue(res, row, 0),
                sizeof(results[row].sender_name) - 1);
        strncpy(results[row].body, PQgetvalue(res, row, 1),
                sizeof(results[row].body) - 1);
        *messages = results;
    };
    return rows;
}

int send_message_to_user(char *message, char *sender_name, struct pollfd fd) {
    MessageHeader hdr;
    hdr.version = 1;
    hdr.msg_type = MSG_CHAT;
    hdr.flags = 0;
    hdr.length = htonl(sizeof(MessageBody)); // convert to network byte order

    MessageBody body;
    strcpy(body.sender_name, sender_name);
    strcpy(body.body, message);

    send(fd.fd, &hdr, sizeof(hdr), 0);
    send(fd.fd, &body, sizeof(body), 0);

    return 0;
}

int send_history_to_user(struct pollfd *fds, int sender_idx, PGconn *conn) {
    MessageBody *messages;
    int num_messages = load_history(conn, &messages);
    struct pollfd fd = fds[sender_idx];

    for (int i = 0; i < num_messages; i++) {
        send_message_to_user(messages[i].body, messages[i].sender_name, fd);
    }

    return 0;
}

int broadcast_msg(struct pollfd *fds, int sender_idx, MessageHeader *hdr,
                  MessageBody *body) {
    for (int i = 1; i <= MAX_CLIENTS; i++) {
        int fd = fds[i].fd;

        if (i == sender_idx || fd < 0) {
            continue;
        }
        send(fd, hdr, sizeof(*hdr), 0);
        send(fd, body, sizeof(*body), 0);
    }
    return 0;
};

int persist_message(char *message, char *author_name, PGconn *conn) {
    PGresult *res = PQexecParams(
        conn, "INSERT INTO messages (sender, content) VALUES ($1, $2)", 2, NULL,
        (const char *[]){author_name, message}, NULL, NULL, 0);
    PQclear(res);
    return 0;
};

int recv_packet(int sockfd, struct pollfd fds[MAX_CLIENTS + 1],
                User users[MAX_CLIENTS + 1], int sender_idx, PGconn *conn) {
    MessageHeader hdr;
    if (recv(sockfd, &hdr, sizeof(hdr), MSG_WAITALL) <= 0) {
        // Tell other users that someone left; clients assume a body is
        // coming; use an empty one
        MessageBody message_body;
        strcpy(message_body.sender_name, users[sender_idx].name);
        strcpy(message_body.body, "");

        MessageHeader hdr;
        hdr.version = 1;
        hdr.msg_type = MSG_USER_DISCONNECTED;
        hdr.flags = 0;
        hdr.length =
            htonl(sizeof(MessageBody)); // convert to network byte order
        broadcast_msg(fds, sender_idx, &hdr, &message_body);
        close(sockfd);
        fds[sender_idx].fd = -1;
        remove_user(sender_idx, users);
        return 0;
    }

    hdr.length = ntohl(hdr.length); // convert network to local

    MessageBody message_body;
    if (recv(sockfd, &message_body, hdr.length, MSG_WAITALL) <= 0) {
        // TODO: free?
        return 0;
    }

    switch (hdr.msg_type) {
    case MSG_SET_NAME: {
        free(users[sender_idx].name);
        users[sender_idx].name = strdup(message_body.body);

        MessageHeader hdr;
        hdr.version = 1;
        hdr.msg_type = MSG_USER_JOINED;
        hdr.flags = 0;
        hdr.length =
            htonl(sizeof(MessageBody)); // convert to network byte order
        broadcast_msg(fds, sender_idx, &hdr, &message_body);
        send_history_to_user(fds, sender_idx, conn);
        break;
    }
    case MSG_CHAT: {
        MessageHeader hdr;
        hdr.version = 1;
        hdr.msg_type = MSG_CHAT;
        hdr.flags = 0;
        // TODO: use real length
        hdr.length =
            htonl(sizeof(MessageBody)); // convert to network byte order
        // TODO: reuse name
        char *username = strdup(users[sender_idx].name);
        broadcast_msg(fds, sender_idx, &hdr, &message_body);
        free(username);
        persist_message(message_body.body, message_body.sender_name, conn);
        break;
    }
    case MSG_DISCONNECT: {
        MessageHeader hdr;
        hdr.version = 1;
        hdr.msg_type = MSG_USER_DISCONNECTED;
        hdr.flags = 0;
        hdr.length = htonl(sizeof(MessageBody));
        char *username = strdup(users[sender_idx].name);
        broadcast_msg(fds, sender_idx, &hdr, &message_body);
        free(username);
        break;
    }
    default:
        printf("Unknown type %d\n", hdr.msg_type);
    }

    // TODO: free?
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

    // Connect to chat DB
    PGconn *conn = PQconnectdb("host=localhost port=5432 dbname=chatapp "
                               "user=chatuser password=chatpass");
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
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
            MessageHeader hdr;
            hdr.version = 1;
            hdr.msg_type = MSG_ASK_FOR_NAME;
            hdr.flags = 0;
            hdr.length =
                htonl(sizeof(MessageBody)); // convert to network byte order

            MessageBody body;
            strcpy(body.sender_name, "Server");
            strcpy(body.body, ask_for_name);

            send(new_fd, &hdr, sizeof(hdr), 0);
            send(new_fd, &body, sizeof(body), 0);

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
                recv_packet(fd, fds, users, i, conn);
            }
        }
    }

    close(listen_fd);
    return 0;
}
