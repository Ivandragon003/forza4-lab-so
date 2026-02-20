#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#ifdef __linux__
#include <netinet/tcp.h>
#endif

#include "network.h"
#include "session.h"

int main() {
    int server_fd;
    struct sockaddr_in indirizzo;
    int addrlen = sizeof(indirizzo);
    signal(SIGPIPE, SIG_IGN);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Errore creazione socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Errore setsockopt");
        exit(EXIT_FAILURE);
    }

    indirizzo.sin_family = AF_INET;
    indirizzo.sin_addr.s_addr = INADDR_ANY;
    indirizzo.sin_port = htons(PORTA);

    if (bind(server_fd, (struct sockaddr*)&indirizzo, sizeof(indirizzo)) < 0) {
        perror("Errore bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENT) < 0) {
        perror("Errore listen");
        exit(EXIT_FAILURE);
    }

    printf("Server Forza 4 avviato!\n");
    printf("In ascolto sulla porta %d...\n", PORTA);

    while (1) {
        DatiClient* dati_client = malloc(sizeof(DatiClient));
        dati_client->socket = accept(server_fd, (struct sockaddr*)&indirizzo, (socklen_t*)&addrlen);

        if (dati_client->socket < 0) {
            perror("Errore accept");
            free(dati_client);
            continue;
        }

        int keepalive = 1;
        if (setsockopt(dati_client->socket, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0) {
            perror("Errore setsockopt SO_KEEPALIVE");
        }

#ifdef TCP_KEEPIDLE
        {
            int keepidle = 10;
            if (setsockopt(dati_client->socket, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) < 0) {
                perror("Errore setsockopt TCP_KEEPIDLE");
            }
        }
#endif
#ifdef TCP_KEEPINTVL
        {
            int keepintvl = 5;
            if (setsockopt(dati_client->socket, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl)) < 0) {
                perror("Errore setsockopt TCP_KEEPINTVL");
            }
        }
#endif
#ifdef TCP_KEEPCNT
        {
            int keepcnt = 2;
            if (setsockopt(dati_client->socket, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt)) < 0) {
                perror("Errore setsockopt TCP_KEEPCNT");
            }
        }
#endif
#ifdef TCP_USER_TIMEOUT
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
            if (setsockopt(dati_client->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
                perror("Errore setsockopt SO_RCVTIMEO");
            }
        }

        dati_client->id_partita_corrente = 0;

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
