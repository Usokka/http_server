#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>       // Indispensable pour PATH_MAX
#include <sys/sendfile.h> // Indispensable pour sendfile()

#include "http.h"

#define BUFFER_SIZE 4096
#define WEB_ROOT "./www"

// Gestion de la macro de secours si PATH_MAX fait de la résistance
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Fonction utilitaire pour déterminer le type MIME
const char *get_mime_type(const char *filepath) {
  if (strstr(filepath, ".html")) return "text/html";
  if (strstr(filepath, ".css"))  return "text/css";
  if (strstr(filepath, ".js"))   return "application/javascript";
  if (strstr(filepath, ".png"))  return "image/png";
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

int handle_http_request(int client_sock) {
  char buffer[BUFFER_SIZE];
  int total_read = 0;

  // 1. BOUCLE DE LECTURE NON-BLOQUANTE (Impératif avec epoll EPOLLET)
  while (1) {
    // On lit à la suite du buffer pour accumuler les morceaux de requêtes
    int bytes_read = read(client_sock, buffer + total_read, BUFFER_SIZE - total_read - 1);

    if (bytes_read > 0) {
      total_read += bytes_read;
      buffer[total_read] = '\0'; // On garde la chaîne propre pour strstr

      // Est-ce qu'on a reçu la fin de la requête HTTP (\r\n\r\n) ?
      if (strstr(buffer, "\r\n\r\n")) {
        break; // Requête complète !
      }
    } 
    else if (bytes_read == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // L'OS n'a plus de données pour l'instant et la requête n'a pas encore son \r\n\r\n.
        // On retourne 0 pour garder la socket ouverte dans epoll.
        return 0; 
      }
      return 1; // Une vraie erreur réseau, on demande la fermeture
    } 
    else {
      return 1; // bytes_read == 0 -> Le client s'est déconnecté
    }
  }

  // Variables pour parser la ligne de requête HTTP
  char method[16], path[256], protocol[16];

  // Parsing sécurisé via sscanf
  if (sscanf(buffer, "%15s %255s %15s", method, path, protocol) != 3) {
    return 1; // Requête malformée, on coupe
  }

  printf("[LOG] Requete recue : %s %s\n", method, path);

  // On ne gère que le GET pour ce projet
  if (strcmp(method, "GET") != 0) {
    const char *response = "HTTP/1.1 501 Not Implemented\r\n\r\n";
    write(client_sock, response, strlen(response));
    return 1;
  }

  // Gestion de l'index par défaut
  if (strcmp(path, "/") == 0) {
    strcpy(path, "/index.html");
  }

  // 2. SÉCURISATION ABSOLUE DU CHEMIN (Anti-Directory Traversal avec realpath)
  char requested_filepath[512];
  snprintf(requested_filepath, sizeof(requested_filepath), "%s%s", WEB_ROOT, path);

  char resolved_base[PATH_MAX];
  char resolved_file[PATH_MAX];

  // Résolution des chemins absolus (supprime les .., les liens symboliques et décode les caractères)
  if (realpath(WEB_ROOT, resolved_base) == NULL || 
      realpath(requested_filepath, resolved_file) == NULL) {
    send_404(client_sock);
    return 1;
  }

  // On vérifie strictement que le fichier demandé est confiné dans le sous-répertoire de www
  if (strncmp(resolved_file, resolved_base, strlen(resolved_base)) != 0) {
    printf("[WARNING] Tentative de Directory Traversal interceptée ! Path: %s\n", path);
    send_404(client_sock);
    return 1;
  }

  // Vérification de l'existence du fichier et validation que c'est un fichier régulier (pas un dossier)
  struct stat st;
  if (stat(resolved_file, &st) == -1 || !S_ISREG(st.st_mode)) {
    send_404(client_sock);
    return 1;
  }

  // Ouverture du fichier en mode descripteur brut pour sendfile
  int file_fd = open(resolved_file, O_RDONLY);
  if (file_fd == -1) {
    send_404(client_sock);
    return 1;
  }

  // Envoi des en-têtes HTTP
  char header_buffer[512];
  snprintf(header_buffer, sizeof(header_buffer),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: %s\r\n"
           "Content-Length: %ld\r\n"
           "Connection: close\r\n"
           "\r\n",
           get_mime_type(resolved_file), st.st_size);

  write(client_sock, header_buffer, strlen(header_buffer));

  // 3. ENVOI OPTIMISÉ VIA LE NOYAU LINUX (sendfile)
  off_t offset = 0;
  sendfile(client_sock, file_fd, &offset, st.st_size);

  close(file_fd);
  return 1; // Traitement complet terminé, on signale qu'on peut fermer
}