#ifndef GAME_H
#define GAME_H

#include <stdatomic.h>
#include <stddef.h>

#define RIGHE 6
#define COLONNE 7
#define MAX_PARTITE 100


typedef enum {
    PARTITA_IN_ATTESA,
    PARTITA_RICHIESTA_PENDENTE, 
    PARTITA_IN_CORSO,
    PARTITA_TERMINATA
} StatoPartita;


typedef struct {
    int id_partita;
    char griglia[RIGHE][COLONNE];
    int socket_giocatore1;
    int socket_giocatore2;
    char nome_giocatore1[50];
    char nome_giocatore2[50];
    int turno_corrente;
    StatoPartita stato;
    int vincitore;
    int socket_richiedente;       
    char nome_richiedente[50];
    atomic_int timeout_annullato;
} Partita;


extern Partita partite[MAX_PARTITE];
extern int contatore_partite;

void inizializza_griglia(char griglia[RIGHE][COLONNE]);
int inserisci_gettone(char griglia[RIGHE][COLONNE], int colonna, char giocatore);
int controlla_vittoria(char griglia[RIGHE][COLONNE], char giocatore);
int controlla_pareggio(char griglia[RIGHE][COLONNE]);
void griglia_a_stringa(char griglia[RIGHE][COLONNE], char* buffer, size_t size);


int crea_partita(int socket_giocatore, char* nome_giocatore);
Partita* trova_partita(int id_partita);
int richiedi_partita(int id_partita, int socket_giocatore, char* nome_giocatore);
int accetta_richiesta(int id_partita);
int rifiuta_richiesta(int id_partita);
char* lista_partite();

#endif
