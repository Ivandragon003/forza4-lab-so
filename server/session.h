#ifndef SESSION_H
#define SESSION_H

typedef struct {
    int socket;
    char nome[50];
    int id_partita_corrente;
} DatiClient;

void* gestisci_client(void* arg);

#endif
