#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
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
#define MSG_SIZE 12

struct player {
    struct sockaddr_storage addr;
    socklen_t addr_len;
    bool active;
};

static void write_le32(uint8_t *p, int32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static int32_t read_le32(const uint8_t *p) {
    return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static void pack_msg(uint8_t out[MSG_SIZE], const char cmd[3], int32_t a, int32_t b) {
    memcpy(out, cmd, 3);
    out[3] = 0;
    write_le32(out + 4, a);
    write_le32(out + 8, b);
}

static int unpack_msg(const uint8_t *buf, size_t len, char cmd[4], int32_t *a, int32_t *b) {
    if (len < MSG_SIZE) {
        return -1;
    }
    memcpy(cmd, buf, 3);
    cmd[3] = '\0';
    *a = read_le32(buf + 4);
    *b = read_le32(buf + 8);
    return 0;
}

static int send_msg(int fd, const struct sockaddr_storage *addr, socklen_t addr_len,
                    const char cmd[3], int32_t a, int32_t b) {
    uint8_t buf[MSG_SIZE];
    pack_msg(buf, cmd, a, b);
    ssize_t n = sendto(fd, buf, sizeof(buf), 0, (const struct sockaddr *)addr, addr_len);
    return (n == (ssize_t)sizeof(buf)) ? 0 : -1;
}

static bool sockaddr_equal(const struct sockaddr_storage *a, const struct sockaddr_storage *b) {
    if (a->ss_family != b->ss_family) {
        return false;
    }
    if (a->ss_family == AF_INET) {
        const struct sockaddr_in *sa = (const struct sockaddr_in *)a;
        const struct sockaddr_in *sb = (const struct sockaddr_in *)b;
        return sa->sin_port == sb->sin_port && sa->sin_addr.s_addr == sb->sin_addr.s_addr;
    }
    if (a->ss_family == AF_INET6) {
        const struct sockaddr_in6 *sa = (const struct sockaddr_in6 *)a;
        const struct sockaddr_in6 *sb = (const struct sockaddr_in6 *)b;
        return sa->sin6_port == sb->sin6_port &&
               memcmp(&sa->sin6_addr, &sb->sin6_addr, sizeof(sa->sin6_addr)) == 0;
    }
    return false;
}

static int find_player(struct player players[MAX_PLAYERS], const struct sockaddr_storage *addr) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].active && sockaddr_equal(&players[i].addr, addr)) {
            return i;
        }
    }
    return -1;
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

static void reset_players(struct player players[MAX_PLAYERS]) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        players[i].active = false;
        players[i].addr_len = 0;
        memset(&players[i].addr, 0, sizeof(players[i].addr));
    }
}

static int start_server(uint16_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    printf("UDP server listening on port %u\n", port);

    struct player players[MAX_PLAYERS];
    reset_players(players);

    while (1) {
        int player_count = 0;

        while (player_count < MAX_PLAYERS) {
            uint8_t buf[MSG_SIZE];
            struct sockaddr_storage from;
            socklen_t from_len = sizeof(from);
            ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("recvfrom");
                continue;
            }

            char cmd[4];
            int32_t a = 0;
            int32_t b = 0;
            if (unpack_msg(buf, (size_t)n, cmd, &a, &b) != 0) {
                continue;
            }

            if (strcmp(cmd, "HEL") != 0) {
                continue;
            }

            int existing = find_player(players, &from);
            if (existing >= 0) {
                continue;
            }

            players[player_count].active = true;
            players[player_count].addr = from;
            players[player_count].addr_len = from_len;

            if (send_msg(fd, &players[player_count].addr, players[player_count].addr_len,
                         "PID", player_count, 0) != 0) {
                continue;
            }

            if (player_count == 0) {
                send_msg(fd, &players[player_count].addr, players[player_count].addr_len,
                         "HLD", 0, 0);
            }

            player_count++;
        }

        send_msg(fd, &players[0].addr, players[0].addr_len, "SRT", 0, 0);
        send_msg(fd, &players[1].addr, players[1].addr_len, "SRT", 0, 0);

        char board[BOARD_SIZE][BOARD_SIZE];
        for (int r = 0; r < BOARD_SIZE; r++) {
            for (int c = 0; c < BOARD_SIZE; c++) {
                board[r][c] = ' ';
            }
        }

        int current_player = 0;
        int previous_player = 1;
        int turn_count = 0;
        bool game_over = false;

        while (!game_over) {
            if (previous_player != current_player) {
                send_msg(fd, &players[(current_player + 1) % 2].addr,
                         players[(current_player + 1) % 2].addr_len, "WAT", 0, 0);
            }

            send_msg(fd, &players[current_player].addr, players[current_player].addr_len,
                     "TRN", 0, 0);

            bool valid = false;
            while (!valid) {
                uint8_t buf[MSG_SIZE];
                struct sockaddr_storage from;
                socklen_t from_len = sizeof(from);
                ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    perror("recvfrom");
                    continue;
                }

                if (!sockaddr_equal(&from, &players[current_player].addr)) {
                    continue;
                }

                char cmd[4];
                int32_t a = 0;
                int32_t b = 0;
                if (unpack_msg(buf, (size_t)n, cmd, &a, &b) != 0) {
                    continue;
                }

                if (strcmp(cmd, "MOV") != 0) {
                    continue;
                }

                int32_t move = a;
                valid = check_move(board, move);

                if (!valid) {
                    send_msg(fd, &players[current_player].addr, players[current_player].addr_len,
                             "INV", 0, 0);
                    continue;
                }

                if (move == 9) {
                    send_msg(fd, &players[current_player].addr, players[current_player].addr_len,
                             "CNT", player_count, 0);
                    previous_player = current_player;
                    continue;
                }

                update_board(board, move, current_player);
                send_msg(fd, &players[0].addr, players[0].addr_len,
                         "UPD", current_player, move);
                send_msg(fd, &players[1].addr, players[1].addr_len,
                         "UPD", current_player, move);

                if (check_winner(board, move)) {
                    send_msg(fd, &players[current_player].addr, players[current_player].addr_len,
                             "WIN", 0, 0);
                    send_msg(fd, &players[(current_player + 1) % 2].addr,
                             players[(current_player + 1) % 2].addr_len, "LSE", 0, 0);
                    game_over = true;
                } else if (turn_count == 8) {
                    send_msg(fd, &players[0].addr, players[0].addr_len, "DRW", 0, 0);
                    send_msg(fd, &players[1].addr, players[1].addr_len, "DRW", 0, 0);
                    game_over = true;
                }

                previous_player = current_player;
                current_player = (current_player + 1) % 2;
                turn_count += 1;
            }
        }

        reset_players(players);
    }

    close(fd);
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

    return start_server((uint16_t)port) == 0 ? 0 : 1;
}
