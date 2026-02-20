#ifndef SESSION_H
#define SESSION_H

#include <stdatomic.h>

typedef struct {
    int socket;
    char nome[50];
    int id_partita_corrente;
} DatiClient;

#define MAX_SOCKET_TRACCIATI 4096

extern _Atomic(DatiClient*) client_per_socket[MAX_SOCKET_TRACCIATI];

DatiClient* trova_client_attivo_per_socket(int socket);
void aggiorna_id_partita_client_per_socket(int socket, int id_partita);

void* gestisci_client(void* arg);

#endif
