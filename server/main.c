#include <stdio.h>  //funzioni di input e output come printf e perror
#include <stdlib.h> //funzioni generiche come malloc, free ed exit
#include <unistd.h> //funzioni POSIX come close 
//Le funzioni posix sono funzioni definite dallo standard POSIX, che è uno standard comune per sistemi tipo Unix/Linux.
//Servono a scrivere programmi portabili tra vari sistemi compatibili, e includono cose come processi, file, segnali, socket e thread.

#include <pthread.h> //thread POSIX come pthread_create, pthread_detach
#include <arpa/inet.h> //funzioni e tipi per indirizzi di rete come htons
#include <signal.h> //gestione dei segnali, come SIGPIPE e SIG_ING
#include <sys/time.h> //strutture e funzioni legate al tempo, come struct timeval, usata per SO_RCVTIMEO
#ifdef __linux__
#include <netinet/tcp.h> //opzioni TCP Linux : TCP_KEEPIDLE, TCP_KEEPINTVL, ecc.
#endif

#include "network.h"
#include "session.h"

static int imposta_opzione_socket(int socket_fd,
                                  int level,
                                  int optname,
                                  const void* value,
                                  socklen_t value_len,
                                  const char* errore) {
    if (setsockopt(socket_fd, level, optname, value, value_len) < 0) {
        perror(errore);
        return -1;
    }
    return 0;
}

static void configura_socket_client(int socket_client) {
    int keepalive = 1;
    (void)imposta_opzione_socket(socket_client, SOL_SOCKET, SO_KEEPALIVE,
                                 &keepalive, sizeof(keepalive),
                                 "Errore setsockopt SO_KEEPALIVE");

#ifdef TCP_KEEPIDLE //dopo quanti secondi iniziare i controlli
    {
        int keepidle = 10;  //10 secondi di inattività -> inizia a fare ping
        (void)imposta_opzione_socket(socket_client, IPPROTO_TCP, TCP_KEEPIDLE,
                                     &keepidle, sizeof(keepidle),
                                     "Errore setsockopt TCP_KEEPIDLE");
    }
#endif
#ifdef TCP_KEEPINTVL    //ogni quanto riprovare
    {
        int keepintvl = 5;  //ogni 5 secondi manda un altro ping 
        (void)imposta_opzione_socket(socket_client, IPPROTO_TCP, TCP_KEEPINTVL,
                                     &keepintvl, sizeof(keepintvl),
                                     "Errore setsockopt TCP_KEEPINTVL");
    }
#endif
#ifdef TCP_KEEPCNT  //quante volte prima di arrendersi
    {
        int keepcnt = 2;    //dopo 2 ping senza risposta -> client morto
        (void)imposta_opzione_socket(socket_client, IPPROTO_TCP, TCP_KEEPCNT,
                                     &keepcnt, sizeof(keepcnt),
                                     "Errore setsockopt TCP_KEEPCNT");
    }
#endif
#ifdef TCP_USER_TIMEOUT //tempo massimo per ricevere un ACK
    {
        int timeout_ms = 15000; //15 secondi in millesecondi
        (void)imposta_opzione_socket(socket_client, IPPROTO_TCP, TCP_USER_TIMEOUT,
                                     &timeout_ms, sizeof(timeout_ms),
                                     "Errore setsockopt TCP_USER_TIMEOUT");
    }
#endif
    {
        struct timeval tv;   //sys/time.h
        tv.tv_sec = 15;
        tv.tv_usec = 0;
        (void)imposta_opzione_socket(socket_client, SOL_SOCKET, SO_RCVTIMEO,
                                     &tv, sizeof(tv),
                                     "Errore setsockopt SO_RCVTIMEO");
    }
}

int main() {
    int server_fd;  //fd sarebbe file descriptor. in unix tutto è un file;
    struct sockaddr_in indirizzo;
//struttura usata per rappresentare un indirizzo IPv4 con porta.
//è una struttura "specializzata": sockaddr è generica, sockaddr_in è per IPv4. in genere contiene almeno questi campi:
/*
struct sockaddr_in {
    sa_family_t    sin_family;   // indica il tipo di indirizzo, per IPv4 vale AF_INET. AF_INET = 2 (per IPv4)
    in_port_t      sin_port;     // numero della porta, ma in ordine di byte di rete (esempio nostro 8080)
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
1. Ritorna -1
2. Imposta errno con il codice dell'errore specifico
3. perror("Errore socket")
   ↓
Stampa: "Errore socket: <descrizione errore>"*/

    int opt = 1;
    if (imposta_opzione_socket(server_fd, SOL_SOCKET, SO_REUSEADDR,
                               &opt, sizeof(opt),
                               "Errore setsockopt SO_REUSEADDR") < 0) {
// senza SO_REUSEADDR, se il server crasha e si riavvia immediatamente, il so tiene la porta occupata per qualche minuto.
// con SO_REUSEADDR, il server può subito riusare la porta senza aspettare. scelta quasi OBBLIGATORIA
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

        configura_socket_client(dati_client->socket);
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
