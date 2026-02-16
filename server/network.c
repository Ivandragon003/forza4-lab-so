#include "network.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

void invia_messaggio(int socket, const char* messaggio) {
    if (socket < 0 || messaggio == NULL) {
        return;
    }

    ssize_t inviati = send(socket, messaggio, strlen(messaggio), 0);
    if (inviati < 0) {
        perror("Errore invio messaggio");
    }
}

int ricevi_messaggio(int socket, char* buffer) {
    memset(buffer, 0, DIM_BUFFER);
    int bytes = recv(socket, buffer, DIM_BUFFER - 1, 0);
    if (bytes > 0) {
        buffer[strcspn(buffer, "\n")] = 0;
    }
    return bytes;
}

void* gestisci_client(void* arg) {
    DatiClient* client = (DatiClient*)arg;
    char buffer[DIM_BUFFER];
    
    printf("Client connesso (socket: %d)\n", client->socket);
    
    invia_messaggio(client->socket, "Benvenuto a Forza 4!\nInserisci il tuo nome: ");
    
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
    strcat(benvenuto, "ESCI - Esci dal gioco\n");
    invia_messaggio(client->socket, benvenuto);
    
    while (1) {
        invia_messaggio(client->socket, "\n> ");
        
        if (ricevi_messaggio(client->socket, buffer) <= 0) {
            break;
        }
        
        printf("Ricevuto da %s: %s\n", client->nome, buffer);
        
        if (strcmp(buffer, "CREA") == 0) {
            int id_partita = crea_partita(client->socket, client->nome);
            if (id_partita > 0) {
                client->id_partita_corrente = id_partita;
                char msg[100];
                sprintf(msg, "Partita creata! ID: %d\nIn attesa di un avversario...\n", id_partita);
                invia_messaggio(client->socket, msg);
            } else {
                invia_messaggio(client->socket, "Errore nella creazione della partita.\n");
            }
            
        } else if (strcmp(buffer, "LISTA") == 0) {
            invia_messaggio(client->socket, lista_partite());
            
        } else if (strncmp(buffer, "ENTRA ", 6) == 0) {
            int id_partita = atoi(buffer + 6);
            int risultato = richiedi_partita(id_partita, client->socket, client->nome);
            
            if (risultato == 0) {
                Partita* partita = trova_partita(id_partita);
                
                client->id_partita_corrente = id_partita;
                
                invia_messaggio(client->socket, "Richiesta inviata. In attesa di accettazione...\n");
                
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
                
            } else if (risultato == -1) {
                invia_messaggio(client->socket, "Partita non trovata.\n");
            } else if (risultato == -2) {
                invia_messaggio(client->socket, "Partita non disponibile.\n");
            } else {
                invia_messaggio(client->socket, "Partita già piena.\n");
            }
            
        } else if (strncmp(buffer, "ACCETTA ", 8) == 0) {
            int id_partita = atoi(buffer + 8);
            Partita* partita = trova_partita(id_partita);
            
            if (partita && partita->socket_giocatore1 == client->socket) {
                if (accetta_richiesta(id_partita) == 0) {
                    char msg1[200], msg2[200];
                    sprintf(msg1, "Hai accettato %s. Partita iniziata!\n", partita->nome_giocatore2);
                    sprintf(msg2, "La tua richiesta è stata accettata! Partita iniziata!\n");
                    
                    invia_messaggio(partita->socket_giocatore1, msg1);
                    invia_messaggio(partita->socket_giocatore2, msg2);
                    
                    sprintf(msg1, "Giochi contro %s. Sei il giocatore ROSSO (R).\n", partita->nome_giocatore2);
                    sprintf(msg2, "Giochi contro %s. Sei il giocatore GIALLO (G).\n", partita->nome_giocatore1);
                    
                    invia_messaggio(partita->socket_giocatore1, msg1);
                    invia_messaggio(partita->socket_giocatore2, msg2);
                    
                    invia_messaggio(partita->socket_giocatore1, griglia_a_stringa(partita->griglia));
                    invia_messaggio(partita->socket_giocatore2, griglia_a_stringa(partita->griglia));
                    
                    invia_messaggio(partita->socket_giocatore1, "È il tuo turno! Scegli una colonna (0-6): ");
                    
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
            
        } else if (strcmp(buffer, "ESCI") == 0) {
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
                
                invia_messaggio(partita->socket_giocatore1, "\n");
                invia_messaggio(partita->socket_giocatore1, griglia_a_stringa(partita->griglia));
                invia_messaggio(partita->socket_giocatore2, "\n");
                invia_messaggio(partita->socket_giocatore2, griglia_a_stringa(partita->griglia));
                
                if (controlla_vittoria(partita->griglia, simbolo_giocatore)) {
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
                    partita->stato = PARTITA_TERMINATA;
                    invia_messaggio(partita->socket_giocatore1, "PAREGGIO!\n");
                    invia_messaggio(partita->socket_giocatore2, "PAREGGIO!\n");

                    invia_messaggio(partita->socket_giocatore1, "Puoi creare o unirti a una nuova partita.\n");
                    invia_messaggio(partita->socket_giocatore2, "Puoi creare o unirti a una nuova partita.\n");

                    client->id_partita_corrente = 0;
                    continue;
                }
                
                partita->turno_corrente = (partita->turno_corrente == 1) ? 2 : 1;
                
                if (partita->turno_corrente == 1) {
                    invia_messaggio(partita->socket_giocatore1, "È il tuo turno! Scegli una colonna (0-6): ");
                    invia_messaggio(partita->socket_giocatore2, "Aspetta il tuo turno...\n");
                } else {
                    invia_messaggio(partita->socket_giocatore2, "È il tuo turno! Scegli una colonna (0-6): ");
                    invia_messaggio(partita->socket_giocatore1, "Aspetta il tuo turno...\n");
                }
            }
            
        } else {
            invia_messaggio(client->socket, "Comando non riconosciuto.\n");
        }
    }
    
    if (client->id_partita_corrente > 0) {
        Partita* partita = trova_partita(client->id_partita_corrente);
        if (partita) {
            if (partita->socket_giocatore1 == client->socket || partita->socket_giocatore2 == client->socket) {
                partita->stato = PARTITA_TERMINATA;
                int avversario = (partita->socket_giocatore1 == client->socket)
                    ? partita->socket_giocatore2
                    : partita->socket_giocatore1;
                if (avversario > 0) {
                    invia_messaggio(avversario, "L'altro giocatore si è disconnesso. Partita terminata.\n");
                }
            } else if (partita->socket_richiedente == client->socket) {
                partita->socket_richiedente = -1;
                partita->nome_richiedente[0] = '\0';
                partita->stato = PARTITA_IN_ATTESA;
            }
        }
    }

    printf("Client %s disconnesso\n", client->nome);
    close(client->socket);
    free(client);
    return NULL;
}
