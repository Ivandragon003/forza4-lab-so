#ifndef NETWORK_H
#define NETWORK_H

#define PORTA 8080
#define MAX_CLIENT 100
#define DIM_BUFFER 1024

int invia_messaggio(int socket, const char* messaggio);
int ricevi_messaggio(int socket, char* buffer);

#endif
