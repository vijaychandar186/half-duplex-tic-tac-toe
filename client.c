#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONFIG_FILE "config.ini"

static char *trim(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return s;
}

static void to_upper(char *s) {
    for (; *s; s++) {
        *s = (char)toupper((unsigned char)*s);
    }
}

static void read_protocol(char out[8]) {
    strncpy(out, "TCP", 8);
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) {
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char *key = trim(line);
        char *val = trim(eq + 1);
        to_upper(key);
        if (strcmp(key, "PROTOCOL") == 0) {
            strncpy(out, val, 7);
            out[7] = '\0';
            to_upper(out);
            break;
        }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    char protocol[8];
    read_protocol(protocol);

    const char *path = "./TCP/client";
    if (strcmp(protocol, "UDP") == 0) {
        path = "./UDP/client";
    }

    char **new_argv = calloc((size_t)argc + 1, sizeof(char *));
    if (!new_argv) {
        perror("calloc");
        return 1;
    }
    new_argv[0] = (char *)path;
    for (int i = 1; i < argc; i++) {
        new_argv[i] = argv[i];
    }
    new_argv[argc] = NULL;

    execv(path, new_argv);
    perror("execv");
    free(new_argv);
    return 1;
}
