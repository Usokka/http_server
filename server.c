#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "http.h"
#include "server.h"

#define MAX_EVENTS 10

/*
    Fonction qui rend une socket non bloquante
    fcntl : fonction pour manipuler les descripteurs de fichiers
    F_GETFL : commande pour obtenir les flags actuels de la socket
    F_SETFL : commande pour définir de nouveaux flags
    O_NONBLOCK : flag pour rendre la socket non bloquante
*/
int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    return -1;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void start_server(int port) {
  int listen_sock, epoll_fd;
  struct sockaddr_in addr;
  struct epoll_event ev, events[MAX_EVENTS];

  listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_sock < 0) {
    perror("Erreur socket");
    exit(EXIT_FAILURE);
  }

  /* Option pour éviter l'erreur "Address already in use" au redémarrage */
  int opt = 1;
  setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  /* Configuration de l'adresse du serveur */
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  /* Bind : associer la socket à l'adresse et au port spécifiés */
  if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Erreur bind");
    exit(EXIT_FAILURE);
  }

  /* Listen : mettre la socket en mode écoute */
  if (listen(listen_sock, SOMAXCONN) < 0) {
    perror("Erreur listen");
    exit(EXIT_FAILURE);
  }

  /* Création de l'instance epoll */
  epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    perror("Erreur epoll_create1");
    exit(EXIT_FAILURE);
  }

  // Pour la socket d'écoute, on utilise explicitement ev.data.fd
  ev.events = EPOLLIN;
  ev.data.fd = listen_sock;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &ev) < 0) {
    perror("Erreur epoll_ctl: listen_sock");
    exit(EXIT_FAILURE);
  }

  printf("Serveur en ecoute sur le port %d (Mode epoll)\n", port);

  while (1) {
    int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    if (nfds == -1) {
      perror("Erreur epoll_wait");
      break;
    }

    for (int n = 0; n < nfds; ++n) {
      
      // On teste d'abord de manière sûre si l'événement provient de la socket d'écoute.
      // C'est valide car on a configuré l'événement de listen_sock avec data.fd.
      if (events[n].data.fd == listen_sock) {
        // Nouvelle connexion entrante
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
          perror("Erreur accept");
          continue;
        }

        if (set_nonblocking(client_sock) < 0) {
          perror("Erreur set_nonblocking");
          close(client_sock);
          continue;
        }

        // Allocation dynamique du contexte pour ce client
        ClientContext *client = malloc(sizeof(ClientContext));
        if (client == NULL) {
          perror("Erreur malloc ClientContext");
          close(client_sock);
          continue;
        }
        
        client->fd = client_sock;
        client->total_read = 0;
        memset(client->read_buf, 0, BUFFER_SIZE);

        // Enregistrement du client sous forme de pointeur d'événement (data.ptr)
        ev.events = EPOLLIN | EPOLLET;
        ev.data.ptr = client; 
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sock, &ev) < 0) {
          perror("Erreur epoll_ctl: ADD client");
          free(client);
          close(client_sock);
        }

      } else {
        // Un client existant a envoyé des données (Edge-Triggered)
        // On récupère de manière sûre l'adresse du contexte via data.ptr
        ClientContext *client = (ClientContext *)events[n].data.ptr;
        
        if (handle_http_request(client) == 1) {
          // Si la requête est complètement traitée (retour 1), on nettoie tout proprement
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
          close(client->fd);
          free(client); 
        }
        // Si handle_http_request renvoie 0, c'est que le tampon OS est vide (EAGAIN),
        // on conserve le contexte intact et on attend le prochain cycle epoll.
      }
    }
  }

  close(listen_sock);
  close(epoll_fd);
}