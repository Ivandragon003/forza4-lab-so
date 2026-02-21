# Forza 4 - Lab SO

Server multiplayer TCP in C + client console Java per giocare a Forza 4.

## Panoramica
Il progetto implementa:
- server concorrente multi-client (`pthread`) su porta `8080`
- gestione lobby e partite con richieste di ingresso (`ENTRA` -> `ACCETTA`/`RIFIUTA`)
- gestione disconnessioni durante lobby e partita
- client CLI Java con output colorato

## Architettura
Struttura server:

```text
server/
+-- main.c              # Entry point: socket setup, accept, thread client
+-- network.h/.c        # I/O di rete (send/recv)
+-- game.h/.c           # Logica di gioco e stato partite
+-- session.h/.c        # Sessione client (handshake + loop)
+-- commands.h/.c       # Handler comandi e flusso partita
+-- game_queries.h/.c   # Query helper su partite/stati
+-- Makefile
```

Altri componenti:

```text
client/
+-- Client.java         # Client console
+-- Dockerfile

docker-compose.yml      # Orchestrazione server + client
```

## Requisiti
- Docker Desktop (o Docker Engine + Compose)
- porte locali: `8080` libera

## Avvio
Dalla root del progetto:

1. Avvia/builda il server:
```bash
docker-compose up --build server
```

2. In un altro terminale, avvia un client:
```bash
docker-compose run --rm client
```

Per avviare più client, esegui lo stesso comando in terminali diversi.

## Comandi disponibili (client)
- `CREA` -> crea una nuova partita
- `LISTA` -> mostra partite disponibili
- `ENTRA <id>` -> richiede ingresso in una partita
- `ACCETTA <id>` -> accetta richiesta (solo creatore)
- `RIFIUTA <id>` -> rifiuta richiesta (solo creatore)
- `ABBANDONA` -> resa nella partita corrente
- `ESCI` -> uscita dal gioco (quando non in partita)

## Note su disconnessione
Se il terminale viene chiuso, il container si arresta (grazie a `init: true`),
il processo Java riceve SIGTERM, esegue lo shutdown hook e notifica il server.
Come fallback, il server rileva l'assenza di PING entro 15 secondi e
gestisce la disconnessione automaticamente.

## Rete e container
- Server: container `forza4_server` (`0.0.0.0:8080->8080`)
- Client: container effimero avviato con `docker-compose run --rm client`
- Network: bridge `forza4_network`

## Comandi utili
Build immagini:
```bash
docker-compose build
```

Verifica stato container:
```bash
docker-compose ps
```

Log:
```bash
docker-compose logs -f server
```

Stop:
```bash
docker-compose down
```

## Troubleshooting rapido
- Errore pipe Docker su Windows (`dockerDesktopLinuxEngine`): avvia Docker Desktop.
- Client non interattivo o avvio errato: usa `docker-compose run --rm client`.
- Porta `8080` occupata: libera la porta o cambia mapping in `docker-compose.yml`.
