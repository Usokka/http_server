#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "http.h"

#define BUFFER_SIZE 4096
#define WEB_ROOT "./www"

// Fonction utilitaire pour déterminer le type MIME
const char *get_mime_type(const char *filepath) {
    if (strstr(filepath, ".html")) return "text/html";
    if (strstr(filepath, ".css")) return "text/css";
    if (strstr(filepath, ".js")) return "application/javascript";
    if (strstr(filepath, ".png")) return "image/png";
    if (strstr(filepath, ".jpg") || strstr(filepath, ".jpeg")) return "image/jpeg";
    return "text/plain";
}

// Fonction pour envoyer une erreur 404
void send_404(int client_sock) {
    const char *response = 
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 53\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body><h1>404 - Fichier non trouve</h1></body></html>";
    write(client_sock, response, strlen(response));
}

void handle_http_request(int client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes_read = read(client_sock, buffer, BUFFER_SIZE - 1);
    
    if (bytes_read <= 0) return; // Erreur ou déconnexion
    
    buffer[bytes_read] = '\0'; // Sécurisation de la chaîne

    // Variables pour parser la ligne de requête HTTP
    char method[16], path[256], protocol[16];
    
    // Parsing simpliste (L3 style) via sscanf
    if (sscanf(buffer, "%15s %255s %15s", method, path, protocol) != 3) {
        return; // Requête malformée
    }

    printf("[LOG] Requete recue : %s %s\n", method, path);

    // On ne gère que le GET pour ce projet
    if (strcmp(method, "GET") != 0) {
        const char *response = "HTTP/1.1 501 Not Implemented\r\n\r\n";
        write(client_sock, response, strlen(response));
        return;
    }

    // Gestion de l'index par défaut
    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }

    // Sécurisation basique : empêcher la remontée de répertoire (directory traversal)
    if (strstr(path, "..")) {
        send_404(client_sock);
        return;
    }

    // Construction du chemin final (ex: ./www/index.html)
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s%s", WEB_ROOT, path);

    // Vérification de l'existence du fichier
    struct stat st;
    if (stat(filepath, &st) == -1) {
        send_404(client_sock);
        return;
    }

    // Lecture et envoi du fichier
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        send_404(client_sock);
        return;
    }

    // Envoi des en-têtes HTTP
    char header_buffer[512];
    snprintf(header_buffer, sizeof(header_buffer),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n",
             get_mime_type(filepath), st.st_size);
             
    write(client_sock, header_buffer, strlen(header_buffer));

    // Envoi du corps du fichier par morceaux (chunks)
    char file_buffer[1024];
    size_t n;
    while ((n = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0) {
        write(client_sock, file_buffer, n);
    }

    fclose(file);
}