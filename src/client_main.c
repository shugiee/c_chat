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

WINDOW *msg_win;   // message thread
WINDOW *input_win; // fixed bottom line

// TODO: add borders to history and input
// TODO: show own messages in history instead of just posting them to input line

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 18000
#define BUFFER_SIZE 1024

void init_ui() {
    initscr();
    cbreak();
    noecho(); // we'll echo input manually
    keypad(stdscr, TRUE);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    msg_win = newwin(rows - 1, cols, 0, 0);   // all but last line
    input_win = newwin(1, cols, rows - 1, 0); // last line

    wrefresh(msg_win);
    wrefresh(input_win);
    nodelay(input_win,
            TRUE); // make wgetch non-blocking; returns ERR if no input
}

// Call this from your receive thread to post an incoming message
void post_incoming_message(const char *msg) {
    wprintw(msg_win, "%s\n", msg);
    wrefresh(msg_win);

    // Restore cursor to input window so the draft is unaffected
    wrefresh(input_win);
}

void format_message_as_own(char buf[256], char new_buf[256], WINDOW *msg_win) {
    int width = getmaxx(msg_win);
    snprintf(new_buf, 256, "%*s", width, buf);
}

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

    endwin(); // restore terminal settings
    exit(0);
};

int main(void) {
    struct sockaddr_in server_addr;
    char read_buffer[BUFFER_SIZE];

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

    // TODO: put this somewhere in ncurses; a popup or in initial chat history
    // printf("Client connected to server at %s:%d\n", SERVER_IP, SERVER_PORT);

    char buf[256];
    int pos = 0;

    // Send message
    while (1) {
        int ch = wgetch(input_win);
        // Handle input
        if (ch != ERR) {
            if (ch == '\n') {
                buf[pos] = '\0';
                // TODO: split these up; one method for registration,
                // another for chat messages
                int msg_type = has_registered ? MSG_CHAT : MSG_SET_NAME;
                send_packet(sockfd, msg_type, buf);
                char new_buf[256];
                format_message_as_own(buf, new_buf, msg_win);
                post_incoming_message(new_buf);
                if (!has_registered)
                    has_registered = true;
                pos = 0;
                werase(input_win);
                wrefresh(input_win);
            } else if (ch == KEY_BACKSPACE || ch == 127) {
                if (pos > 0) {
                    buf[--pos] = '\0';
                    werase(input_win);
                    mvwprintw(input_win, 0, 0, "%s", buf);
                    wrefresh(input_win);
                }
            } else if (pos < 255) {
                buf[pos++] = ch;
                waddch(input_win, ch);
                wrefresh(input_win);
            }
        }

        int ret = poll(fds, 2, 50); // use 50ms timeout to avoid busy waiting
        if (ret < 0) {
            perror("poll");
            break;
        }

        // Receive message
        if (fds[1].revents & POLLIN) {
            ssize_t n = recv(sockfd, read_buffer, sizeof(read_buffer) - 1, 0);
            if (n <= 0) {
                printf("Server disconnected\n");
                break;
            }

            read_buffer[n] = '\0';
            post_incoming_message(read_buffer);
            // fflush(stdout);
        };
    }
    close(sockfd);
    return 0;
};
