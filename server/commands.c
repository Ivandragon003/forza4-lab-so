#include "commands.h"
#include "network.h"
#include "game.h"
#include "game_queries.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#define TIMEOUT_RICHIESTA_SEC 30

static void notifica_altri_client(int escludi1, int escludi2, const char* messaggio) {
    for (int i = 0; i < MAX_SOCKET_TRACCIATI; i++) {
        DatiClient* c = atomic_load(&client_per_socket[i]);
        if (c != NULL && c->socket != escludi1 && c->socket != escludi2) {
            invia_messaggio(c->socket, messaggio);
        }
    }
}

static int parse_id_comando(const char* buffer, const char* prefisso, int* out_id) {
    if (buffer == NULL || prefisso == NULL || out_id == NULL) {
        return -1;
    }

    size_t prefisso_len = strlen(prefisso); // conta caratteri di una stringa 
    if (strncmp(buffer, prefisso, prefisso_len) != 0) {  //confranta stringhe nei primi N caratteri
        return -1;
    }

    const char* numero = buffer + prefisso_len; // sposta puntatore
    if (*numero == '\0') {
        return -1;
    }

    errno = 0;
    char* endptr = NULL;
    long id = strtol(numero, &endptr, 10); // salva il numero
    if (numero == endptr || errno != 0 || id <= 0 || id > INT_MAX) {
        return -1;  // errore: nessun numero valido, overflow, o numero fuori range
    }

    while (*endptr != '\0' && isspace((unsigned char)*endptr)) {  // errore: ci sono caratteri non validi dopo il numero
        endptr++;
    }

    if (*endptr != '\0') {
        return -1;
    }

    *out_id = (int)id;
    return 0;
}

static const char* MSG_VITTORIA_TAVOLINO =
    "L'altro giocatore si e' disconnesso. Hai vinto a tavolino.\n";
static const char* MSG_MENU_NUOVA_PARTITA =
    "Puoi creare o unirti a una nuova partita.\n";
static const char* MSG_TURNO =
    "E il tuo turno! Scegli una colonna (0-6).\n";
static const char* MSG_ATTESA_TURNO =
    "Aspetta il tuo turno...\n";
static const char* MSG_NON_SEI_CREATORE =
    "Non sei il creatore di questa partita.\n";

typedef struct {
    int id_partita;
    int socket_richiedente;
} TimeoutRichiestaArgs;

typedef struct {
    int socket;
    const char* messaggio1;
    const char* messaggio2;
    int reset_id_partita;
} NotificaDisconnessione;

typedef struct {
    int id_partita;
    int escludi1;
    int escludi2;
} BroadcastPartitaTerminata;

static int invia_con_fallback(int socket, const char* messaggio, int* socket_ko) {
    if (*socket_ko < 0 && invia_messaggio(socket, messaggio) < 0) {
        *socket_ko = socket;
        return -1;
    }
    return 0;
}

static void accoda_notifica_disconnessione(NotificaDisconnessione* notifiche,
                                           int* count,
                                           int socket,
                                           const char* messaggio1,
                                           const char* messaggio2,
                                           int reset_id_partita) {
    if (*count >= MAX_PARTITE * 2) {
        return;
    }
    notifiche[*count].socket = socket;
    notifiche[*count].messaggio1 = messaggio1;
    notifiche[*count].messaggio2 = messaggio2;
    notifiche[*count].reset_id_partita = reset_id_partita;
    (*count)++;
}

static void accoda_broadcast_partita_terminata(BroadcastPartitaTerminata* broadcast,
                                               int* count,
                                               int id_partita,
                                               int escludi1,
                                               int escludi2) {
    if (*count >= MAX_PARTITE) {
        return;
    }

    for (int i = 0; i < *count; i++) {
        if (broadcast[i].id_partita == id_partita) {
            return;
        }
    }

    broadcast[*count].id_partita = id_partita;
    broadcast[*count].escludi1 = escludi1;
    broadcast[*count].escludi2 = escludi2;
    (*count)++;
}

static void broadcast_partita_conclusa(int id_partita, int escludi1, int escludi2) {
    char notifica_fine[100];
    snprintf(notifica_fine, sizeof(notifica_fine),
             "NOTIFICA: La partita ID %d si e' conclusa.\n", id_partita);
    notifica_altri_client(escludi1, escludi2, notifica_fine);
}

static void invia_formato_non_valido(int socket, const char* comando) {
    char msg[120];
    snprintf(msg, sizeof(msg), "Formato comando non valido. Usa: %s <id_partita>\n", comando);
    invia_messaggio(socket, msg);
}

static int parse_id_comando_o_errore(int socket,
                                     const char* buffer,
                                     const char* prefisso,
                                     const char* comando,
                                     int* out_id) {
    if (parse_id_comando(buffer, prefisso, out_id) != 0) {
        invia_formato_non_valido(socket, comando);
        return -1;
    }
    return 0;
}

static void invia_griglia_con_fallback(int socket1, int socket2, const char* griglia, int* socket_ko) {
    if (socket1 > 0) {
        invia_con_fallback(socket1, "\n", socket_ko);
        invia_con_fallback(socket1, griglia, socket_ko);
    }
    if (socket2 > 0) {
        invia_con_fallback(socket2, "\n", socket_ko);
        invia_con_fallback(socket2, griglia, socket_ko);
    }
}

static void notifica_vittoria_tavolino(int socket_avversario) {
    if (socket_avversario <= 0) {
        return;
    }
    invia_messaggio(socket_avversario, MSG_VITTORIA_TAVOLINO);
    invia_messaggio(socket_avversario, MSG_MENU_NUOVA_PARTITA);
    aggiorna_id_partita_client_per_socket(socket_avversario, 0);
}

static void invia_esito_finale_partita(int id_partita,
                                       int socket_me,
                                       int socket_opp,
                                       const char* griglia,
                                       const char* messaggio_me,
                                       const char* messaggio_opp) {
    if (socket_me > 0) {
        invia_messaggio(socket_me, "\n");
        invia_messaggio(socket_me, griglia);
        invia_messaggio(socket_me, messaggio_me);
        invia_messaggio(socket_me, MSG_MENU_NUOVA_PARTITA);
    }

    if (socket_opp > 0) {
        invia_messaggio(socket_opp, "\n");
        invia_messaggio(socket_opp, griglia);
        invia_messaggio(socket_opp, messaggio_opp);
        invia_messaggio(socket_opp, MSG_MENU_NUOVA_PARTITA);
        aggiorna_id_partita_client_per_socket(socket_opp, 0);
    }

    broadcast_partita_conclusa(id_partita, socket_me, socket_opp);
}

static int termina_partita_per_socket_non_raggiungibile(Partita* partita, int socket_non_raggiungibile) {
    if (partita == NULL || partita->stato != PARTITA_IN_CORSO) {
        return -1;
    }

    int e_giocatore1 = (partita->socket_giocatore1 == socket_non_raggiungibile);
    int avversario = e_giocatore1 ? partita->socket_giocatore2 : partita->socket_giocatore1;

    partita->stato = PARTITA_TERMINATA;
    partita->vincitore = e_giocatore1 ? 2 : 1;

    if (e_giocatore1) {
        partita->socket_giocatore1 = -1;
        partita->nome_giocatore1[0] = '\0';
    } else {
        partita->socket_giocatore2 = -1;
        partita->nome_giocatore2[0] = '\0';
    }

    return avversario;
}

static void gestisci_socket_ko_partita(int id_partita,
                                      int socket_ko,
                                      DatiClient* client,
                                      int reset_client_sempre)
{
    int socket_avversario = -1;
    bool partita_terminata = false;

    blocca_partite();

    Partita* partita_ko = trova_partita(id_partita);

    if (partita_ko != NULL && partita_ko->stato == PARTITA_IN_CORSO) {
        socket_avversario = termina_partita_per_socket_non_raggiungibile(partita_ko, socket_ko);
        partita_terminata = true;
    }

    // reset stato client (sempre se richiesto o se è il socket KO)
    if (reset_client_sempre || socket_ko == client->socket) {
        atomic_store(&client->id_partita_corrente, 0);
    }

    sblocca_partite();

    // notifiche SOLO se la partita è stata realmente terminata qui
    if (partita_terminata && socket_avversario >= 0) {
        notifica_vittoria_tavolino(socket_avversario);
        broadcast_partita_conclusa(id_partita, socket_ko, socket_avversario);
    }
}


static void* gestisci_timeout_richiesta(void* arg) {
    TimeoutRichiestaArgs* timeout_args = (TimeoutRichiestaArgs*)arg;
    sleep(TIMEOUT_RICHIESTA_SEC);

    int socket_creatore = -1;
    int socket_richiedente = -1;
    int timeout_scaduto = 0;

    blocca_partite();
    Partita* partita = trova_partita(timeout_args->id_partita);
    if (partita != NULL &&
        partita->stato == PARTITA_RICHIESTA_PENDENTE &&
        partita->socket_richiedente == timeout_args->socket_richiedente) {
        int atteso = 0;
        if (!atomic_compare_exchange_strong(&partita->timeout_annullato, &atteso, 1)) {
            sblocca_partite();
            free(timeout_args);
            return NULL;
        }

        socket_creatore = partita->socket_giocatore1;
        socket_richiedente = partita->socket_richiedente;

        partita->socket_richiedente = -1;
        partita->nome_richiedente[0] = '\0';
        partita->stato = PARTITA_IN_ATTESA;
        timeout_scaduto = 1;
    }
    sblocca_partite();

    if (timeout_scaduto) {
        if (socket_richiedente > 0) {
            invia_messaggio(socket_richiedente,
                            "Tempo scaduto: nessuna risposta dal creatore. Richiesta annullata.\n");
            aggiorna_id_partita_client_per_socket(socket_richiedente, 0);
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
    blocca_partite();
    int id_in_corso = trova_partita_in_corso_client(client->socket);
    if (id_in_corso > 0) {
        sblocca_partite();
        char msg[170];
        snprintf(msg, sizeof(msg),
                 "Stai gia' giocando la partita ID %d. "
                 "Non puoi crearne un'altra finche' non termina.\n",
                 id_in_corso);
        invia_messaggio(client->socket, msg);
        return 0;
    }

    int id_partita = crea_partita(client->socket, client->nome);
    sblocca_partite();
    if (id_partita > 0) {
        char msg[100];
        snprintf(msg, sizeof(msg), "Partita creata! ID: %d\nIn attesa di un avversario...\n", id_partita);
        invia_messaggio(client->socket, msg);

        char notifica_broadcast[150];
        snprintf(notifica_broadcast, sizeof(notifica_broadcast),
                 "NOTIFICA: Nuova partita disponibile (ID: %d, creata da: %s)\n",
                 id_partita, client->nome);
        notifica_altri_client(client->socket, -1, notifica_broadcast);
    } else {
        invia_messaggio(client->socket, "Errore nella creazione della partita.\n");
    }
    return 0;
}

static int gestisci_comando_entra(DatiClient* client, const char* buffer) {
    blocca_partite();
    int id_in_corso = trova_partita_in_corso_client(client->socket);
    if (id_in_corso > 0) {
        sblocca_partite();
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
        sblocca_partite();
        char msg[180];
        snprintf(msg, sizeof(msg),
                 "Hai gia' una richiesta pendente sulla partita ID %d. "
                 "Attendi ACCETTA/RIFIUTA prima di entrare altrove.\n",
                 id_pendente);
        invia_messaggio(client->socket, msg);
        return 0;
    }

    int id_partita = 0;
    if (parse_id_comando_o_errore(client->socket, buffer, "ENTRA ", "ENTRA", &id_partita) != 0) {
        sblocca_partite();
        return 0;
    }
    int risultato = richiedi_partita(id_partita, client->socket, client->nome);
    int socket_creatore = -1;

    if (risultato == 0) {
        Partita* partita = trova_partita(id_partita);
        if (partita != NULL) {
            socket_creatore = partita->socket_giocatore1;
        }

        atomic_store(&client->id_partita_corrente, id_partita);
    }
    sblocca_partite();

    if (risultato == 0) {
        char msg_attesa[180];
        snprintf(msg_attesa, sizeof(msg_attesa),
                 "Richiesta inviata. Attendi risposta (timeout %d secondi)...\n",
                 TIMEOUT_RICHIESTA_SEC);
        invia_messaggio(client->socket, msg_attesa);

        char notifica[300];
        snprintf(notifica, sizeof(notifica),
                 "\n[NOTIFICA] %s vuole unirsi alla tua partita (ID: %d)\n"
                 "Digita 'ACCETTA %d' o 'RIFIUTA %d'\n",
                 client->nome, id_partita, id_partita, id_partita);
        if (socket_creatore > 0) {
            invia_messaggio(socket_creatore, notifica);
        }

        int timeout_avviato = 0;
        TimeoutRichiestaArgs* timeout_args = malloc(sizeof(TimeoutRichiestaArgs));
        if (timeout_args != NULL) {
            timeout_args->id_partita = id_partita;
            timeout_args->socket_richiedente = client->socket;
            pthread_t timeout_thread;
            if (pthread_create(&timeout_thread, NULL, gestisci_timeout_richiesta, timeout_args) == 0) { // creo thread in parallelo
                pthread_detach(timeout_thread); // quando finisce libero thread
                timeout_avviato = 1;
            } else {
                free(timeout_args);
            }
        }

        if (!timeout_avviato) {
            int annullata = 0;
            blocca_partite();
            Partita* partita = trova_partita(id_partita);
            if (partita != NULL &&
                partita->stato == PARTITA_RICHIESTA_PENDENTE &&
                partita->socket_richiedente == client->socket) {
                partita->socket_richiedente = -1;
                partita->nome_richiedente[0] = '\0';
                partita->stato = PARTITA_IN_ATTESA;
                atomic_store(&partita->timeout_annullato, 1);
                annullata = 1;
            }
            sblocca_partite();

            if (annullata) {
                aggiorna_id_partita_client_per_socket(client->socket, 0);
                invia_messaggio(client->socket,
                                "Errore interno: timeout non disponibile. Richiesta annullata.\n");
                if (socket_creatore > 0) {
                    invia_messaggio(socket_creatore,
                                    "Richiesta annullata automaticamente per errore interno.\n");
                }
            }
        }

    } else {
        switch (risultato) {
            case -1:
                invia_messaggio(client->socket, "Partita non trovata.\n");
                break;
            case -2:
                invia_messaggio(client->socket, "Partita non disponibile.\n");
                break;
            case -4:
                invia_messaggio(client->socket, "Non puoi entrare nella tua stessa partita.\n");
                break;
            default:
                invia_messaggio(client->socket, "Partita gia' piena.\n");
                break;
        }
    }
    return 0;
}

static int gestisci_comando_accetta(DatiClient* client, const char* buffer) {
    int id_partita = 0;
    if (parse_id_comando_o_errore(client->socket, buffer, "ACCETTA ", "ACCETTA", &id_partita) != 0) {
        return 0;
    }
    blocca_partite();
    int id_in_corso = trova_partita_in_corso_client(client->socket);
    if (id_in_corso > 0 && id_in_corso != id_partita) {
        sblocca_partite();
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
            int sock_g1 = partita->socket_giocatore1;
            int sock_g2 = partita->socket_giocatore2;
            char nome_g1[50], nome_g2[50];
            char griglia[500];
            snprintf(nome_g1, sizeof(nome_g1), "%s", partita->nome_giocatore1);
            snprintf(nome_g2, sizeof(nome_g2), "%s", partita->nome_giocatore2);
            griglia_a_stringa(partita->griglia, griglia, sizeof(griglia));
            atomic_store(&client->id_partita_corrente, id_partita);
            sblocca_partite();

            char msg1[200], msg2[200];
            int socket_ko = -1;
            snprintf(msg1, sizeof(msg1), "Hai accettato %s. Partita iniziata!\n", nome_g2);
            snprintf(msg2, sizeof(msg2), "La tua richiesta e' stata accettata! Partita iniziata!\n");

            invia_con_fallback(sock_g1, msg1, &socket_ko);
            invia_con_fallback(sock_g2, msg2, &socket_ko);

            snprintf(msg1, sizeof(msg1), "Giochi contro %s. Sei il giocatore ROSSO (R).\n", nome_g2);
            snprintf(msg2, sizeof(msg2), "Giochi contro %s. Sei il giocatore GIALLO (G).\n", nome_g1);

            invia_con_fallback(sock_g1, msg1, &socket_ko);
            invia_con_fallback(sock_g2, msg2, &socket_ko);
            invia_con_fallback(sock_g1, MSG_TURNO, &socket_ko);
            invia_con_fallback(sock_g2, MSG_ATTESA_TURNO, &socket_ko);
            invia_griglia_con_fallback(sock_g1, sock_g2, griglia, &socket_ko);

            if (socket_ko > 0) {
                gestisci_socket_ko_partita(id_partita, socket_ko, client, 0);
                return 0;
            }

            char notifica_broadcast[200];
            snprintf(notifica_broadcast, sizeof(notifica_broadcast),
                     "NOTIFICA: La partita ID %d (%s vs %s) e' iniziata. Non e' piu' possibile partecipare.\n",
                     id_partita, nome_g1, nome_g2);
            notifica_altri_client(sock_g1, sock_g2, notifica_broadcast);
            return 0;
        }
        sblocca_partite();
        invia_messaggio(client->socket, "Errore nell'accettazione.\n");
        return 0;
    } else {
        sblocca_partite();
        invia_messaggio(client->socket, MSG_NON_SEI_CREATORE);
        return 0;
    }
}

static int gestisci_comando_rifiuta(DatiClient* client, const char* buffer) {
    int id_partita = 0;
    if (parse_id_comando_o_errore(client->socket, buffer, "RIFIUTA ", "RIFIUTA", &id_partita) != 0) {
        return 0;
    }
    blocca_partite();
    Partita* partita = trova_partita(id_partita);

    if (partita && partita->socket_giocatore1 == client->socket) {
        int socket_richiedente = partita->socket_richiedente;
        if (rifiuta_richiesta(id_partita) == 0) {
            sblocca_partite();
            if (socket_richiedente > 0) {
                invia_messaggio(socket_richiedente, "La tua richiesta e' stata rifiutata.\n");
                aggiorna_id_partita_client_per_socket(socket_richiedente, 0);
            }
            invia_messaggio(client->socket, "Hai rifiutato la richiesta.\n");
        } else {
            sblocca_partite();
            invia_messaggio(client->socket, "Errore nel rifiuto.\n");
        }
    } else {
        sblocca_partite();
        invia_messaggio(client->socket, MSG_NON_SEI_CREATORE);
    }
    return 0;
}

static int gestisci_comando_abbandona(DatiClient* client) {
    blocca_partite();
    Partita* partita_in_corso = trova_partita_in_corso_per_socket(client->socket);
    if (partita_in_corso != NULL) {
        int e_giocatore1 = (partita_in_corso->socket_giocatore1 == client->socket); //chi abbandona è il giocatore 1
        int avversario = e_giocatore1 ? partita_in_corso->socket_giocatore2 //se e_giocaotre1 == 1, allora l'avversario è il giocatore 2, 1 altrimenti.
                                      : partita_in_corso->socket_giocatore1;
        int id_abbandonata = partita_in_corso->id_partita;

        partita_in_corso->stato = PARTITA_TERMINATA;
        partita_in_corso->vincitore = e_giocatore1 ? 2 : 1;
        atomic_store(&client->id_partita_corrente, 0);
        sblocca_partite();

        invia_messaggio(client->socket, "Hai abbandonato la partita. Hai perso.\n");
        if (avversario > 0) {
            invia_messaggio(avversario, "L'altro giocatore ha abbandonato. Hai vinto per resa.\n");
            invia_messaggio(avversario, MSG_MENU_NUOVA_PARTITA);
            aggiorna_id_partita_client_per_socket(avversario, 0);
        }
        invia_messaggio(client->socket,
                        "Sei uscito dalla partita. Puoi creare o unirti a una nuova partita.\n");
        broadcast_partita_conclusa(id_abbandonata, client->socket, avversario);
    } else {
        sblocca_partite();
        invia_messaggio(client->socket,
                        "ABBANDONA e' valido solo durante una partita in corso.\n");
    }
    return 0;
}

static int gestisci_mossa_gioco(DatiClient* client, const char* buffer) {
    int id_partita = atomic_load(&client->id_partita_corrente);
    int socket_me = client->socket;
    int socket_opp = -1;
    int socket_turno = -1;
    int socket_attesa = -1;
    int stato_post = 0;  // 0=nessuna azione, 1=vittoria, 2=pareggio, 3=continua
    char griglia[500];

    blocca_partite();
    Partita* partita = trova_partita(id_partita);
    if (!partita || partita->stato != PARTITA_IN_CORSO) {
        atomic_store(&client->id_partita_corrente, 0);
        sblocca_partite();
        return 0;
    }

    int e_giocatore1 = (socket_me == partita->socket_giocatore1);
    socket_opp = e_giocatore1 ? partita->socket_giocatore2 : partita->socket_giocatore1;

    if ((e_giocatore1 && partita->turno_corrente != 1) ||
        (!e_giocatore1 && partita->turno_corrente != 2)) {
        sblocca_partite();
        invia_messaggio(socket_me, "Non e' il tuo turno!\n");
        return 0;
    }
    if (strlen(buffer) != 1 || buffer[0] < '0' || buffer[0] > '6') {
        sblocca_partite();
        invia_messaggio(socket_me, "Errore: input errato. Inserisci un numero da 0 a 6.\n");
        return 0;
    }

    int colonna = buffer[0] - '0';
    char simbolo_giocatore = e_giocatore1 ? 'R' : 'G';

    int riga = inserisci_gettone(partita->griglia, colonna, simbolo_giocatore);
    if (riga == -1) {
        sblocca_partite();
        invia_messaggio(socket_me, "Mossa non valida! Colonna piena, riprova.\n");
        return 0;
    }

    griglia_a_stringa(partita->griglia, griglia, sizeof(griglia));

    if (controlla_vittoria(partita->griglia, simbolo_giocatore)) {
        partita->stato = PARTITA_TERMINATA;
        partita->vincitore = e_giocatore1 ? 1 : 2;
        atomic_store(&client->id_partita_corrente, 0);
        stato_post = 1;
    } else if (controlla_pareggio(partita->griglia)) {
        partita->stato = PARTITA_TERMINATA;
        atomic_store(&client->id_partita_corrente, 0);
        stato_post = 2;
    } else {
        partita->turno_corrente = (partita->turno_corrente == 1) ? 2 : 1;
        if (partita->turno_corrente == 1) {
            socket_turno = partita->socket_giocatore1;
            socket_attesa = partita->socket_giocatore2;
        } else {
            socket_turno = partita->socket_giocatore2;
            socket_attesa = partita->socket_giocatore1;
        }
        stato_post = 3;
    }
    sblocca_partite();

    if (stato_post == 1) {
        invia_esito_finale_partita(id_partita,
                                   socket_me,
                                   socket_opp,
                                   griglia,
                                   "HAI VINTO! Congratulazioni!\n",
                                   "Hai perso. Riprova!\n");
        return 0;
    }

    if (stato_post == 2) {
        invia_esito_finale_partita(id_partita,
                                   socket_me,
                                   socket_opp,
                                   griglia,
                                   "PAREGGIO!\n",
                                   "PAREGGIO!\n");
        return 0;
    }

    int socket_ko = -1;
    if (socket_turno > 0) {
        invia_con_fallback(socket_turno, MSG_TURNO, &socket_ko);
    }
    if (socket_attesa > 0) {
        invia_con_fallback(socket_attesa, MSG_ATTESA_TURNO, &socket_ko);
    }
    invia_griglia_con_fallback(socket_me, socket_opp, griglia, &socket_ko);

    if (socket_ko > 0) {
        gestisci_socket_ko_partita(id_partita, socket_ko, client, 1);
    }

    return 0;
}

int gestisci_input_client(DatiClient* client, const char* buffer) {
    char buffer_normalizzato[DIM_BUFFER];
    size_t len = strlen(buffer);
    if (len >= sizeof(buffer_normalizzato)) {
        len = sizeof(buffer_normalizzato) - 1; //evita overflow
    }
    for (size_t i = 0; i < len; i++) {
        buffer_normalizzato[i] = (char)toupper((unsigned char)buffer[i]);
    }
    buffer_normalizzato[len] = '\0';

    if (strcmp(buffer_normalizzato, "PING") == 0) {
        return 0;
    }

    if (strcmp(buffer_normalizzato, "CREA") == 0) {
        return gestisci_comando_crea(client);
    }

    if (strcmp(buffer_normalizzato, "LISTA") == 0) {
        char lista_buffer[2048];
        blocca_partite();
        lista_partite(lista_buffer, sizeof(lista_buffer));
        sblocca_partite();
        invia_messaggio(client->socket, lista_buffer);
        return 0;
    }

    if (strncmp(buffer_normalizzato, "ENTRA ", 6) == 0) {
        return gestisci_comando_entra(client, buffer_normalizzato);
    }

    if (strncmp(buffer_normalizzato, "ACCETTA ", 8) == 0) {
        return gestisci_comando_accetta(client, buffer_normalizzato);
    }

    if (strncmp(buffer_normalizzato, "RIFIUTA ", 8) == 0) {
        return gestisci_comando_rifiuta(client, buffer_normalizzato);
    }

    if (strcmp(buffer_normalizzato, "ABBANDONA") == 0) {
        return gestisci_comando_abbandona(client);
    }

    if (strcmp(buffer_normalizzato, "ESCI") == 0) {
        blocca_partite();
        int id_in_corso = trova_partita_in_corso_client(client->socket);
        sblocca_partite();
        if (id_in_corso > 0) {
            invia_messaggio(client->socket,
                            "Sei in partita. Usa ABBANDONA per arrenderti e tornare al menu.\n");
            return 0;
        }

        invia_messaggio(client->socket, "Arrivederci!\n");
        return -1;
    }

    if (atomic_load(&client->id_partita_corrente) > 0) {
        return gestisci_mossa_gioco(client, buffer_normalizzato);
    }

    invia_messaggio(client->socket, "Errore: input errato. Usa un comando del menu.\n");
    return 0;
}

void gestisci_disconnessione_client(DatiClient* client) {
    NotificaDisconnessione notifiche[MAX_PARTITE * 2];
    int notifiche_count = 0;
    BroadcastPartitaTerminata broadcast[MAX_PARTITE];
    int broadcast_count = 0;

    blocca_partite();
    for (int i = 0; i < contatore_partite; i++) {
        Partita* partita = &partite[i];
        if (partita->stato == PARTITA_TERMINATA) {
            continue;
        }

        if (partita->socket_giocatore1 == client->socket) {
            if (partita->stato == PARTITA_IN_CORSO) {
                partita->stato = PARTITA_TERMINATA;
                partita->vincitore = 2;
                accoda_broadcast_partita_terminata(
                    broadcast,
                    &broadcast_count,
                    partita->id_partita,
                    client->socket,
                    partita->socket_giocatore2);
                if (partita->socket_giocatore2 > 0) {
                    accoda_notifica_disconnessione(
                        notifiche,
                        &notifiche_count,
                        partita->socket_giocatore2,
                        MSG_VITTORIA_TAVOLINO,
                        MSG_MENU_NUOVA_PARTITA,
                        1);
                }
            } else if (partita->stato == PARTITA_IN_ATTESA ||
                       partita->stato == PARTITA_RICHIESTA_PENDENTE) {
                if (partita->stato == PARTITA_RICHIESTA_PENDENTE && partita->socket_richiedente > 0) {
                    accoda_notifica_disconnessione(
                        notifiche,
                        &notifiche_count,
                        partita->socket_richiedente,
                        "Il creatore della partita si e' disconnesso. Partita eliminata.\n",
                        MSG_MENU_NUOVA_PARTITA,
                        1);
                }
                partita->stato = PARTITA_TERMINATA;
                accoda_broadcast_partita_terminata(
                    broadcast,
                    &broadcast_count,
                    partita->id_partita,
                    client->socket,
                    partita->socket_richiedente);
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
            accoda_broadcast_partita_terminata(
                broadcast,
                &broadcast_count,
                partita->id_partita,
                client->socket,
                partita->socket_giocatore1);
            if (partita->socket_giocatore1 > 0) {
                accoda_notifica_disconnessione(
                    notifiche,
                    &notifiche_count,
                    partita->socket_giocatore1,
                    MSG_VITTORIA_TAVOLINO,
                    MSG_MENU_NUOVA_PARTITA,
                    1);
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
                accoda_notifica_disconnessione(
                    notifiche,
                    &notifiche_count,
                    partita->socket_giocatore1,
                    "Il richiedente si e' disconnesso. Richiesta annullata.\n",
                    NULL,
                    0);
            }
            atomic_store(&partita->timeout_annullato, 1);
        }
    }
    sblocca_partite();

    for (int i = 0; i < broadcast_count; i++) {
        broadcast_partita_conclusa(
            broadcast[i].id_partita,
            broadcast[i].escludi1,
            broadcast[i].escludi2);
    }

    for (int i = 0; i < notifiche_count; i++) {
        if (notifiche[i].socket <= 0) {
            continue;
        }
        if (notifiche[i].messaggio1 != NULL) {
            invia_messaggio(notifiche[i].socket, notifiche[i].messaggio1);
        }
        if (notifiche[i].messaggio2 != NULL) {
            invia_messaggio(notifiche[i].socket, notifiche[i].messaggio2);
        }
        if (notifiche[i].reset_id_partita) {
            aggiorna_id_partita_client_per_socket(notifiche[i].socket, 0);
        }
    }
}
