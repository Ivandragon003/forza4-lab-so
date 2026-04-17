#include "network.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

int invia_messaggio(int socket, const char* messaggio) {
    if (socket < 0 || messaggio == NULL) {
        return -1;
    }

    size_t len = strlen(messaggio);
    size_t tot_inviati = 0; //byte inviati fino ad ora

    while (tot_inviati < len) {
        ssize_t inviati = send(socket, messaggio + tot_inviati, len - tot_inviati, 0);
        if (inviati < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (inviati == 0) {
            return -1;
        }
        tot_inviati += (size_t)inviati;
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
            if (errno == EINTR) {
                continue;
            }
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
