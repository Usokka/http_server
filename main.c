#include <stdio.h>
#include <stdlib.h>
#include "server.h"

int main(int argc, char *argv[]) {
    int port = 8080; 

    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Erreur: Port invalide.\n");
            return EXIT_FAILURE;
        }
    }

    printf("=== Demarrage du serveur C-Web ===\n");
    start_server(port);

    return EXIT_SUCCESS;
}