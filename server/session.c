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

static void aggiorna_entry_client_per_socket(int socket, DatiClient* client) {  //scrive un puntatore in una cella dell'array globale
    if (socket >= 0 && socket < MAX_SOCKET_TRACCIATI) {
        atomic_store(&client_per_socket[socket], client);
    }
}

static void chiudi_e_libera_client(DatiClient* client) {
    if (client == NULL) {
        return;
    }
    aggiorna_entry_client_per_socket(client->socket, NULL); //rimuove il client dall'array globale: imposta client_per_socket[fd] = NULL
    if (client->socket >= 0) { //chiude la socket: libera il file descriptor nele sistema operativo
        close(client->socket); 
    }
    free(client);//libera la memoria: il free che bilancia il malloc del main
}

DatiClient* trova_client_attivo_per_socket(int socket) {    //lettura atomica dell'array
    if (socket < 0 || socket >= MAX_SOCKET_TRACCIATI) {
        return NULL;
    }
    return atomic_load(&client_per_socket[socket]);
}

void aggiorna_id_partita_client_per_socket(int socket, int id_partita) {    //cerca il client e aggiorna atomicamente il suo id_partita_corrente
    DatiClient* client = trova_client_attivo_per_socket(socket);
    if (client != NULL) {
        atomic_store(&client->id_partita_corrente, id_partita);
    }
}

void* gestisci_client(void* arg) {
    DatiClient* client = (DatiClient*)arg;  //recupera il puntatore originale
    char buffer[DIM_BUFFER]; //array locale sullo stack del thread

    aggiorna_entry_client_per_socket(client->socket, client);

    printf("Client connesso (socaket: %d)\n", client->socket);

    invia_messaggio(client->socket, "Benvenuto a Forza 4!\n");
    invia_messaggio(client->socket, "Inserisci il tuo nome:\n");

    if (ricevi_messaggio(client->socket, buffer) <= 0) {
        chiudi_e_libera_client(client);
        return NULL;
    }
    snprintf(client->nome, sizeof(client->nome), "%.49s", buffer); // copia il nome del client limitando la lunghezza per evitare overflow del buffer

    char benvenuto[500];
    snprintf(benvenuto, sizeof(benvenuto), "Ciao %s! Comandi disponibili:\n%s", client->nome, MENU_COMANDI);
    invia_messaggio(client->socket, benvenuto);

    while (1) {
        int mostra_prompt_menu = 1;
        int id_corrente = atomic_load(&client->id_partita_corrente);
        if (id_corrente > 0) {
            blocca_partite();
            Partita* partita_prompt = trova_partita(id_corrente);
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


        if (gestisci_input_client(client, buffer) < 0) {
            break;
        }
    }

    gestisci_disconnessione_client(client);

    printf("Client %s disconnesso\n", client->nome);
    chiudi_e_libera_client(client);
    return NULL;
}
