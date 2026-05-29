#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>       // Indispensable pour PATH_MAX
#include <sys/sendfile.h> // Indispensable pour sendfile()
#include <netinet/tcp.h>  // Indispensable pour TCP_CORK
#include <netinet/in.h>   // Indispensable pour IPPROTO_TCP
#include <time.h>         // Indispensable pour nanosleep()

#include "http.h"
#include "server.h"       // Indispensable pour la structure ClientContext

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
void send_404(int client_fd) {
  const char *response =
      "HTTP/1.1 404 Not Found\r\n"
      "Content-Type: text/html\r\n"
      "Content-Length: 53\r\n"
      "Connection: close\r\n"
      "\r\n"
      "<html><body><h1>404 - Fichier non trouve</h1></body></html>";
  write(client_fd, response, strlen(response));
}

int handle_http_request(ClientContext *client) {
  // 1. BOUCLE DE LECTURE NON-BLOQUANTE (Impératif avec epoll EPOLLET)
  while (1) {
    // Sécurité anti-overflow : si le buffer est plein et qu'on n'a toujours pas de \r\n\r\n
    if (client->total_read >= BUFFER_SIZE - 1) {
      printf("[WARNING] Requete trop grande, rejet (Buffer Overflow Protection)\n");
      
      // VIDANGE : On lit et on jette le reste des données pour éviter un paquet TCP RST
      char trash[512];
      while (read(client->fd, trash, sizeof(trash)) > 0) {
        // On consomme activement le flux résiduel
      }
      return 1; // Demande de fermeture propre à server.c
    }

    int bytes_read = read(client->fd, client->read_buf + client->total_read,
                          BUFFER_SIZE - client->total_read - 1);

    if (bytes_read > 0) {
      client->total_read += bytes_read;
      client->read_buf[client->total_read] = '\0';

      // Est-ce qu'on a enfin la fin des en-têtes HTTP ?
      if (strstr(client->read_buf, "\r\n\r\n")) {
        break; // Requête enfin complète, on sort pour la traiter !
      }
    } 
    else if (bytes_read == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0; // Le tampon de l'OS est vide, on attend le prochain signal epoll
      }
      return 1; // Vraie erreur réseau, on demande la fermeture
    } 
    else {
      return 1; // Le client s'est déconnecté proprement
    }
  }

  // 2. PARSING DE LA REQUÊTE ACCUMULÉE
  char method[16], path[256], protocol[16];
  if (sscanf(client->read_buf, "%15s %255s %15s", method, path, protocol) != 3) {
    return 1; // Requête malformée
  }

  printf("[LOG] Requete recue : %s %s\n", method, path);

  // On ne gère que le GET pour ce projet
  if (strcmp(method, "GET") != 0) {
    const char *response = "HTTP/1.1 501 Not Implemented\r\n\r\n";
    write(client->fd, response, strlen(response));
    return 1;
  }

  // Gestion de l'index par défaut
  if (strcmp(path, "/") == 0) {
    strcpy(path, "/index.html");
  }

  // 3. SÉCURISATION ABSOLUE DU CHEMIN (Anti-Directory Traversal avec realpath)
  char requested_filepath[512];
  snprintf(requested_filepath, sizeof(requested_filepath), "%s%s", WEB_ROOT, path);

  char resolved_base[PATH_MAX];
  char resolved_file[PATH_MAX];

  // Résolution des chemins absolus (nettoie les .., décode l'URL)
  if (realpath(WEB_ROOT, resolved_base) == NULL ||
      realpath(requested_filepath, resolved_file) == NULL) {
    send_404(client->fd);
    return 1;
  }

  // Vérification stricte de confinement dans le dossier WEB_ROOT
  if (strncmp(resolved_file, resolved_base, strlen(resolved_base)) != 0) {
    printf("[WARNING] Tentative de Directory Traversal interceptée ! Path: %s\n", path);
    send_404(client->fd);
    return 1;
  }

  // Vérification de l'existence et validation que c'est bien un fichier (pas un dossier)
  struct stat st;
  if (stat(resolved_file, &st) == -1 || !S_ISREG(st.st_mode)) {
    send_404(client->fd);
    return 1;
  }

  // Ouverture du fichier en mode descripteur brut
  int file_fd = open(resolved_file, O_RDONLY);
  if (file_fd == -1) {
    send_404(client->fd);
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

  // TCP_CORK : on dit au noyau de retenir le segment TCP jusqu'au uncork.
  // Garantit que headers + corps arrivent dans le même burst TCP,
  // évitant la race où un recv(N) côté client ne capture que les headers
  // avant que sendfile() n'ait eu le temps de pousser le corps.
  int cork = 1;
  setsockopt(client->fd, IPPROTO_TCP, TCP_CORK, &cork, sizeof(cork));

  write(client->fd, header_buffer, strlen(header_buffer));

  // 4. ENVOI OPTIMISÉ VIA LE NOYAU LINUX (sendfile)
  // Boucle de retry : la socket est non-bloquante, sendfile peut faire
  // des envois partiels si le buffer d'émission du noyau se remplit.
  off_t offset = 0;
  ssize_t remaining = st.st_size;
  while (remaining > 0) {
    ssize_t sent = sendfile(client->fd, file_fd, &offset, remaining);
    if (sent > 0) {
      remaining -= sent;
    } else if (sent == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Buffer d'envoi plein — pause de 1 ms puis on réessaie
        struct timespec ts = {0, 1000000};
        nanosleep(&ts, NULL);
        continue;
      }
      break; // Vraie erreur réseau
    } else {
      break; // EOF inattendu sur le fichier source
    }
  }
  close(file_fd);

  // Uncork : on libère les données retenues, le noyau envoie maintenant
  cork = 0;
  setsockopt(client->fd, IPPROTO_TCP, TCP_CORK, &cork, sizeof(cork));

  return 1; // Traitement complet terminé, on ferme la socket et free le contexte
}