#include "game_queries.h"

#include <stddef.h>

int trova_partita_in_corso_client(int socket_client) {
    Partita* partita = trova_partita_in_corso_per_socket(socket_client);
    return partita ? partita->id_partita : 0;
}

Partita* trova_partita_in_corso_per_socket(int socket_client) {
    for (int i = 0; i < contatore_partite; i++) {
        Partita* partita = &partite[i];
        if (partita->stato == PARTITA_IN_CORSO &&
            (partita->socket_giocatore1 == socket_client ||
             partita->socket_giocatore2 == socket_client)) {
            return partita;
        }
    }
    return NULL;
}

int trova_richiesta_pendente_client(int socket_client) {
    for (int i = 0; i < contatore_partite; i++) {
        Partita* partita = &partite[i];
        if (partita->stato == PARTITA_RICHIESTA_PENDENTE &&
            partita->socket_richiedente == socket_client) {
            return partita->id_partita;
        }
    }
    return 0;
}
