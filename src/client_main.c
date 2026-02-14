#include <arpa/inet.h>
#include <fcntl.h>
#include <ncurses.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <protocol.h>

typedef struct {
    WINDOW *outer;
    WINDOW *inner;
} BorderedWindow;

BorderedWindow msg_win;   // message thread
BorderedWindow input_win; // fixed bottom line

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 18000
#define BUFFER_SIZE 1024

char sender_name[64];

BorderedWindow make_bordered_window(int rows, int cols, int y, int x) {
    BorderedWindow bw;
    bw.outer = newwin(rows, cols, y, x);
    box(bw.outer, 0, 0); // draw border
    wrefresh(bw.outer);

    // inner window is 2 smaller in each dimension, offset by 1
    bw.inner = derwin(bw.outer, rows - 2, cols - 2, 1, 1);
    return bw;
}

void free_bordered_window(BorderedWindow *bw) {
    delwin(bw->inner);
    delwin(bw->outer);
}

void refresh_bordered_window(BorderedWindow *bw) {
    touchwin(bw->outer);
    wrefresh(bw->inner);
}

void init_ui() {
    initscr();
    cbreak();
    noecho(); // we'll echo input manually
    keypad(stdscr, TRUE);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    msg_win = make_bordered_window(rows - 3, cols, 0, 0);   // all but last line
    input_win = make_bordered_window(3, cols, rows - 3, 0); // last line

    refresh_bordered_window(&msg_win);
    refresh_bordered_window(&input_win);

    // make wgetch non-blocking; returns ERR if no input
    nodelay(input_win.inner, TRUE);
}

// Call this from your receive thread to post an incoming message
void post_message(const char *msg) {
    wprintw(msg_win.inner, "%s\n", msg);
    refresh_bordered_window(&msg_win);

    // Restore cursor to input window so the draft is unaffected
    refresh_bordered_window(&input_win);
}

void format_message_as_own(char buf[256], char new_buf[256], WINDOW *msg_win) {
    int width = getmaxx(msg_win);
    snprintf(new_buf, 256, "%*s", width, buf);
}

void format_message_as_alert(char buf[256], char new_buf[256],
                             WINDOW *msg_win) {
    int width = getmaxx(msg_win);
    snprintf(new_buf, 256, "%*s%s", (width - (int)strlen(buf)) / 2, "", buf);
}

// Global so that the handle_sigint can use it
int sockfd = -1;

void send_packet(int sockfd, uint8_t type, const char *body) {
    // sender_name must be set before sending any messages
    if (sender_name[0] == '\0') {
        fprintf(stderr, "Error: sender_name is not set\n");
        return;
    }

    MessageHeader hdr;
    hdr.version = 1;
    hdr.msg_type = type;
    hdr.flags = 0;
    hdr.length = htonl(sizeof(MessageBody)); // convert to network byte order
    send(sockfd, &hdr, sizeof(hdr), 0);

    MessageBody msg;
    strcpy(msg.sender_name, sender_name);
    strcpy(msg.body, body);
    send(sockfd, &msg, sizeof(MessageBody), 0);
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

    endwin(); // restore terminal settings
    exit(0);
};

int recv_packet(struct pollfd, MessageBody *message_body) {
    MessageHeader hdr;

    // Receive header
    if (recv(sockfd, &hdr, sizeof(hdr), MSG_WAITALL) <= 0) {
        // Server disconnected; client should too
        printf("Server disconnected\n");
        return -1;
    }

    hdr.length = ntohl(hdr.length); // convert network to local

    // Receive real message; use header's length to know how much to read
    ssize_t n = recv(sockfd, message_body, hdr.length, 0);
    if (n <= 0) {
        printf("Error receiving message body\n");
        return -1;
    }

    message_body->body[n] = '\0';

    switch (hdr.msg_type) {
    case MSG_ASK_FOR_NAME: {
        post_message(message_body->body);
        // TODO:
        break;
    }
    case MSG_USER_JOINED: {
        // TODO:
        break;
    }
    case MSG_USER_DISCONNECTED: {
        // TODO:
        break;
    }
    case MSG_CHAT: {
        post_message(message_body->body);
        break;
    }
    default:
        printf("Unknown type %d\n", hdr.msg_type);
    }

    return 0;
}

void log_successful_connection() {
    char raw_connection_alert[256];
    snprintf(raw_connection_alert, 256, "Client connected to server at %s:%d\n",
             SERVER_IP, SERVER_PORT);
    char formatted_connection_alert[256];
    format_message_as_alert(raw_connection_alert, formatted_connection_alert,
                            msg_win.inner);

    post_message(formatted_connection_alert);
}

int main(void) {
    struct sockaddr_in server_addr;
    MessageBody message_body;

    init_ui();

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

    struct pollfd fds[2];
    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    bool has_registered = false;

    log_successful_connection();

    char buf[256];
    int pos = 0;

    // Send message
    while (1) {
        int ch = wgetch(input_win.inner);
        // Handle input
        if (ch != ERR) {
            if (ch == '\n') {
                buf[pos] = '\0';
                // TODO: split these up; one method for registration,
                // another for chat messages
                int msg_type = has_registered ? MSG_CHAT : MSG_SET_NAME;
                // Must update sender_name before sending the message!
                if (!has_registered) {
                    strcpy(sender_name, buf);
                    has_registered = true;
                }
                send_packet(sockfd, msg_type, buf);
                char new_buf[256];
                format_message_as_own(buf, new_buf, msg_win.inner);
                post_message(new_buf);
                pos = 0;
                werase(input_win.inner);
                refresh_bordered_window(&input_win);
            } else if (ch == KEY_BACKSPACE || ch == 127) {
                if (pos > 0) {
                    buf[--pos] = '\0';
                    werase(input_win.inner);
                    mvwprintw(input_win.inner, 0, 0, "%s", buf);
                    refresh_bordered_window(&input_win);
                }
            } else if (pos < 255) {
                buf[pos++] = ch;
                waddch(input_win.inner, ch);
                refresh_bordered_window(&input_win);
            }
        }

        int ret = poll(fds, 2, 50); // use 50ms timeout to avoid busy waiting
        if (ret < 0) {
            perror("poll");
            break;
        }

        // Receive message
        if (fds[1].revents & POLLIN) {
            if (recv_packet(fds[1], &message_body) < 0) {
                break;
            }
        };
    }
    close(sockfd);
    free_bordered_window(&input_win);
    free_bordered_window(&msg_win);
    return 0;
};
