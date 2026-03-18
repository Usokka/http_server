#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <errno.h>

#include "server.h"
#include "http.h"

#define MAX_EVENTS 10

    /* 
        Fonction qui rend une socket non bloquante
        fcntl : fonction pour manipuler les descripteurs de fichiers
        F_GETFL : commande pour obtenir les flags actuels de la socket
        F_SETFL : commande pour définir de nouveaux flags
        O_NONBLOCK : flag pour rendre la socket non bloquante
        -> donc avecl e "|" on ajoute le flag O_NONBLOCK aux flags existants sans les écraser
    */

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
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

    /* 
        Option pour éviter l'erreur "Address already in use" au redémarrage
        SOL_SOCKET : niveau de la socket, (ici niveau global)
        SO_REUSEADDR : option pour réutiliser l'adresse même si elle est en TIME_WAIT
        &opt : pointeur vers la valeur de l'option (ici 1 pour activer donc la mettre à vrai)
        sizeof(opt) : taille de la valeur de l'option pour que l'OS sache combien lire
    */
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));


    /*
        Configuration de l'adresse du serveur
        AF_INET : famille d'adresses IPv4
        INADDR_ANY : accepter les connexions sur toutes les interfaces réseau
        htons(port) : convertir le numéro de port en format réseau (big-endian)
    */
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    /*
        Bind : associer la socket à l'adresse et au port spécifiés, en fait c'est un syscall qui demande à l'OS de réserver le port pour notre application, il checke que le port n'est pas déjà utilisé, et qu'il est valide port (!= [0-1023] )et que l'adresse est valide
        (struct sockaddr *)&addr : cast de l'adresse en type générique pour la fonction bind
        sizeof(addr) : taille de la structure d'adresse pour que l'OS sache combien lire
    */
    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Erreur bind");
        exit(EXIT_FAILURE);
    }

    /*
        Listen : mettre la socket en mode écoute pour accepter les connexions entrantes
        SOMAXCONN : nombre maximum de connexions en file d'attente (défini par l'OS)
    */
    if (listen(listen_sock, SOMAXCONN) < 0) {
        perror("Erreur listen");
        exit(EXIT_FAILURE);
    }

    /*
        Création de l'instance epoll pour gérer les événements de manière efficace, en fait ça tourne en asynchrone, on peut gérer plusieurs connexions en même temps sans bloquer sur une seule, c'est plus performant que de faire du multi-threading ou du multi-processus pour chaque connexion car on évite le coût de création de threads/processus et la synchronisation entre eux
         
        epoll_create1(0) : crée une instance d'epoll, le paramètre 0 signifie qu'on n'utilise pas de flags spécifiques
        ev.events = EPOLLIN : on veut être notifié lorsque des données sont disponibles à lire sur une socket
        ev.data.fd = listen_sock : on associe la socket d'écoute à cet événement pour pouvoir identifier plus tard quelle socket a déclenché l'événement
        epoll_ctl : ajoute la socket d'écoute à l'instance epoll pour surveiller les événements de lecture (EPOLLIN)
    */
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("Erreur epoll_create1");
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN;
    ev.data.fd = listen_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &ev) < 0) {
        perror("Erreur epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    printf("Serveur en ecoute sur le port %d (Mode epoll)\n", port);

    while (1) {

        /*
            nfds : nombre de descripteurs de fichiers prêts à être traités, c'est le retour de epoll_wait qui bloque jusqu'à ce qu'un événement se produise sur l'un des descripteurs surveillés, et retourne le nombre d'événements prêts
            epoll_wait : attend les événements sur les descripteurs surveillés, et remplit le tableau events avec les événements prêts, le paramètre -1 signifie qu'on attend indéfiniment jusqu'à ce qu'un événement se produise
        */
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("Erreur epoll_wait");
            break;
        }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == listen_sock) {
                // Nouvelle connexion entrante
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);

                int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
                
                if (client_sock < 0) {
                    perror("Erreur accept");
                    continue;
                }

                set_nonblocking(client_sock);
                
                /*
                    EPOLLET : mode Edge Triggered, cela signifie que l'OS ne nous notifiera qu'une seule fois lorsqu'un événement se produit, et ne nous notifiera pas à nouveau tant que nous n'aurons pas traité cet événement, cela permet d'éviter les notifications redondantes et d'améliorer les performanceS
                    EPOLLIN : on veut être notifié lorsque des données sont disponibles à lire sur la socket du client, cela nous permettra de traiter les requêtes HTTP entrantes de ce client
                */
                ev.events = EPOLLIN | EPOLLET; // Edge Triggered mode
                ev.data.fd = client_sock;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sock, &ev);
                
            } else {
                int client_sock = events[n].data.fd;
                handle_http_request(client_sock);
                
                /*
                    Après avoir traité la requête, on ferme la connexion 
                    epoll_ctl : retirer le client de l'instance epoll pour ne plus surveiller les événements sur cette socket
                    close : fermer la socket du client pour libérer les ressources
                */
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_sock, NULL);
                close(client_sock);
            }
        }
    }

    close(listen_sock);
    close(epoll_fd);
}