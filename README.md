# Tic-Tac-Toe (C, TCP/UDP)

A simple two-player Tic-Tac-Toe game over the network, implemented in C. You can choose TCP or UDP via `config.ini`. The top-level `server` and `client` binaries dispatch to the selected protocol.

## Layout

- `TCP/` TCP client/server implementation
- `UDP/` UDP client/server implementation
- `server.c` launcher (reads `config.ini` and runs TCP or UDP server)
- `client.c` launcher (reads `config.ini` and runs TCP or UDP client)
- `config.ini` protocol selector

## Build

```bash
gcc -O2 -Wall -Wextra -pthread -o TCP/server TCP/server.c
gcc -O2 -Wall -Wextra -o TCP/client TCP/client.c
gcc -O2 -Wall -Wextra -o UDP/server UDP/server.c
gcc -O2 -Wall -Wextra -o UDP/client UDP/client.c
gcc -O2 -Wall -Wextra -o server server.c
gcc -O2 -Wall -Wextra -o client client.c
```

## Choose Protocol

Edit `config.ini`:

```
protocol=TCP
```

Use `TCP` or `UDP`.

## Run

Start the server on a port:

```bash
./server 9000
```

Connect two clients (each in its own terminal):

```bash
./client 127.0.0.1 9000
```

## Protocol Summary

TCP uses the original 3-byte command + 32-bit little-endian integer stream. UDP uses a fixed 12-byte datagram: 3-byte command, 1-byte pad, then two 32-bit little-endian integers.

UDP mode is best-effort (no retransmits) and runs one game at a time.

Commands:

- `HLD` wait for second player
- `SRT` game start
- `TRN` your turn
- `INV` invalid move
- `CNT` active player count
- `UPD` board update
- `WAT` waiting for opponent
- `WIN` you win
- `LSE` you lose
- `DRW` draw

Moves are `0-8` for board positions, or `9` to request player count.
