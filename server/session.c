#include "session.h"
#include "network.h"
#include "game.h"
#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

_Atomic(DatiClient*) client_per_socket[MAX_SOCKET_TRACCIATI] = {0};

static const char* MENU_COMANDI =
    "CREA - Crea una nuova partita\n"
    "LISTA - Mostra partite disponibili\n"
    "ENTRA <id> - Richiedi di unirti a una partita\n"
    "ACCETTA <id> - Accetta richiesta (solo creatore)\n"
    "RIFIUTA <id> - Rifiuta richiesta (solo creatore)\n"
    "ABBANDONA - Ti arrendi nella partita corrente ma resti connesso\n"
    "ESCI - Esci dal gioco (solo quando non sei in partita)\n";

DatiClient* trova_client_attivo_per_socket(int socket) {
    if (socket < 0 || socket >= MAX_SOCKET_TRACCIATI) {
        return NULL;
    }
    return atomic_load(&client_per_socket[socket]);
}

void aggiorna_id_partita_client_per_socket(int socket, int id_partita) {
    DatiClient* client = trova_client_attivo_per_socket(socket);
    if (client != NULL) {
        client->id_partita_corrente = id_partita;
    }
}

void* gestisci_client(void* arg) {
    DatiClient* client = (DatiClient*)arg;
    char buffer[DIM_BUFFER];

    if (client->socket >= 0 && client->socket < MAX_SOCKET_TRACCIATI) {
        atomic_store(&client_per_socket[client->socket], client);
    }

    printf("Client connesso (socket: %d)\n", client->socket);

    invia_messaggio(client->socket, "Benvenuto a Forza 4!\n");
    invia_messaggio(client->socket, "Inserisci il tuo nome:\n");

    if (ricevi_messaggio(client->socket, buffer) <= 0) {
        if (client->socket >= 0 && client->socket < MAX_SOCKET_TRACCIATI) {
            atomic_store(&client_per_socket[client->socket], NULL);
        }
        close(client->socket);
        free(client);
        return NULL;
    }
    snprintf(client->nome, sizeof(client->nome), "%.49s", buffer);

    char benvenuto[500];
    snprintf(benvenuto, sizeof(benvenuto), "Ciao %s! Comandi disponibili:\n%s", client->nome, MENU_COMANDI);
    invia_messaggio(client->socket, benvenuto);

    while (1) {
        int mostra_prompt_menu = 1;
        if (client->id_partita_corrente > 0) {
            blocca_partite();
            Partita* partita_prompt = trova_partita(client->id_partita_corrente);
            if (partita_prompt && partita_prompt->stato == PARTITA_IN_CORSO) {
                mostra_prompt_menu = 0;
            }
            sblocca_partite();
        }
        if (mostra_prompt_menu) {
            invia_messaggio(client->socket, "\n> ");
        }

        int ret = ricevi_messaggio(client->socket, buffer);
        if (ret <= 0) {
            break;
        }

        printf("Ricevuto da %s: %s\n", client->nome, buffer);

        if (gestisci_input_client(client, buffer) < 0) {
            break;
        }
    }

    gestisci_disconnessione_client(client);

    printf("Client %s disconnesso\n", client->nome);
    if (client->socket >= 0 && client->socket < MAX_SOCKET_TRACCIATI) {
        atomic_store(&client_per_socket[client->socket], NULL);
    }
    close(client->socket);
    free(client);
    return NULL;
}
