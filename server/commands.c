#include "commands.h"
#include "network.h"
#include "game.h"
#include "game_queries.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TIMEOUT_RICHIESTA_SEC 30

typedef struct {
    int id_partita;
    int socket_richiedente;
} TimeoutRichiestaArgs;

static void termina_partita_per_socket_non_raggiungibile(Partita* partita, int socket_non_raggiungibile) {
    if (partita == NULL || partita->stato != PARTITA_IN_CORSO) {
        return;
    }

    int e_giocatore1 = (partita->socket_giocatore1 == socket_non_raggiungibile);
    int avversario = e_giocatore1 ? partita->socket_giocatore2 : partita->socket_giocatore1;

    partita->stato = PARTITA_TERMINATA;
    partita->vincitore = e_giocatore1 ? 2 : 1;

    if (avversario > 0) {
        invia_messaggio(avversario, "L'altro giocatore si e' disconnesso. Hai vinto a tavolino.\n");
        invia_messaggio(avversario, "Puoi creare o unirti a una nuova partita.\n");
    }

    if (e_giocatore1) {
        partita->socket_giocatore1 = -1;
        partita->nome_giocatore1[0] = '\0';
    } else {
        partita->socket_giocatore2 = -1;
        partita->nome_giocatore2[0] = '\0';
    }
}

static int invia_con_fallback(int socket, const char* messaggio, int* socket_ko) {
    if (*socket_ko < 0 && invia_messaggio(socket, messaggio) < 0) {
        *socket_ko = socket;
        return -1;
    }
    return 0;
}

static void invia_griglia_a_entrambi(Partita* partita) {
    char griglia[500];
    griglia_a_stringa(partita->griglia, griglia, sizeof(griglia));

    invia_messaggio(partita->socket_giocatore1, "\n");
    invia_messaggio(partita->socket_giocatore1, griglia);
    invia_messaggio(partita->socket_giocatore2, "\n");
    invia_messaggio(partita->socket_giocatore2, griglia);
}

static void invia_griglia_a_entrambi_con_fallback(Partita* partita, int* socket_ko) {
    char griglia[500];
    griglia_a_stringa(partita->griglia, griglia, sizeof(griglia));

    invia_con_fallback(partita->socket_giocatore1, "\n", socket_ko);
    invia_con_fallback(partita->socket_giocatore1, griglia, socket_ko);
    invia_con_fallback(partita->socket_giocatore2, "\n", socket_ko);
    invia_con_fallback(partita->socket_giocatore2, griglia, socket_ko);
}

static void invia_fine_partita_a_entrambi(Partita* partita) {
    invia_messaggio(partita->socket_giocatore1, "Puoi creare o unirti a una nuova partita.\n");
    invia_messaggio(partita->socket_giocatore2, "Puoi creare o unirti a una nuova partita.\n");
}

static void* gestisci_timeout_richiesta(void* arg) {
    TimeoutRichiestaArgs* timeout_args = (TimeoutRichiestaArgs*)arg;
    sleep(TIMEOUT_RICHIESTA_SEC);

    Partita* partita = trova_partita(timeout_args->id_partita);
    if (partita != NULL &&
        partita->stato == PARTITA_RICHIESTA_PENDENTE &&
        partita->socket_richiedente == timeout_args->socket_richiedente) {
        int atteso = 0;
        if (!atomic_compare_exchange_strong(&partita->timeout_annullato, &atteso, 1)) {
            free(timeout_args);
            return NULL;
        }

        int socket_creatore = partita->socket_giocatore1;
        int socket_richiedente = partita->socket_richiedente;

        partita->socket_richiedente = -1;
        partita->nome_richiedente[0] = '\0';
        partita->stato = PARTITA_IN_ATTESA;

        if (socket_richiedente > 0) {
            invia_messaggio(socket_richiedente,
                            "Tempo scaduto: nessuna risposta dal creatore. Richiesta annullata.\n");
        }
        if (socket_creatore > 0) {
            invia_messaggio(socket_creatore,
                            "La richiesta pendente e' scaduta automaticamente.\n");
        }
    }

    free(timeout_args);
    return NULL;
}

static int gestisci_comando_crea(DatiClient* client) {
    int id_in_corso = trova_partita_in_corso_client(client->socket);
    if (id_in_corso > 0) {
        char msg[170];
        snprintf(msg, sizeof(msg),
                 "Stai gia' giocando la partita ID %d. "
                 "Non puoi crearne un'altra finche' non termina.\n",
                 id_in_corso);
        invia_messaggio(client->socket, msg);
        return 0;
    }

    int id_partita = crea_partita(client->socket, client->nome);
    if (id_partita > 0) {
        char msg[100];
        sprintf(msg, "Partita creata! ID: %d\nIn attesa di un avversario...\n", id_partita);
        invia_messaggio(client->socket, msg);
    } else {
        invia_messaggio(client->socket, "Errore nella creazione della partita.\n");
    }
    return 0;
}

static int gestisci_comando_entra(DatiClient* client, const char* buffer) {
    int id_in_corso = trova_partita_in_corso_client(client->socket);
    if (id_in_corso > 0) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "Sei gia' coinvolto nella partita ID %d. "
                 "Non puoi entrare in un'altra partita.\n",
                 id_in_corso);
        invia_messaggio(client->socket, msg);
        return 0;
    }
    int id_pendente = trova_richiesta_pendente_client(client->socket);
    if (id_pendente > 0) {
        char msg[180];
        snprintf(msg, sizeof(msg),
                 "Hai gia' una richiesta pendente sulla partita ID %d. "
                 "Attendi ACCETTA/RIFIUTA prima di entrare altrove.\n",
                 id_pendente);
        invia_messaggio(client->socket, msg);
        return 0;
    }

    int id_partita = atoi(buffer + 6);
    int risultato = richiedi_partita(id_partita, client->socket, client->nome);

    if (risultato == 0) {
        Partita* partita = trova_partita(id_partita);

        client->id_partita_corrente = id_partita;

        char msg_attesa[180];
        snprintf(msg_attesa, sizeof(msg_attesa),
                 "Richiesta inviata. Attendi risposta (timeout %d secondi)...\n",
                 TIMEOUT_RICHIESTA_SEC);
        invia_messaggio(client->socket, msg_attesa);

        char notifica[300];
        sprintf(notifica, "\n[NOTIFICA] %s vuole unirsi alla tua partita (ID: %d)\n", client->nome, id_partita);
        strcat(notifica, "Digita 'ACCETTA ");
        char id_str[10];
        sprintf(id_str, "%d", id_partita);
        strcat(notifica, id_str);
        strcat(notifica, "' o 'RIFIUTA ");
        strcat(notifica, id_str);
        strcat(notifica, "'\n");
        invia_messaggio(partita->socket_giocatore1, notifica);

        TimeoutRichiestaArgs* timeout_args = malloc(sizeof(TimeoutRichiestaArgs));
        if (timeout_args != NULL) {
            timeout_args->id_partita = id_partita;
            timeout_args->socket_richiedente = client->socket;
            pthread_t timeout_thread;
            if (pthread_create(&timeout_thread, NULL, gestisci_timeout_richiesta, timeout_args) == 0) {
                pthread_detach(timeout_thread);
            } else {
                free(timeout_args);
            }
        }

    } else if (risultato == -1) {
        invia_messaggio(client->socket, "Partita non trovata.\n");
    } else if (risultato == -2) {
        invia_messaggio(client->socket, "Partita non disponibile.\n");
    } else if (risultato == -4) {
        invia_messaggio(client->socket, "Non puoi entrare nella tua stessa partita.\n");
    } else {
        invia_messaggio(client->socket, "Partita gia' piena.\n");
    }
    return 0;
}

static int gestisci_comando_accetta(DatiClient* client, const char* buffer) {
    int id_partita = atoi(buffer + 8);
    int id_in_corso = trova_partita_in_corso_client(client->socket);
    if (id_in_corso > 0 && id_in_corso != id_partita) {
        char msg[170];
        snprintf(msg, sizeof(msg),
                 "Stai gia' giocando/gestendo la partita ID %d. "
                 "Non puoi accettare richieste su altre partite.\n",
                 id_in_corso);
        invia_messaggio(client->socket, msg);
        return 0;
    }

    Partita* partita = trova_partita(id_partita);

    if (partita && partita->socket_giocatore1 == client->socket) {
        if (accetta_richiesta(id_partita) == 0) {
            char msg1[200], msg2[200];
            int socket_ko = -1;
            sprintf(msg1, "Hai accettato %s. Partita iniziata!\n", partita->nome_giocatore2);
            sprintf(msg2, "La tua richiesta e' stata accettata! Partita iniziata!\n");

            invia_con_fallback(partita->socket_giocatore1, msg1, &socket_ko);
            invia_con_fallback(partita->socket_giocatore2, msg2, &socket_ko);

            sprintf(msg1, "Giochi contro %s. Sei il giocatore ROSSO (R).\n", partita->nome_giocatore2);
            sprintf(msg2, "Giochi contro %s. Sei il giocatore GIALLO (G).\n", partita->nome_giocatore1);

            invia_con_fallback(partita->socket_giocatore1, msg1, &socket_ko);
            invia_con_fallback(partita->socket_giocatore2, msg2, &socket_ko);
            invia_griglia_a_entrambi_con_fallback(partita, &socket_ko);
            invia_con_fallback(partita->socket_giocatore1, "E il tuo turno! Scegli una colonna (0-6): ", &socket_ko);
            invia_con_fallback(partita->socket_giocatore2, "Aspetta il tuo turno...\n", &socket_ko);

            if (socket_ko > 0) {
                termina_partita_per_socket_non_raggiungibile(partita, socket_ko);
                client->id_partita_corrente = 0;
                return 0;
            }
            client->id_partita_corrente = id_partita;
        } else {
            invia_messaggio(client->socket, "Errore nell'accettazione.\n");
        }
    } else {
        invia_messaggio(client->socket, "Non sei il creatore di questa partita.\n");
    }
    return 0;
}

static int gestisci_comando_rifiuta(DatiClient* client, const char* buffer) {
    int id_partita = atoi(buffer + 8);
    Partita* partita = trova_partita(id_partita);

    if (partita && partita->socket_giocatore1 == client->socket) {
        int socket_richiedente = partita->socket_richiedente;
        if (rifiuta_richiesta(id_partita) == 0) {
            if (socket_richiedente > 0) {
                invia_messaggio(socket_richiedente, "La tua richiesta e' stata rifiutata.\n");
            }
            invia_messaggio(client->socket, "Hai rifiutato la richiesta.\n");
        } else {
            invia_messaggio(client->socket, "Errore nel rifiuto.\n");
        }
    } else {
        invia_messaggio(client->socket, "Non sei il creatore di questa partita.\n");
    }
    return 0;
}

static int gestisci_comando_abbandona(DatiClient* client) {
    Partita* partita_in_corso = trova_partita_in_corso_per_socket(client->socket);
    if (partita_in_corso != NULL) {
        int e_giocatore1 = (partita_in_corso->socket_giocatore1 == client->socket);
        int avversario = e_giocatore1 ? partita_in_corso->socket_giocatore2
                                      : partita_in_corso->socket_giocatore1;

        partita_in_corso->stato = PARTITA_TERMINATA;
        partita_in_corso->vincitore = e_giocatore1 ? 2 : 1;

        invia_messaggio(client->socket, "Hai abbandonato la partita. Hai perso.\n");
        if (avversario > 0) {
            invia_messaggio(avversario, "L'altro giocatore ha abbandonato. Hai vinto per resa.\n");
            invia_messaggio(avversario, "Puoi creare o unirti a una nuova partita.\n");
        }
        client->id_partita_corrente = 0;
        invia_messaggio(client->socket,
                        "Sei uscito dalla partita. Puoi creare o unirti a una nuova partita.\n");
    } else {
        invia_messaggio(client->socket,
                        "ABBANDONA e' valido solo durante una partita in corso.\n");
    }
    return 0;
}

static int gestisci_mossa_gioco(DatiClient* client, const char* buffer) {
    Partita* partita = trova_partita(client->id_partita_corrente);
    if (!partita || partita->stato != PARTITA_IN_CORSO) {
        client->id_partita_corrente = 0;
        return 0;
    }

    int e_giocatore1 = (client->socket == partita->socket_giocatore1);
    if ((e_giocatore1 && partita->turno_corrente != 1) ||
        (!e_giocatore1 && partita->turno_corrente != 2)) {
        invia_messaggio(client->socket, "Non e' il tuo turno!\n");
        return 0;
    }
    if (strlen(buffer) != 1 || buffer[0] < '0' || buffer[0] > '6') {
        invia_messaggio(client->socket, "Input non valido! Inserisci un numero da 0 a 6: ");
        return 0;
    }

    int colonna = atoi(buffer);
    char simbolo_giocatore = e_giocatore1 ? 'R' : 'G';

    int riga = inserisci_gettone(partita->griglia, colonna, simbolo_giocatore);
    if (riga == -1) {
        invia_messaggio(client->socket, "Mossa non valida! Riprova: ");
        return 0;
    }

    if (controlla_vittoria(partita->griglia, simbolo_giocatore)) {
        invia_griglia_a_entrambi(partita);

        partita->stato = PARTITA_TERMINATA;
        partita->vincitore = e_giocatore1 ? 1 : 2;

        invia_messaggio(client->socket, "HAI VINTO! Congratulazioni!\n");
        invia_messaggio(e_giocatore1 ? partita->socket_giocatore2 : partita->socket_giocatore1,
                        "Hai perso. Riprova!\n");
        invia_fine_partita_a_entrambi(partita);

        client->id_partita_corrente = 0;
        return 0;
    }

    if (controlla_pareggio(partita->griglia)) {
        invia_griglia_a_entrambi(partita);

        partita->stato = PARTITA_TERMINATA;
        invia_messaggio(partita->socket_giocatore1, "PAREGGIO!\n");
        invia_messaggio(partita->socket_giocatore2, "PAREGGIO!\n");

        invia_fine_partita_a_entrambi(partita);

        client->id_partita_corrente = 0;
        return 0;
    }

    partita->turno_corrente = (partita->turno_corrente == 1) ? 2 : 1;
    int socket_ko = -1;

    if (partita->turno_corrente == 1) {
        invia_con_fallback(partita->socket_giocatore1, "E il tuo turno! Scegli una colonna (0-6): ", &socket_ko);
        invia_con_fallback(partita->socket_giocatore2, "Aspetta il tuo turno...\n", &socket_ko);
    } else {
        invia_con_fallback(partita->socket_giocatore2, "E il tuo turno! Scegli una colonna (0-6): ", &socket_ko);
        invia_con_fallback(partita->socket_giocatore1, "Aspetta il tuo turno...\n", &socket_ko);
    }
    invia_griglia_a_entrambi_con_fallback(partita, &socket_ko);

    if (socket_ko > 0) {
        termina_partita_per_socket_non_raggiungibile(partita, socket_ko);
        client->id_partita_corrente = 0;
    }

    return 0;
}

int gestisci_input_client(DatiClient* client, const char* buffer) {
    if (strcmp(buffer, "CREA") == 0) {
        return gestisci_comando_crea(client);
    }

    if (strcmp(buffer, "LISTA") == 0) {
        invia_messaggio(client->socket, lista_partite());
        return 0;
    }

    if (strncmp(buffer, "ENTRA ", 6) == 0) {
        return gestisci_comando_entra(client, buffer);
    }

    if (strncmp(buffer, "ACCETTA ", 8) == 0) {
        return gestisci_comando_accetta(client, buffer);
    }

    if (strncmp(buffer, "RIFIUTA ", 8) == 0) {
        return gestisci_comando_rifiuta(client, buffer);
    }

    if (strcmp(buffer, "ABBANDONA") == 0) {
        return gestisci_comando_abbandona(client);
    }

    if (strcmp(buffer, "ESCI") == 0) {
        int id_in_corso = trova_partita_in_corso_client(client->socket);
        if (id_in_corso > 0) {
            invia_messaggio(client->socket,
                            "Sei in partita. Usa ABBANDONA per arrenderti e tornare al menu.\n");
            return 0;
        }

        invia_messaggio(client->socket, "Arrivederci!\n");
        return -1;
    }

    if (client->id_partita_corrente > 0) {
        return gestisci_mossa_gioco(client, buffer);
    }

    invia_messaggio(client->socket, "Comando non riconosciuto.\n");
    return 0;
}

void gestisci_disconnessione_client(DatiClient* client) {
    for (int i = 0; i < contatore_partite; i++) {
        Partita* partita = &partite[i];
        if (partita->stato == PARTITA_TERMINATA) {
            continue;
        }

        if (partita->socket_giocatore1 == client->socket) {
            if (partita->stato == PARTITA_IN_CORSO) {
                partita->stato = PARTITA_TERMINATA;
                partita->vincitore = 2;
                if (partita->socket_giocatore2 > 0) {
                    invia_messaggio(partita->socket_giocatore2,
                                    "L'altro giocatore si e' disconnesso. Hai vinto a tavolino.\n");
                }
            } else if (partita->stato == PARTITA_IN_ATTESA ||
                       partita->stato == PARTITA_RICHIESTA_PENDENTE) {
                if (partita->stato == PARTITA_RICHIESTA_PENDENTE && partita->socket_richiedente > 0) {
                    invia_messaggio(partita->socket_richiedente,
                                    "Il creatore della partita si e' disconnesso. Partita eliminata.\n");
                }
                partita->stato = PARTITA_TERMINATA;
            }
            partita->socket_giocatore1 = -1;
            partita->socket_giocatore2 = -1;
            partita->socket_richiedente = -1;
            partita->nome_giocatore1[0] = '\0';
            partita->nome_giocatore2[0] = '\0';
            partita->nome_richiedente[0] = '\0';
            atomic_store(&partita->timeout_annullato, 1);
            continue;
        }

        if (partita->socket_giocatore2 == client->socket &&
            partita->stato == PARTITA_IN_CORSO) {
            partita->stato = PARTITA_TERMINATA;
            partita->vincitore = 1;
            if (partita->socket_giocatore1 > 0) {
                invia_messaggio(partita->socket_giocatore1,
                                "L'altro giocatore si e' disconnesso. Hai vinto a tavolino.\n");
            }
            partita->socket_giocatore2 = -1;
            partita->nome_giocatore2[0] = '\0';
            continue;
        }

        if (partita->socket_richiedente == client->socket &&
            partita->stato == PARTITA_RICHIESTA_PENDENTE) {
            partita->socket_richiedente = -1;
            partita->nome_richiedente[0] = '\0';
            partita->stato = PARTITA_IN_ATTESA;
            if (partita->socket_giocatore1 > 0) {
                invia_messaggio(partita->socket_giocatore1,
                                "Il richiedente si e' disconnesso. Richiesta annullata.\n");
            }
            atomic_store(&partita->timeout_annullato, 1);
        }
    }
}
