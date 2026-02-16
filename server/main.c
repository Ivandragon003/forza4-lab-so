#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <signal.h>
#include "network.h"
#include "game.h"

int main() {
    int server_fd;
    struct sockaddr_in indirizzo;
    int addrlen = sizeof(indirizzo);
    signal(SIGPIPE, SIG_IGN);

    // Creazione socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Errore creazione socket");
        exit(EXIT_FAILURE);
    }
    
    // Opzioni socket
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Errore setsockopt");
        exit(EXIT_FAILURE);
    }
    
    indirizzo.sin_family = AF_INET;
    indirizzo.sin_addr.s_addr = INADDR_ANY;
    indirizzo.sin_port = htons(PORTA);
    
    // Bind
    if (bind(server_fd, (struct sockaddr *)&indirizzo, sizeof(indirizzo)) < 0) {
        perror("Errore bind");
        exit(EXIT_FAILURE);
    }
    
  
    if (listen(server_fd, MAX_CLIENT) < 0) {
        perror("Errore listen");
        exit(EXIT_FAILURE);
    }
    

    printf("Server Forza 4 avviato!\n");
    printf("In ascolto sulla porta %d...\n", PORTA);
    
    
    // Loop accettazione client
    while (1) {
        DatiClient* dati_client = malloc(sizeof(DatiClient));
        dati_client->socket = accept(server_fd, (struct sockaddr *)&indirizzo, (socklen_t*)&addrlen);
        
        if (dati_client->socket < 0) {
            perror("Errore accept");
            free(dati_client);
            continue;
        }
        
        dati_client->id_partita_corrente = 0;
        
        // Crea thread per gestire il client
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
