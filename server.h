#ifndef SERVER_H
#define SERVER_H

// Démarre le serveur sur le port spécifié
void start_server(int port);

// Rend un descripteur de fichier non-bloquant (nécessaire pour epoll)
int set_nonblocking(int fd);

#endif