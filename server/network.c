#include "network.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>

#define TIMEOUT_RICHIESTA_SEC 30

typedef struct {
    int id_partita;
    int socket_richiedente;
} TimeoutRichiestaArgs;

static int trova_partita_in_corso_client(int socket_client) {
    for (int i = 0; i < contatore_partite; i++) {
        Partita* partita = &partite[i];
        if (partita->stato == PARTITA_IN_CORSO &&
            (partita->socket_giocatore1 == socket_client ||
             partita->socket_giocatore2 == socket_client)) {
            return partita->id_partita;
        }
    }
    return 0;
}

static Partita* trova_partita_in_corso_per_socket(int socket_client) {
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

static int trova_richiesta_pendente_client(int socket_client) {
    for (int i = 0; i < contatore_partite; i++) {
        Partita* partita = &partite[i];
        if (partita->stato == PARTITA_RICHIESTA_PENDENTE &&
            partita->socket_richiedente == socket_client) {
            return partita->id_partita;
        }
    }
    return 0;
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

void* gestisci_client(void* arg) {
    DatiClient* client = (DatiClient*)arg;
    char buffer[DIM_BUFFER];
    
    printf("Client connesso (socket: %d)\n", client->socket);
    
    invia_messaggio(client->socket, "Benvenuto a Forza 4!\n");
    invia_messaggio(client->socket, "Inserisci il tuo nome:\n");
    
    if (ricevi_messaggio(client->socket, buffer) <= 0) {
        close(client->socket);
        free(client);
        return NULL;
    }
    snprintf(client->nome, sizeof(client->nome), "%s", buffer);
    
    char benvenuto[400];
    sprintf(benvenuto, "Ciao %s! Comandi disponibili:\n", client->nome);
    strcat(benvenuto, "CREA - Crea una nuova partita\n");
    strcat(benvenuto, "LISTA - Mostra partite disponibili\n");
    strcat(benvenuto, "ENTRA <id> - Richiedi di unirti a una partita\n");
    strcat(benvenuto, "ACCETTA <id> - Accetta richiesta (solo creatore)\n");
    strcat(benvenuto, "RIFIUTA <id> - Rifiuta richiesta (solo creatore)\n");
    strcat(benvenuto, "ABBANDONA - Ti arrendi nella partita corrente ma resti connesso\n");
    strcat(benvenuto, "ESCI - Esci dal gioco (solo quando non sei in partita)\n");
    invia_messaggio(client->socket, benvenuto);
    
    while (1) {
        int mostra_prompt_menu = 1;
        if (client->id_partita_corrente > 0) {
            Partita* partita_prompt = trova_partita(client->id_partita_corrente);
            if (partita_prompt && partita_prompt->stato == PARTITA_IN_CORSO) {
                mostra_prompt_menu = 0;
            }
        }
        if (mostra_prompt_menu) {
            invia_messaggio(client->socket, "\n> ");
        }
        
        if (ricevi_messaggio(client->socket, buffer) <= 0) {
            break;
        }
        
        printf("Ricevuto da %s: %s\n", client->nome, buffer);
        
        if (strcmp(buffer, "CREA") == 0) {
            int id_in_corso = trova_partita_in_corso_client(client->socket);
            if (id_in_corso > 0) {
                char msg[170];
                snprintf(msg, sizeof(msg),
                         "Stai gia' giocando la partita ID %d. "
                         "Non puoi crearne un'altra finche' non termina.\n",
                         id_in_corso);
                invia_messaggio(client->socket, msg);
                continue;
            }

            int id_partita = crea_partita(client->socket, client->nome);
            if (id_partita > 0) {
                char msg[100];
                sprintf(msg, "Partita creata! ID: %d\nIn attesa di un avversario...\n", id_partita);
                invia_messaggio(client->socket, msg);
            } else {
                invia_messaggio(client->socket, "Errore nella creazione della partita.\n");
            }
            
        } else if (strcmp(buffer, "LISTA") == 0) {
            invia_messaggio(client->socket, lista_partite());
            
        } else if (strncmp(buffer, "ENTRA ", 6) == 0) {
            int id_in_corso = trova_partita_in_corso_client(client->socket);
            if (id_in_corso > 0) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "Sei gia' coinvolto nella partita ID %d. "
                         "Non puoi entrare in un'altra partita.\n",
                         id_in_corso);
                invia_messaggio(client->socket, msg);
                continue;
            }
            int id_pendente = trova_richiesta_pendente_client(client->socket);
            if (id_pendente > 0) {
                char msg[180];
                snprintf(msg, sizeof(msg),
                         "Hai gia' una richiesta pendente sulla partita ID %d. "
                         "Attendi ACCETTA/RIFIUTA prima di entrare altrove.\n",
                         id_pendente);
                invia_messaggio(client->socket, msg);
                continue;
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
                invia_messaggio(client->socket, "Partita già piena.\n");
            }
            
        } else if (strncmp(buffer, "ACCETTA ", 8) == 0) {
            int id_partita = atoi(buffer + 8);
            int id_in_corso = trova_partita_in_corso_client(client->socket);
            if (id_in_corso > 0 && id_in_corso != id_partita) {
                char msg[170];
                snprintf(msg, sizeof(msg),
                         "Stai gia' giocando/gestendo la partita ID %d. "
                         "Non puoi accettare richieste su altre partite.\n",
                         id_in_corso);
                invia_messaggio(client->socket, msg);
                continue;
            }

            Partita* partita = trova_partita(id_partita);
            
            if (partita && partita->socket_giocatore1 == client->socket) {
                if (accetta_richiesta(id_partita) == 0) {
                    char msg1[200], msg2[200];
                    int socket_ko = -1;
                    sprintf(msg1, "Hai accettato %s. Partita iniziata!\n", partita->nome_giocatore2);
                    sprintf(msg2, "La tua richiesta è stata accettata! Partita iniziata!\n");
                    
                    if (invia_messaggio(partita->socket_giocatore1, msg1) < 0) {
                        socket_ko = partita->socket_giocatore1;
                    }
                    if (invia_messaggio(partita->socket_giocatore2, msg2) < 0 && socket_ko < 0) {
                        socket_ko = partita->socket_giocatore2;
                    }
                    
                    sprintf(msg1, "Giochi contro %s. Sei il giocatore ROSSO (R).\n", partita->nome_giocatore2);
                    sprintf(msg2, "Giochi contro %s. Sei il giocatore GIALLO (G).\n", partita->nome_giocatore1);
                    
                    if (invia_messaggio(partita->socket_giocatore1, msg1) < 0 && socket_ko < 0) {
                        socket_ko = partita->socket_giocatore1;
                    }
                    if (invia_messaggio(partita->socket_giocatore2, msg2) < 0 && socket_ko < 0) {
                        socket_ko = partita->socket_giocatore2;
                    }
                    
                    if (invia_messaggio(partita->socket_giocatore1, griglia_a_stringa(partita->griglia)) < 0 && socket_ko < 0) {
                        socket_ko = partita->socket_giocatore1;
                    }
                    if (invia_messaggio(partita->socket_giocatore2, griglia_a_stringa(partita->griglia)) < 0 && socket_ko < 0) {
                        socket_ko = partita->socket_giocatore2;
                    }
                    
                    if (invia_messaggio(partita->socket_giocatore1, "È il tuo turno! Scegli una colonna (0-6): ") < 0 && socket_ko < 0) {
                        socket_ko = partita->socket_giocatore1;
                    }
                    
                    if (invia_messaggio(partita->socket_giocatore2, "Aspetta il tuo turno...\n") < 0 && socket_ko < 0) {
                        socket_ko = partita->socket_giocatore2;
                    }
                    
                    if (socket_ko > 0) {
                        termina_partita_per_socket_non_raggiungibile(partita, socket_ko);
                        client->id_partita_corrente = 0;
                        continue;
                    }
                    client->id_partita_corrente = id_partita;
                } else {
                    invia_messaggio(client->socket, "Errore nell'accettazione.\n");
                }
            } else {
                invia_messaggio(client->socket, "Non sei il creatore di questa partita.\n");
            }
            
        } else if (strncmp(buffer, "RIFIUTA ", 8) == 0) {
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
            
        } else if (strcmp(buffer, "ABBANDONA") == 0) {
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
        } else if (strcmp(buffer, "ESCI") == 0) {
            int id_in_corso = trova_partita_in_corso_client(client->socket);
            if (id_in_corso > 0) {
                invia_messaggio(client->socket,
                                "Sei in partita. Usa ABBANDONA per arrenderti e tornare al menu.\n");
                continue;
            }

            invia_messaggio(client->socket, "Arrivederci!\n");
            break;
            
        } else if (client->id_partita_corrente > 0) {
            Partita* partita = trova_partita(client->id_partita_corrente);
            if (!partita || partita->stato != PARTITA_IN_CORSO) {
                client->id_partita_corrente = 0;
                continue;
            }
            
            if (partita && partita->stato == PARTITA_IN_CORSO) {
                int e_giocatore1 = (client->socket == partita->socket_giocatore1);
                if ((e_giocatore1 && partita->turno_corrente != 1) || 
                    (!e_giocatore1 && partita->turno_corrente != 2)) {
                    invia_messaggio(client->socket, "Non è il tuo turno!\n");
                    continue;
                }
                if (strlen(buffer) != 1 || buffer[0] < '0' || buffer[0] > '6') {
                    invia_messaggio(client->socket, "Input non valido! Inserisci un numero da 0 a 6: ");
                    continue;
                }
                int colonna = atoi(buffer);
                char simbolo_giocatore = e_giocatore1 ? 'R' : 'G';
                
                int riga = inserisci_gettone(partita->griglia, colonna, simbolo_giocatore);
                
                if (riga == -1) {
                    invia_messaggio(client->socket, "Mossa non valida! Riprova: ");
                    continue;
                }
                
                if (controlla_vittoria(partita->griglia, simbolo_giocatore)) {
                    invia_messaggio(partita->socket_giocatore1, "\n");
                    invia_messaggio(partita->socket_giocatore1, griglia_a_stringa(partita->griglia));
                    invia_messaggio(partita->socket_giocatore2, "\n");
                    invia_messaggio(partita->socket_giocatore2, griglia_a_stringa(partita->griglia));

                    partita->stato = PARTITA_TERMINATA;
                    partita->vincitore = e_giocatore1 ? 1 : 2;
                    
                    invia_messaggio(client->socket, "HAI VINTO! Congratulazioni!\n");
                    invia_messaggio(e_giocatore1 ? partita->socket_giocatore2 : partita->socket_giocatore1, 
                                  "Hai perso. Riprova!\n");
                    invia_messaggio(partita->socket_giocatore1, "Puoi creare o unirti a una nuova partita.\n");
                    invia_messaggio(partita->socket_giocatore2, "Puoi creare o unirti a una nuova partita.\n");

                    client->id_partita_corrente = 0;
                    continue;
                }
                
                if (controlla_pareggio(partita->griglia)) {
                    invia_messaggio(partita->socket_giocatore1, "\n");
                    invia_messaggio(partita->socket_giocatore1, griglia_a_stringa(partita->griglia));
                    invia_messaggio(partita->socket_giocatore2, "\n");
                    invia_messaggio(partita->socket_giocatore2, griglia_a_stringa(partita->griglia));

                    partita->stato = PARTITA_TERMINATA;
                    invia_messaggio(partita->socket_giocatore1, "PAREGGIO!\n");
                    invia_messaggio(partita->socket_giocatore2, "PAREGGIO!\n");

                    invia_messaggio(partita->socket_giocatore1, "Puoi creare o unirti a una nuova partita.\n");
                    invia_messaggio(partita->socket_giocatore2, "Puoi creare o unirti a una nuova partita.\n");

                    client->id_partita_corrente = 0;
                    continue;
                }
                
                partita->turno_corrente = (partita->turno_corrente == 1) ? 2 : 1;
                int socket_ko = -1;
                
                if (partita->turno_corrente == 1) {
                    if (invia_messaggio(partita->socket_giocatore1, "È il tuo turno! Scegli una colonna (0-6): ") < 0) {
                        socket_ko = partita->socket_giocatore1;
                    }
                    if (invia_messaggio(partita->socket_giocatore2, "Aspetta il tuo turno...\n") < 0 && socket_ko < 0) {
                        socket_ko = partita->socket_giocatore2;
                    }
                } else {
                    if (invia_messaggio(partita->socket_giocatore2, "È il tuo turno! Scegli una colonna (0-6): ") < 0) {
                        socket_ko = partita->socket_giocatore2;
                    }
                    if (invia_messaggio(partita->socket_giocatore1, "Aspetta il tuo turno...\n") < 0 && socket_ko < 0) {
                        socket_ko = partita->socket_giocatore1;
                    }
                }
                if (invia_messaggio(partita->socket_giocatore1, "\n") < 0 && socket_ko < 0) {
                    socket_ko = partita->socket_giocatore1;
                }
                if (invia_messaggio(partita->socket_giocatore1, griglia_a_stringa(partita->griglia)) < 0 && socket_ko < 0) {
                    socket_ko = partita->socket_giocatore1;
                }
                if (invia_messaggio(partita->socket_giocatore2, "\n") < 0 && socket_ko < 0) {
                    socket_ko = partita->socket_giocatore2;
                }
                if (invia_messaggio(partita->socket_giocatore2, griglia_a_stringa(partita->griglia)) < 0 && socket_ko < 0) {
                    socket_ko = partita->socket_giocatore2;
                }
                
                if (socket_ko > 0) {
                    termina_partita_per_socket_non_raggiungibile(partita, socket_ko);
                    client->id_partita_corrente = 0;
                    continue;
                }
            }
            
        } else {
            invia_messaggio(client->socket, "Comando non riconosciuto.\n");
        }
    }
    
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

    printf("Client %s disconnesso\n", client->nome);
    close(client->socket);
    free(client);
    return NULL;
}
