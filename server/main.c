#include <stdio.h>  //funzioni di input e output come printf e perror
#include <stdlib.h> //funzioni generiche come malloc, free ed exit
#include <unistd.h> //funzioni POSIX come close 
//Le funzioni posix sono funzioni definite dallo standard POSIX, che è uno standard comune per sistemi tipo Unix/Linux.
//Servono a scrivere programmi portabili tra vari sistemi compatibili, e includono cose come processi, file, segnali, socket e thread.

#include <pthread.h> //thread POSIX come pthread_create, pthread_detach
#include <arpa/inet.h> //funzioni e tipi per indirizzi di rete come htons
#include <signal.h> //gestione dei segnali, come SIGPIPE e SIG_ING
#include <sys/time.h> //strutture e funzioni legate al tempo, come TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT, TCP_USER_TIMEOUT
#ifdef __linux__
#include <netinet/tcp.h> //opzioni TCP Linux
#endif

#include "network.h"
#include "session.h"

int main() {
    int server_fd;  //fd sarebbe file descriptor. in unix tutto è un file;
    struct sockaddr_in indirizzo;
//struttura usata per rappresentare un indirizzo IPv4 con porta.
//è una struttura "specializzata": sockaddr è generica, sockaddr_in è per IPv4. in genere contiene almeno questi campi:
/*
struct sockaddr_in {
    sa_family_t    sin_family;   // indica il tipo di indirizzo, per IPv4 vale AF_INET. AF_INET = 2 (per IPv4)
    in_port_t      sin_port;     // numero della porta, ma in ordine di byte di rete (esempio nostro 80800)
    struct in_addr sin_addr;     // contiene l'indirizzo IP (es. 192.168.1.1)
    unsigned char   sin_zero[8]; // spazio di padding, non si usa direttamente
};
*/
    socklen_t addrlen = sizeof(indirizzo);
    signal(SIGPIPE, SIG_IGN);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) { 
//          AF_INET → protocollo IPv4       SOCK_STREAM=TCP, mentre SOCK_DGRAM=udp;
/*quando il codice fa socket(...), non ottiene “un oggetto astratto”, ma un numero come 3, 4, 5 che identifica quella socket dentro il processo.*/
        perror("Errore creazione socket");
//perror(const char *s)
//funzione che stampa la stringa s passata, uno spazio e la traduzione testualer del valore corrente di errno
//errno è una macro che espande a un int thread-local modificabile. non è una variabile normale, ma si comporta come un intero.
//indica il codice numerico dell'ultimo errore di una system call o funzione di libreria che segue la convenzione errno
    exit(EXIT_FAILURE); //termina il server
    }
/*socket(AF_INET, SOCK_STREAM, 0)
  ↓ fallisce
1. Ritorna -1 (o NULL)
2. Imposta errno = 98 (EADDRINUSE)
3. perror("Errore socket")
   ↓
Stampa: "Errore socket: Address already in use"*/

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
// senza SO_REUSEADDR, se il server crasha e si riavvia immediatamente, il so tiene la porta occupata per qualche minuto.
// con SO_REUSEADDR, il server può subito riusare la porta senza aspettare. scelta quasi OBBLIGATORIA
        perror("Errore setsockopt");
        exit(EXIT_FAILURE);
    }
//struttura del nostro indirizzo di tipo sockaddr_in:
    indirizzo.sin_family = AF_INET; //userò ipv4;
    indirizzo.sin_addr.s_addr = INADDR_ANY; //accetta connesione da qualsiasi interfaccia di rete;
    indirizzo.sin_port = htons(PORTA); // converte la porta in network byte order (big-endian)
/*
htons = Host TO Network Short (16 bit). Converte un numero a 16 bit dall'ordine del tuo computer (host order) all'ordine della rete
(network order = sempre big-endian)

*/



    if (bind(server_fd, (struct sockaddr*)&indirizzo, sizeof(indirizzo)) < 0) {
// bind associa la socket all'indirizzo/porta;
        perror("Errore bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENT) < 0) {
//listen(server_fd, MAX_CLIENT) mette la socket in ascolto
//MAX_CLIENT è la dimensione della coda di connesioni in attesa;
        perror("Errore listen");
        exit(EXIT_FAILURE);
    }

    printf("Server Forza 4 avviato!\n");
    printf("In ascolto sulla porta %d...\n", PORTA);

    while (1) {
        DatiClient* dati_client = malloc(sizeof(DatiClient));
        if (dati_client == NULL) {
            perror("Errore malloc DatiClient");
            continue;
        }
        addrlen = sizeof(indirizzo);
        dati_client->socket = accept(server_fd, (struct sockaddr*)&indirizzo, &addrlen);
//accept blocca il processo finchè non arriva un client, poi restituice una nuova socket dedicata a quel client;

        if (dati_client->socket < 0) {
            perror("Errore accept");
            free(dati_client);
            continue;
        }

        int keepalive = 1;
        if (setsockopt(dati_client->socket, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0) {
            perror("Errore setsockopt SO_KEEPALIVE");
        }

#ifdef TCP_KEEPIDLE // dopo quanti secondi di inattività iniziare i controlli
        {
            int keepidle = 10;
            if (setsockopt(dati_client->socket, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) < 0) {
                perror("Errore setsockopt TCP_KEEPIDLE");
            }
        }
#endif
#ifdef TCP_KEEPINTVL // ogni quanto riprovare a controllare dopo il primo tentativo
        {
            int keepintvl = 5;
            if (setsockopt(dati_client->socket, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl)) < 0) {
                perror("Errore setsockopt TCP_KEEPINTVL");
            }
        }
#endif
#ifdef TCP_KEEPCNT // quante volte riprovo prima di dire “client morto”
        {
            int keepcnt = 2;
            if (setsockopt(dati_client->socket, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt)) < 0) {
                perror("Errore setsockopt TCP_KEEPCNT");
            }
        }
#endif
#ifdef TCP_USER_TIMEOUT // massimo tempo per ricevere ACK o risposta TCP prima di considerare la connessione morta
        {
            int timeout_ms = 15000;
            if (setsockopt(dati_client->socket, IPPROTO_TCP, TCP_USER_TIMEOUT,
                           &timeout_ms, sizeof(timeout_ms)) < 0) {
                perror("Errore setsockopt TCP_USER_TIMEOUT");
            }
        }
#endif
        {
            struct timeval tv;
            tv.tv_sec = 15;
            tv.tv_usec = 0;
            // evita blocchi infiniti su recv() se il client smette di rispondere
            if (setsockopt(dati_client->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) { 
                perror("Errore setsockopt SO_RCVTIMEO");
            }
        }

        atomic_init(&dati_client->id_partita_corrente, 0);

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, gestisci_client, (void*)dati_client) != 0) {
            perror("Errore creazione thread");
            close(dati_client->socket);
            free(dati_client);
            continue;
        }

        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}
