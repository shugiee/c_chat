#define _POSIX_C_SOURCE 199309L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ncurses.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
#define RESIZE_DEBOUNCE_MS 60

long long timespec_to_ns(struct timespec ts) {
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

char current_user_name[64];

BorderedWindow make_bordered_window(int rows, int cols, int y, int x) {
    BorderedWindow bw;
    bw.outer = newwin(rows, cols, y, x);
    box(bw.outer, 0, 0); // draw border
    wrefresh(bw.outer);

    // inner window is 2 smaller in each dimension, offset by 1
    bw.inner = derwin(bw.outer, rows - 2, cols - 2, 1, 1);
    return bw;
}

void history_init(MessageHistory *h) {
    h->capacity = 100;
    h->length = 0;
    h->data = malloc(h->capacity * sizeof(MessageHeaderAndBody));
}

void history_push(MessageHistory *h, MessageHeaderAndBody msg) {
    if (h->length == h->capacity) {
        h->capacity *= 2;
        h->data = realloc(h->data, h->capacity * sizeof(MessageHeaderAndBody));
    }
    h->data[h->length++] = msg;
}

void history_free(MessageHistory *h) {
    free(h->data);
    h->data = NULL;
    h->length = h->capacity = 0;
}

void free_bordered_window(BorderedWindow *bw) {
    delwin(bw->inner);
    delwin(bw->outer);
}

void refresh_bordered_window(BorderedWindow *bw) {
    touchwin(bw->outer);
    wrefresh(bw->inner);
}

int get_number_idx_from_name(char *sender_name) {
    unsigned int sum = 0;
    for (int i = 0; sender_name[i] != 0; i++) {
        // Multiply by a small prime > 10 to calculate anagrams differently
        sum = sum * 11 + sender_name[i];
    }
    return sum % 10;
}

void init_ui() {
    initscr();
    start_color();
    use_default_colors();
    cbreak();
    noecho(); // we'll echo input manually
    keypad(stdscr, TRUE);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    msg_win = make_bordered_window(rows - 3, cols, 0, 0);   // all but last line
    input_win = make_bordered_window(3, cols, rows - 3, 0); // last line

    scrollok(msg_win.inner, TRUE); // allow msg_win to scroll

    refresh_bordered_window(&msg_win);
    refresh_bordered_window(&input_win);

    init_pair(0, -1, -1);
    init_pair(1, COLOR_RED, -1);
    init_pair(2, COLOR_GREEN, -1);
    init_pair(3, COLOR_BLUE, -1);
    init_pair(4, COLOR_CYAN, -1);
    init_pair(5, COLOR_MAGENTA, -1);
    init_pair(6, COLOR_YELLOW, -1);
    init_pair(7, COLOR_RED, -1);
    init_pair(8, COLOR_GREEN, -1);
    init_pair(9, COLOR_BLUE, -1);
    init_pair(10, COLOR_MAGENTA, -1);

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

void post_message_with_flair(const char *msg, char *sender_name) {
    int color_pair_number = get_number_idx_from_name(sender_name);
    wattron(msg_win.inner, COLOR_PAIR(color_pair_number));
    post_message(msg);
    // Reset colors
    wattroff(msg_win.inner, COLOR_PAIR(color_pair_number));
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
    // current_user_name must be set before sending any messages
    if (current_user_name[0] == '\0') {
        fprintf(stderr, "Error: current_user_name is not set\n");
        return;
    }

    MessageHeader hdr;
    hdr.version = 1;
    hdr.msg_type = type;
    hdr.flags = 0;
    hdr.length = htonl(sizeof(MessageBody)); // convert to network byte order
    send(sockfd, &hdr, sizeof(hdr), 0);

    MessageBody msg;
    strcpy(msg.sender_name, current_user_name);
    strcpy(msg.body, body);
    send(sockfd, &msg, sizeof(MessageBody), 0);
}

void handle_sigint(int sig) {
    (void)sig; // unused

    if (sockfd >= 0) {
        // Send "disconnect" message
        const char *msg = "Client exiting";
        send_packet(sockfd, MSG_DISCONNECT, msg);
        printf("Sent disconnect message to server\n");
        close(sockfd);
    }

    endwin(); // restore terminal settings
    exit(0);
};

void log_user_joined(MessageBody *message_body) {
    char user_joined_alert[256];
    snprintf(user_joined_alert, 256, "%s joined!\n", message_body->sender_name);
    char formatted_user_joined_alert[256];
    format_message_as_alert(user_joined_alert, formatted_user_joined_alert,
                            msg_win.inner);
    post_message_with_flair(formatted_user_joined_alert,
                            message_body->sender_name);
}

void log_user_left(MessageBody *message_body) {
    char user_left_alert[256];
    snprintf(user_left_alert, 256, "%s left\n", message_body->sender_name);
    char formatted_user_left_alert[256];
    format_message_as_alert(user_left_alert, formatted_user_left_alert,
                            msg_win.inner);
    post_message_with_flair(formatted_user_left_alert,
                            message_body->sender_name);
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

int store_message_in_history(MessageBody *body, MessageHeader *hdr,
                             MessageHistory *history) {
    MessageHeaderAndBody headerAndBody;
    headerAndBody.body = *body;
    headerAndBody.header = *hdr;

    history_push(history, headerAndBody);
    return 0;
}

// TODO: handle own messages that are received over the wire from history!
int post_received_message(MessageBody *body, MessageHeader *hdr,
                          MessageHistory *history) {
    // Dim the user's name
    wattron(msg_win.inner, A_DIM);
    post_message_with_flair(body->sender_name, body->sender_name);
    wattroff(msg_win.inner, A_DIM);

    post_message_with_flair(body->body, body->sender_name);
    return 0;
}

int recv_packet(struct pollfd, MessageBody *message_body,
                MessageHistory *history) {
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
        // TODO: give this its own UI before joining the room
        post_message(message_body->body);
        break;
    }
    case MSG_USER_JOINED: {
        log_user_joined(message_body);
        break;
    }
    case MSG_USER_DISCONNECTED: {
        log_user_left(message_body);
        break;
    }
    case MSG_CHAT: {
        store_message_in_history(message_body, &hdr, history);
        post_received_message(message_body, &hdr, history);
        break;
    }
    default:
        printf("Unknown type %d\n", hdr.msg_type);
    }
    // Post one empty message to force a final newline
    post_message("");

    return 0;
}

static void debounce_resize_event(void) {
    (void)resize_term(0, 0);

    int next = ERR;
    timeout(RESIZE_DEBOUNCE_MS);
    while (1) {
        next = getch();
        if (next != KEY_RESIZE)
            break;
        (void)resize_term(0, 0);
    }
    timeout(-1);

    // Re-post the next event (if any) so it can be handled in the main loop
    if (next != ERR)
        (void)ungetch(next);
}

int main(void) {
    struct sockaddr_in server_addr;
    MessageBody message_body;
    MessageHistory history;

    history_init(&history);

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
            // Window resize
            if (ch == KEY_RESIZE) {
                // Debounce multiple resize events
                debounce_resize_event();
                int rows, cols;
                getmaxyx(stdscr, rows, cols);

                free_bordered_window(&msg_win);
                free_bordered_window(&input_win);

                msg_win = make_bordered_window(rows - 3, cols, 0, 0);
                input_win = make_bordered_window(3, cols, rows - 3, 0);
                scrollok(msg_win.inner, TRUE);
                nodelay(input_win.inner, TRUE);

                // Redraw the current input buffer
                mvwprintw(input_win.inner, 0, 0, "%.*s", pos, buf);

                for (int i = 0; i < history.length; i++) {
                    MessageHeaderAndBody headerAndBody = history.data[i];
                    post_received_message(&headerAndBody.body,
                                          &headerAndBody.header, &history);

                    post_message("");
                }
                continue;
            } else if (ch == '\n') {
                if (buf[0] == '\0')
                    continue;
                buf[pos] = '\0';
                // TODO: split these up; one method for registration,
                // another for chat messages
                int msg_type = has_registered ? MSG_CHAT : MSG_SET_NAME;
                // Must update current_user_name before sending the message!
                if (!has_registered) {
                    strcpy(current_user_name, buf);
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
            // If window resized while polling, just redraw and continue
            if (errno == EINTR)
                continue; // signal interrupted poll (e.g. SIGWINCH); retry
            perror("poll");
            break;
        }

        // Receive message
        if (fds[1].revents & POLLIN) {
            if (recv_packet(fds[1], &message_body, &history) < 0) {
                break;
            }
        };
    }
    close(sockfd);
    free_bordered_window(&input_win);
    free_bordered_window(&msg_win);
    history_free(&history);
    return 0;
};
