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

static int recv_msg3(int fd, char out[4]) {
    if (recv_all(fd, out, 3) != 0) {
        return -1;
    }
    out[3] = '\0';
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
            return send_int_le(fd, (int32_t)value);
        }

        printf("Invalid input.\n");
    }
}

static int get_update(int fd, char board[BOARD_SIZE][BOARD_SIZE]) {
    int32_t player_id = 0;
    int32_t move = 0;
    if (recv_int_le(fd, &player_id) != 0) {
        return -1;
    }
    if (recv_int_le(fd, &move) != 0) {
        return -1;
    }

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
    hints.ai_socktype = SOCK_STREAM;

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

    int32_t player_id = 0;
    if (recv_int_le(fd, &player_id) != 0) {
        fprintf(stderr, "Connection closed\n");
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

    char msg[4];
    while (1) {
        if (recv_msg3(fd, msg) != 0) {
            fprintf(stderr, "Connection closed\n");
            close(fd);
            return 1;
        }
        if (strcmp(msg, "HLD") == 0) {
            printf("Waiting for second player...\n");
        }
        if (strcmp(msg, "SRT") == 0) {
            break;
        }
    }

    printf("Game start!\n");
    printf("You are %s\n", player_id ? "X" : "O");

    draw_board(board);

    while (1) {
        if (recv_msg3(fd, msg) != 0) {
            fprintf(stderr, "Connection closed\n");
            break;
        }

        if (strcmp(msg, "TRN") == 0) {
            printf("Your move\n");
            if (take_turn(fd) != 0) {
                fprintf(stderr, "Failed to send move\n");
                break;
            }
        } else if (strcmp(msg, "INV") == 0) {
            printf("Invalid move\n");
        } else if (strcmp(msg, "CNT") == 0) {
            int32_t count = 0;
            if (recv_int_le(fd, &count) != 0) {
                fprintf(stderr, "Connection closed\n");
                break;
            }
            printf("Active players: %d\n", count);
        } else if (strcmp(msg, "UPD") == 0) {
            if (get_update(fd, board) != 0) {
                fprintf(stderr, "Connection closed\n");
                break;
            }
            draw_board(board);
        } else if (strcmp(msg, "WAT") == 0) {
            printf("Waiting for opponent\n");
        } else if (strcmp(msg, "WIN") == 0) {
            printf("You win!\n");
            break;
        } else if (strcmp(msg, "LSE") == 0) {
            printf("You lose.\n");
            break;
        } else if (strcmp(msg, "DRW") == 0) {
            printf("Draw.\n");
            break;
        }
    }

    close(fd);
    return 0;
}
