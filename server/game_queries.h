#ifndef GAME_QUERIES_H
#define GAME_QUERIES_H

#include "game.h"

int trova_partita_in_corso_client(int socket_client);
Partita* trova_partita_in_corso_per_socket(int socket_client);
int trova_richiesta_pendente_client(int socket_client);

#endif
