#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BOARD_SIZE 3
#define MAX_PLAYERS 2

static int player_count = 0;
static pthread_mutex_t player_lock = PTHREAD_MUTEX_INITIALIZER;

static int send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t recvd = 0;
    while (recvd < len) {
        ssize_t n = recv(fd, p + recvd, len - recvd, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        recvd += (size_t)n;
    }
    return 0;
}

static int send_int_le(int fd, int32_t value) {
    uint8_t buf[4];
    buf[0] = (uint8_t)(value & 0xFF);
    buf[1] = (uint8_t)((value >> 8) & 0xFF);
    buf[2] = (uint8_t)((value >> 16) & 0xFF);
    buf[3] = (uint8_t)((value >> 24) & 0xFF);
    return send_all(fd, buf, sizeof(buf));
}

static int recv_int_le(int fd, int32_t *value) {
    uint8_t buf[4];
    if (recv_all(fd, buf, sizeof(buf)) != 0) {
        return -1;
    }
    *value = (int32_t)(buf[0] |
                       (buf[1] << 8) |
                       (buf[2] << 16) |
                       (buf[3] << 24));
    return 0;
}

static int send_msg3(int fd, const char msg[3]) {
    return send_all(fd, msg, 3);
}

static int send_msg_to_both(int clients[MAX_PLAYERS], const char msg[3]) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (send_msg3(clients[i], msg) != 0) {
            return -1;
        }
    }
    return 0;
}

static int send_int_to_both(int clients[MAX_PLAYERS], int32_t value) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (send_int_le(clients[i], value) != 0) {
            return -1;
        }
    }
    return 0;
}

static void draw_board(char board[BOARD_SIZE][BOARD_SIZE]) {
    printf("\n");
    for (int r = 0; r < BOARD_SIZE; r++) {
        printf(" %c | %c | %c \n", board[r][0], board[r][1], board[r][2]);
        if (r != BOARD_SIZE - 1) {
            printf("-----------\n");
        }
    }
    printf("\n");
}

static bool check_move(char board[BOARD_SIZE][BOARD_SIZE], int32_t move) {
    if (move == 9) {
        return true;
    }
    if (move < 0 || move > 8) {
        return false;
    }
    int row = move / 3;
    int col = move % 3;
    return board[row][col] == ' ';
}

static void update_board(char board[BOARD_SIZE][BOARD_SIZE], int32_t move, int player_id) {
    int row = move / 3;
    int col = move % 3;
    board[row][col] = player_id ? 'X' : 'O';
}

static bool check_winner(char board[BOARD_SIZE][BOARD_SIZE], int32_t move) {
    int r = move / 3;
    int c = move % 3;
    char mark = board[r][c];

    if (mark == ' ') {
        return false;
    }

    if (board[r][0] == mark && board[r][1] == mark && board[r][2] == mark) {
        return true;
    }

    if (board[0][c] == mark && board[1][c] == mark && board[2][c] == mark) {
        return true;
    }

    if (r == c) {
        if (board[0][0] == mark && board[1][1] == mark && board[2][2] == mark) {
            return true;
        }
    }
    if (r + c == 2) {
        if (board[0][2] == mark && board[1][1] == mark && board[2][0] == mark) {
            return true;
        }
    }

    return false;
}

static int send_update(int clients[MAX_PLAYERS], int32_t move, int player_id) {
    if (send_msg_to_both(clients, "UPD") != 0) {
        return -1;
    }
    if (send_int_to_both(clients, player_id) != 0) {
        return -1;
    }
    if (send_int_to_both(clients, move) != 0) {
        return -1;
    }
    return 0;
}

static int send_player_count(int fd) {
    if (send_msg3(fd, "CNT") != 0) {
        return -1;
    }
    pthread_mutex_lock(&player_lock);
    int32_t count = player_count;
    pthread_mutex_unlock(&player_lock);
    return send_int_le(fd, count);
}

static int get_player_move(int fd, int32_t *move) {
    if (send_msg3(fd, "TRN") != 0) {
        return -1;
    }
    return recv_int_le(fd, move);
}

struct game_args {
    int clients[MAX_PLAYERS];
};

static void close_clients(int clients[MAX_PLAYERS]) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i] >= 0) {
            shutdown(clients[i], SHUT_RDWR);
            close(clients[i]);
            clients[i] = -1;
        }
    }
}

static void *run_game(void *arg) {
    struct game_args *args = (struct game_args *)arg;
    int clients[MAX_PLAYERS] = {args->clients[0], args->clients[1]};
    free(args);

    char board[BOARD_SIZE][BOARD_SIZE];
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            board[r][c] = ' ';
        }
    }

    printf("Game started\n");
    if (send_msg_to_both(clients, "SRT") != 0) {
        close_clients(clients);
        goto done;
    }

    draw_board(board);

    int current_player = 0;
    int previous_player = 1;
    int turn_count = 0;
    bool game_over = false;

    while (!game_over) {
        if (previous_player != current_player) {
            if (send_msg3(clients[(current_player + 1) % 2], "WAT") != 0) {
                break;
            }
        }

        bool valid = false;

        while (!valid) {
            int32_t move = -1;
            if (get_player_move(clients[current_player], &move) != 0) {
                printf("Player disconnected\n");
                close_clients(clients);
                goto done;
            }

            printf("Player %d played %d\n", current_player, move);

            valid = check_move(board, move);

            if (!valid) {
                if (send_msg3(clients[current_player], "INV") != 0) {
                    close_clients(clients);
                    goto done;
                }
            } else if (move == 9) {
                if (send_player_count(clients[current_player]) != 0) {
                    close_clients(clients);
                    goto done;
                }
                previous_player = current_player;
            } else {
                update_board(board, move, current_player);
                if (send_update(clients, move, current_player) != 0) {
                    close_clients(clients);
                    goto done;
                }

                draw_board(board);

                if (check_winner(board, move)) {
                    send_msg3(clients[current_player], "WIN");
                    send_msg3(clients[(current_player + 1) % 2], "LSE");
                    printf("Player %d wins\n", current_player);
                    game_over = true;
                } else if (turn_count == 8) {
                    send_msg_to_both(clients, "DRW");
                    printf("Draw\n");
                    game_over = true;
                }

                previous_player = current_player;
                current_player = (current_player + 1) % 2;
                turn_count += 1;
            }
        }
    }

    close_clients(clients);

done:
    pthread_mutex_lock(&player_lock);
    player_count -= 2;
    printf("Players remaining: %d\n", player_count);
    pthread_mutex_unlock(&player_lock);
    return NULL;
}

static int accept_clients(int server_fd, int clients[MAX_PLAYERS]) {
    int count = 0;
    while (count < MAX_PLAYERS) {
        struct sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);
        int conn = accept(server_fd, (struct sockaddr *)&addr, &addr_len);
        if (conn < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        printf("Connected: %d\n", conn);

        if (send_int_le(conn, count) != 0) {
            close(conn);
            return -1;
        }

        clients[count] = conn;

        pthread_mutex_lock(&player_lock);
        player_count += 1;
        pthread_mutex_unlock(&player_lock);

        if (count == 0) {
            if (send_msg3(conn, "HLD") != 0) {
                close(conn);
                return -1;
            }
        }

        count++;
    }
    return 0;
}

static int start_server(uint16_t port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 16) != 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    printf("Server listening on port %u\n", port);

    while (1) {
        int clients[MAX_PLAYERS] = {-1, -1};
        if (accept_clients(server_fd, clients) != 0) {
            perror("accept");
            close_clients(clients);
            continue;
        }

        struct game_args *args = malloc(sizeof(*args));
        if (!args) {
            close_clients(clients);
            continue;
        }
        args->clients[0] = clients[0];
        args->clients[1] = clients[1];

        pthread_t tid;
        if (pthread_create(&tid, NULL, run_game, args) != 0) {
            close_clients(clients);
            free(args);
            continue;
        }
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    char *end = NULL;
    long port = strtol(argv[1], &end, 10);
    if (!end || *end != '\0' || port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return 1;
    }

    if (start_server((uint16_t)port) != 0) {
        return 1;
    }
    return 0;
}
