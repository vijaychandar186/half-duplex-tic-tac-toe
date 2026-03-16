#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BOARD_SIZE 3
#define MSG_SIZE 12

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

static int send_msg(int fd, const char cmd[3], int32_t a, int32_t b) {
    uint8_t buf[MSG_SIZE];
    pack_msg(buf, cmd, a, b);
    ssize_t n = send(fd, buf, sizeof(buf), 0);
    return (n == (ssize_t)sizeof(buf)) ? 0 : -1;
}

static int recv_msg(int fd, char cmd[4], int32_t *a, int32_t *b) {
    uint8_t buf[MSG_SIZE];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n < 0) {
        return -1;
    }
    return unpack_msg(buf, (size_t)n, cmd, a, b);
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

static int take_turn(int fd) {
    char line[64];
    while (1) {
        printf("Enter 0-8 for move or 9 for player count: ");
        if (!fgets(line, sizeof(line), stdin)) {
            return -1;
        }

        char *end = NULL;
        long value = strtol(line, &end, 10);
        if (end != line && (*end == '\n' || *end == '\0') && value >= 0 && value <= 9) {
            return send_msg(fd, "MOV", (int32_t)value, 0);
        }

        printf("Invalid input.\n");
    }
}

static int get_update(char board[BOARD_SIZE][BOARD_SIZE], int32_t player_id, int32_t move) {
    int row = move / 3;
    int col = move % 3;
    board[row][col] = player_id ? 'X' : 'O';
    return 0;
}

static int connect_to_server(const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
        return 1;
    }

    int fd = connect_to_server(argv[1], argv[2]);
    if (fd < 0) {
        fprintf(stderr, "Failed to connect to %s:%s\n", argv[1], argv[2]);
        return 1;
    }

    if (send_msg(fd, "HEL", 0, 0) != 0) {
        fprintf(stderr, "Failed to contact server\n");
        close(fd);
        return 1;
    }

    char board[BOARD_SIZE][BOARD_SIZE];
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            board[r][c] = ' ';
        }
    }

    printf("Tic-Tac-Toe\n");
    printf("-----------\n");

    int32_t player_id = -1;
    char cmd[4];
    int32_t a = 0;
    int32_t b = 0;

    while (1) {
        if (recv_msg(fd, cmd, &a, &b) != 0) {
            fprintf(stderr, "Connection closed\n");
            close(fd);
            return 1;
        }
        if (strcmp(cmd, "PID") == 0) {
            player_id = a;
        } else if (strcmp(cmd, "HLD") == 0) {
            printf("Waiting for second player...\n");
        } else if (strcmp(cmd, "SRT") == 0) {
            break;
        }
    }

    if (player_id < 0) {
        fprintf(stderr, "Failed to receive player id\n");
        close(fd);
        return 1;
    }

    printf("Game start!\n");
    printf("You are %s\n", player_id ? "X" : "O");

    draw_board(board);

    while (1) {
        if (recv_msg(fd, cmd, &a, &b) != 0) {
            fprintf(stderr, "Connection closed\n");
            break;
        }

        if (strcmp(cmd, "TRN") == 0) {
            printf("Your move\n");
            if (take_turn(fd) != 0) {
                fprintf(stderr, "Failed to send move\n");
                break;
            }
        } else if (strcmp(cmd, "INV") == 0) {
            printf("Invalid move\n");
        } else if (strcmp(cmd, "CNT") == 0) {
            printf("Active players: %d\n", a);
        } else if (strcmp(cmd, "UPD") == 0) {
            get_update(board, a, b);
            draw_board(board);
        } else if (strcmp(cmd, "WAT") == 0) {
            printf("Waiting for opponent\n");
        } else if (strcmp(cmd, "WIN") == 0) {
            printf("You win!\n");
            break;
        } else if (strcmp(cmd, "LSE") == 0) {
            printf("You lose.\n");
            break;
        } else if (strcmp(cmd, "DRW") == 0) {
            printf("Draw.\n");
            break;
        }
    }

    close(fd);
    return 0;
}
