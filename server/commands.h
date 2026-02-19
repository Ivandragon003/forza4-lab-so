#ifndef COMMANDS_H
#define COMMANDS_H

#include "session.h"

int gestisci_input_client(DatiClient* client, const char* buffer);
void gestisci_disconnessione_client(DatiClient* client);

#endif
