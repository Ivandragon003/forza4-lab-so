#include "network.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

int invia_messaggio(int socket, const char* messaggio) {
    if (socket < 0 || messaggio == NULL) {
        return -1;
    }

    ssize_t inviati = send(socket, messaggio, strlen(messaggio), 0);
    if (inviati < 0) {
        perror("Errore invio messaggio");
        return -1;
    }
    return 0;
}

int ricevi_messaggio(int socket, char* buffer) {
    if (socket < 0 || buffer == NULL) {
        return -1;
    }

    size_t pos = 0;
    int overflow = 0;
    memset(buffer, 0, DIM_BUFFER);

    while (1) {
        char c;
        int bytes = recv(socket, &c, 1, 0);
        if (bytes < 0) {
            return -1;
        }
        if (bytes == 0) {
            return 0; 
        }

        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            break;
        }

        if (!overflow && pos < (size_t)(DIM_BUFFER - 1)) {
            buffer[pos++] = c;
        } else {
            overflow = 1;
        }
    }

    buffer[pos] = '\0';
    return (int)pos;
}
