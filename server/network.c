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
    memset(buffer, 0, DIM_BUFFER);
    int bytes = recv(socket, buffer, DIM_BUFFER - 1, 0);
    if (bytes > 0) {
        buffer[strcspn(buffer, "\n")] = 0;
    }
    return bytes;
}
