#include "game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Partita partite[MAX_PARTITE];
int contatore_partite = 0;


void inizializza_griglia(char griglia[RIGHE][COLONNE]) {
    for (int i = 0; i < RIGHE; i++) {
        for (int j = 0; j < COLONNE; j++) {
            griglia[i][j] = '.';
        }
    }
}


int inserisci_gettone(char griglia[RIGHE][COLONNE], int colonna, char giocatore) {
    if (colonna < 0 || colonna >= COLONNE) {
        return -1;  
    }
    
   
    for (int riga = RIGHE - 1; riga >= 0; riga--) {
        if (griglia[riga][colonna] == '.') {
            griglia[riga][colonna] = giocatore;
            return riga;
        }
    }
    
    return -1;  
}


int controlla_vittoria(char griglia[RIGHE][COLONNE], char giocatore) {
    // Controlla orizzontale
    for (int riga = 0; riga < RIGHE; riga++) {
        for (int col = 0; col <= COLONNE - 4; col++) {
            if (griglia[riga][col] == giocatore &&
                griglia[riga][col+1] == giocatore &&
                griglia[riga][col+2] == giocatore &&
                griglia[riga][col+3] == giocatore) {
                return 1;
            }
        }
    }
    
    // Controlla verticale
    for (int riga = 0; riga <= RIGHE - 4; riga++) {
        for (int col = 0; col < COLONNE; col++) {
            if (griglia[riga][col] == giocatore &&
                griglia[riga+1][col] == giocatore &&
                griglia[riga+2][col] == giocatore &&
                griglia[riga+3][col] == giocatore) {
                return 1;
            }
        }
    }
    
    // Controlla diagonale 
    for (int riga = 0; riga <= RIGHE - 4; riga++) {
        for (int col = 0; col <= COLONNE - 4; col++) {
            if (griglia[riga][col] == giocatore &&
                griglia[riga+1][col+1] == giocatore &&
                griglia[riga+2][col+2] == giocatore &&
                griglia[riga+3][col+3] == giocatore) {
                return 1;
            }
        }
    }
    
    // Controlla diagonale 
    for (int riga = 3; riga < RIGHE; riga++) {
        for (int col = 0; col <= COLONNE - 4; col++) {
            if (griglia[riga][col] == giocatore &&
                griglia[riga-1][col+1] == giocatore &&
                griglia[riga-2][col+2] == giocatore &&
                griglia[riga-3][col+3] == giocatore) {
                return 1;
            }
        }
    }
    
    return 0;
}


int controlla_pareggio(char griglia[RIGHE][COLONNE]) {
    for (int col = 0; col < COLONNE; col++) {
        if (griglia[0][col] == '.') {
            return 0;  
        }
    }
    return 1;  
}


char* griglia_a_stringa(char griglia[RIGHE][COLONNE]) {
    static char buffer[500];
    buffer[0] = '\0';
    
    for (int i = 0; i < RIGHE; i++) {
        for (int j = 0; j < COLONNE; j++) {
            char cella[3];
            sprintf(cella, "%c ", griglia[i][j]);
            strcat(buffer, cella);
        }
        strcat(buffer, "\n");
    }
    strcat(buffer, "0 1 2 3 4 5 6\n");
    
    return buffer;
}


int crea_partita(int socket_giocatore, char* nome_giocatore) {
    if (contatore_partite >= MAX_PARTITE) {
        return -1;  
    }
    
    Partita* partita = &partite[contatore_partite];
    partita->id_partita = contatore_partite + 1;
    inizializza_griglia(partita->griglia);
    partita->socket_giocatore1 = socket_giocatore;
    partita->socket_giocatore2 = -1;
    snprintf(partita->nome_giocatore1, sizeof(partita->nome_giocatore1), "%s", nome_giocatore);
    partita->nome_giocatore2[0] = '\0';
    partita->turno_corrente = 1;
    partita->stato = PARTITA_IN_ATTESA;
    partita->vincitore = 0;
    
    contatore_partite++;
    return partita->id_partita;
}


Partita* trova_partita(int id_partita) {
    for (int i = 0; i < contatore_partite; i++) {
        if (partite[i].id_partita == id_partita) {
            return &partite[i];
        }
    }
    return NULL;
}


// Invece di unirsi direttamente, mette la richiesta in attesa
int richiedi_partita(int id_partita, int socket_giocatore, char* nome_giocatore) {
    Partita* partita = trova_partita(id_partita);
    
    if (partita == NULL) {
        return -1;  // Partita non trovata
    }
    
    if (partita->stato != PARTITA_IN_ATTESA) {
        return -2;  // Partita non disponibile
    }
    
    if (partita->socket_giocatore2 != -1) {
        return -3;  // Partita già piena
    }
    
    // Salva la richiesta
    partita->socket_richiedente = socket_giocatore;
    snprintf(partita->nome_richiedente, sizeof(partita->nome_richiedente), "%s", nome_giocatore);
    partita->stato = PARTITA_RICHIESTA_PENDENTE;
    
    return 0;  
}

int accetta_richiesta(int id_partita) {
    Partita* partita = trova_partita(id_partita);
    
    if (partita == NULL || partita->stato != PARTITA_RICHIESTA_PENDENTE) {
        return -1;
    }
    
    // sposta il richiedente in giocatore2
    partita->socket_giocatore2 = partita->socket_richiedente;
    snprintf(partita->nome_giocatore2, sizeof(partita->nome_giocatore2), "%s", partita->nome_richiedente);
    partita->stato = PARTITA_IN_CORSO;
    
    return 0;
}

int rifiuta_richiesta(int id_partita) {
    Partita* partita = trova_partita(id_partita);
    
    if (partita == NULL || partita->stato != PARTITA_RICHIESTA_PENDENTE) {
        return -1;
    }
    
  
    partita->socket_richiedente = -1;
    partita->nome_richiedente[0] = '\0';
    partita->stato = PARTITA_IN_ATTESA;
    
    return 0;
} 

char* lista_partite() {
    static char buffer[2048];
    size_t usati = 0;
    size_t capienza = sizeof(buffer);
    buffer[0] = '\0';
    
    if (contatore_partite == 0) {
        snprintf(buffer, capienza, "Nessuna partita disponibile.\n");
        return buffer;
    }
    
    int scritti = snprintf(buffer + usati, capienza - usati, "Partite disponibili:\n");
    if (scritti < 0) {
        buffer[0] = '\0';
        return buffer;
    }
    if ((size_t)scritti >= capienza - usati) {
        buffer[capienza - 1] = '\0';
        return buffer;
    }
    usati += (size_t)scritti;

    for (int i = 0; i < contatore_partite; i++) {
        const char* stato_str;
        
        switch(partite[i].stato) {
            case PARTITA_IN_ATTESA:
                stato_str = "In attesa";
                break;
            case PARTITA_IN_CORSO:
                stato_str = "In corso";
                break;
            case PARTITA_TERMINATA:
                stato_str = "Terminata";
                break;
            default:
                stato_str = "Sconosciuto";
        }
        
        if (usati >= capienza - 1) {
            break;
        }

        scritti = snprintf(
            buffer + usati,
            capienza - usati,
            "ID: %d | Creata da: %s | Stato: %s\n",
            partite[i].id_partita,
            partite[i].nome_giocatore1,
            stato_str
        );

        if (scritti < 0) {
            buffer[0] = '\0';
            return buffer;
        }
        if ((size_t)scritti >= capienza - usati) {
            usati = capienza - 1;
            buffer[usati] = '\0';
            break;
        }
        usati += (size_t)scritti;
    }
    
    return buffer;
}
