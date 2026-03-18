# 🚀 Tutoriel : Créer un Serveur Web Haute Performance en C (de A à Z)

Bienvenue dans ce tutoriel ! Nous allons analyser et construire pas à pas un serveur web statique en C. Ce serveur n'est pas un simple jouet : il utilise **epoll**, une technologie avancée sous Linux pour gérer de multiples connexions de manière asynchrone et non-bloquante.

## 📋 Prérequis

- Un environnement Linux (le système epoll est spécifique à Linux)
- Un compilateur C (gcc)
- L'outil make pour la compilation
- Des bases en langage C (pointeurs, structures, allocation)

## 🏛️ Architecture du Projet

Le projet est divisé de manière modulaire pour séparer les responsabilités :

- **main.c** : Le point d'entrée. Gère les arguments de la ligne de commande.
- **server.c / server.h** : Le cœur réseau. Gère les connexions TCP/IP et la boucle d'événements (epoll).
- **http.c / http.h** : La logique métier. Comprend et répond aux requêtes du protocole HTTP.
- **Makefile** : Automatise la compilation et prépare l'environnement de test.

## Étape 1 : Le point d'entrée (main.c)

Le rôle du main.c est extrêmement simple : il configure le port d'écoute et lance le serveur.

```c
#include <stdio.h>
#include <stdlib.h>
#include "server.h"

int main(int argc, char *argv[]) {
    int port = 8080; // Port par défaut

    // Permet de choisir le port au lancement: ./cweb 9000
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
```

### 💡 Explication :

On lit l'argument passé en ligne de commande (ex: `./cweb 8080`). Si aucun port n'est fourni, on utilise 8080 par défaut. On vérifie que le port est valide (entre 1 et 65535), puis on appelle `start_server()`.

## Étape 2 : Le Cœur Réseau avec epoll (server.c)

C'est ici que réside la complexité technique. Un serveur classique crée un thread (processus léger) pour chaque client. C'est lourd. Notre serveur utilise **epoll**, qui permet à un seul thread de surveiller des milliers de connexions simultanément.

### 2.1 - Rendre les sockets non-bloquantes

Pour qu'epoll fonctionne de manière optimale, les actions de lecture/écriture ne doivent pas bloquer le programme d'attendre.

```c
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
```

### 2.2 - Initialisation de la Socket

Dans `start_server(int port)`, nous devons préparer le terrain :

- `socket()` : Crée un point de terminaison réseau (IPv4, TCP).
- `setsockopt()` : Utilise **SO_REUSEADDR** pour pouvoir relancer le serveur immédiatement après un crash sans l'erreur "Address already in use".
- `bind()` : Associe la socket au port choisi (ex: 8080).
- `listen()` : Indique à l'OS que nous sommes prêts à accepter des connexions.

### 2.3 - La magie de epoll

```c
epoll_fd = epoll_create1(0);
// On ajoute la socket d'écoute à epoll
ev.events = EPOLLIN;
ev.data.fd = listen_sock;
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &ev);
```

Nous disons à Linux : **"Préviens-moi quand il y a de l'activité (EPOLLIN) sur la socket d'écoute (listen_sock)"**.

### 2.4 - La boucle principale

```c
while (1) {
    int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1); // Attend un événement

    for (int n = 0; n < nfds; ++n) {
        if (events[n].data.fd == listen_sock) {
            // NOUVEAU CLIENT !
            int client_sock = accept(listen_sock, ...);
            set_nonblocking(client_sock);

            // On ajoute le client à epoll (Edge Triggered)
            ev.events = EPOLLIN | EPOLLET; 
            ev.data.fd = client_sock;
            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sock, &ev);
        } else {
            // REQUÊTE D'UN CLIENT EXISTANT !
            int client_sock = events[n].data.fd;
            handle_http_request(client_sock);

            // Fin de la communication HTTP/1.0
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_sock, NULL);
            close(client_sock);
        }
    }
}
```

### 🔍 Que se passe-t-il ici ?

1. `epoll_wait` met le programme en pause jusqu'à ce qu'un événement réseau se produise.
2. Si c'est sur la `listen_sock`, c'est un nouveau visiteur. On l'accepte et on demande à epoll de surveiller ce nouveau client.
3. Si c'est sur une autre socket, c'est un client qui nous envoie une requête HTTP. On passe le relais à `handle_http_request()`.

## Étape 3 : Le Protocole HTTP (http.c)

Maintenant que nous avons reçu des données du client, il faut les comprendre (HTTP) et renvoyer la bonne page web.

### 3.1 - Lire et comprendre la requête

```c
char buffer[BUFFER_SIZE];
int bytes_read = read(client_sock, buffer, BUFFER_SIZE - 1);
buffer[bytes_read] = '\0'; // Toujours terminer la chaîne !

char method[16], path[256], protocol[16];
if (sscanf(buffer, "%15s %255s %15s", method, path, protocol) != 3) {
    return; // Requête malformée
}
```

Ici, on extrait les informations. Si le navigateur demande l'accueil, `buffer` contiendra `GET / HTTP/1.1`. `sscanf` va séparer cela en :

- `method = "GET"`
- `path = "/"`
- `protocol = "HTTP/1.1"`

### 3.2 - Routage et Sécurité

```c
if (strcmp(method, "GET") != 0) {
    // Erreur 501: On ne gère pas les POST, PUT, etc.
    return;
}
if (strcmp(path, "/") == 0) strcpy(path, "/index.html");

// SÉCURITÉ : Empêcher de lire /etc/passwd !
if (strstr(path, "..")) {
    send_404(client_sock);
    return;
}
```

On s'assure que le visiteur ne peut pas remonter dans les dossiers du serveur en utilisant `../...`.

### 3.3 - Trouver le type MIME

Pour que le navigateur affiche correctement une image ou du CSS, il faut lui indiquer le type de fichier via la fonction `get_mime_type()` (ex: `.html` → `text/html`, `.png` → `image/png`).

### 3.4 - Lire le fichier et répondre

```c
char filepath[512];
snprintf(filepath, sizeof(filepath), "./www%s", path);

struct stat st;
if (stat(filepath, &st) == -1) {
    send_404(client_sock); return; // Le fichier n'existe pas
}

FILE *file = fopen(filepath, "rb");
// ... Envoi de l'en-tête HTTP (200 OK, Content-Type, Content-Length) ...

char file_buffer[1024];
size_t n;
while ((n = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0) {
    write(client_sock, file_buffer, n); // Envoi du fichier par blocs
}
```

On construit le chemin du fichier (ex: `./www/index.html`). On récupère sa taille avec `stat`. On envoie la réponse HTTP officielle, puis on lit le fichier par petits morceaux de 1024 octets qu'on expédie au client.

## Étape 4 : L'Automatisation avec le Makefile

Pour éviter de taper de longues commandes GCC, le Makefile gère la compilation :

```makefile
all: $(TARGET)

$(TARGET): $(OBJS)
    $(CC) $(CFLAGS) -o $(TARGET) $(OBJS)
```

Il contient aussi une commande très pratique pour préparer le dossier web :

```makefile
init:
    mkdir -p www
    echo "<h1>Bienvenue sur C-Web</h1><p>Serveur ecrit en C !</p>" > www/index.html
```

## Étape 5 : Compilation et Test

Vous avez maintenant tous les fichiers. Voici comment lancer la machine :

1. Ouvrez votre terminal dans le dossier contenant les fichiers.

2. Compilez le projet :

```bash
make
```

3. Initialisez le dossier racine web (crée le dossier www et un index.html) :

```bash
make init
```

4. Lancez le serveur (sur le port 8080 par défaut) :

```bash
./cweb
```

*(Vous devriez voir `=== Demarrage du serveur C-Web ===` et `Serveur en ecoute sur le port 8080`)*

5. **Testez !**
   - Ouvrez votre navigateur web et allez sur : **http://localhost:8080**
   - Vous verrez votre page d'accueil s'afficher.
   - Regardez votre terminal : vous verrez les requêtes s'afficher (ex: `[LOG] Requete recue : GET /`).

## 📚 Annexe : Documentation Détaillée (Fonctions, Paramètres et Structures)

Afin de bien comprendre les rouages du serveur, voici un dictionnaire de toutes les fonctions et structures de données utilisées dans ce projet, avec l'explication précise de chaque paramètre.

### 🛠️ 1. Fonctions Internes du Projet (Créées par nous)

#### `int main(int argc, char *argv[])` (dans main.c)

**Rôle** : Point d'entrée du programme.

**Paramètres** :

- `argc` (int) : Le nombre d'arguments passés en ligne de commande (vaut au moins 1, car le nom de l'exécutable compte).
- `argv` (char *[]) : Un tableau de chaînes de caractères contenant ces arguments (ex: `argv[1]` contiendra le port "8080" si tapé par l'utilisateur).

#### `void start_server(int port)` (dans server.c)

**Rôle** : Initialise toute l'infrastructure réseau, crée epoll, et lance la boucle d'écoute.

**Paramètres** :

- `port` (int) : Le numéro du port TCP sur lequel le serveur doit écouter (ex: 8080).

#### `int set_nonblocking(int fd)` (dans server.c)

**Rôle** : Modifie le comportement d'un descripteur de fichier pour qu'il ne bloque pas l'exécution du programme.

**Paramètres** :

- `fd` (int) : Le "file descriptor" (souvent une socket réseau) que l'on souhaite rendre non-bloquant.

#### `void handle_http_request(int client_sock)` (dans http.c)

**Rôle** : Lit la requête d'un client, l'analyse et lui renvoie la réponse HTTP (fichiers ou erreur 404).

**Paramètres** :

- `client_sock` (int) : Le descripteur de fichier (la socket) connecté spécifiquement à ce client. C'est dans ce descripteur qu'on lit et écrit.

#### `const char *get_mime_type(const char *filepath)` (dans http.c)

**Rôle** : Déduit le type MIME (text/html, image/png...) en fonction de l'extension du fichier demandé.

**Paramètres** :

- `filepath` (const char *) : Le chemin complet du fichier demandé par le client (ex: "./www/style.css").

#### `void send_404(int client_sock)` (dans http.c)

**Rôle** : Envoie au client une réponse HTTP standard indiquant que la ressource n'existe pas.

**Paramètres** :

- `client_sock` (int) : La socket du client à qui envoyer l'erreur.

### 📦 2. Structures de Données Système (et leurs champs utilisés)

#### `struct sockaddr_in` (de `<netinet/in.h>`)

**Rôle** : Représente une adresse réseau pour la famille de protocoles IPv4.

**Champs utilisés** :

- `sin_family` : Toujours défini à `AF_INET` pour spécifier qu'on utilise IPv4.
- `sin_port` : Le numéro de port. Doit être converti au format "Network Byte Order" avec la fonction `htons()`.
- `sin_addr.s_addr` : L'adresse IP. Dans notre cas, `INADDR_ANY` signifie "écoute sur toutes les interfaces réseau disponibles de cette machine".

#### `struct epoll_event` (de `<sys/epoll.h>`)

**Rôle** : Utilisée par epoll pour définir quel événement on écoute sur un descripteur donné, ou pour retourner quel événement a été déclenché.

**Champs utilisés** :

- `events` (uint32_t) : Un masque de bits définissant le type d'événement. Nous utilisons `EPOLLIN` (des données sont prêtes à être lues) et `EPOLLET` (mode Edge Triggered, signale seulement le changement d'état).
- `data.fd` (int) : Le descripteur de fichier associé. C'est ici qu'on stocke `listen_sock` ou `client_sock` pour s'en souvenir quand epoll nous notifie.

#### `struct stat` (de `<sys/stat.h>`)

**Rôle** : Contient des métadonnées sur un fichier, récupérées via la fonction `stat()`.

**Champ utilisé** :

- `st_size` (off_t) : La taille totale du fichier en octets. Essentiel pour renseigner l'en-tête HTTP `Content-Length`.

### 🐧 3. Appels Système et Fonctions Standard Utilisés (avec paramètres détaillés)

#### Réseau et Sockets (`<sys/socket.h>`)

##### `int socket(int domain, int type, int protocol)`

- `domain` : La famille de protocole (`AF_INET` pour IPv4).
- `type` : Le type de socket (`SOCK_STREAM` pour TCP, un flux fiable).
- `protocol` : Le protocole spécifique (0 demande au système de choisir le protocole par défaut du type choisi, soit TCP).

##### `int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)`

- `sockfd` : La socket à configurer.
- `level` : Le niveau de la pile réseau où s'applique l'option (`SOL_SOCKET` pour le niveau socket général).
- `optname` : L'option à changer (`SO_REUSEADDR` pour réutiliser immédiatement le port local).
- `optval` : Pointeur vers la valeur de l'option (un int valant 1).
- `optlen` : La taille en octets de optval (soit `sizeof(int)`).

##### `int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)`

- `sockfd` : La socket que l'on veut lier à un port.
- `addr` : Pointeur vers notre structure sockaddr_in (castée en `struct sockaddr *`).
- `addrlen` : La taille de la structure adresse (`sizeof(addr)`).

##### `int listen(int sockfd, int backlog)`

- `sockfd` : La socket qui devient une socket d'écoute passive.
- `backlog` : Le nombre maximum de connexions en attente non encore acceptées (`SOMAXCONN` laisse l'OS décider du maximum raisonnable).

##### `int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)`

- `sockfd` : La socket d'écoute.
- `addr` : Pointeur vers une structure vide qui sera remplie par l'OS avec l'adresse IP du client qui se connecte.
- `addrlen` : Pointeur vers la taille de la structure addr.

#### Mécanisme Epoll (`<sys/epoll.h>`)

##### `int epoll_create1(int flags)`

- `flags` : Options de création. Vaut 0 ici (comportement standard).

##### `int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)`

- `epfd` : Le descripteur de l'instance epoll (retourné par `epoll_create1`).
- `op` : L'opération à effectuer (`EPOLL_CTL_ADD` pour ajouter, `EPOLL_CTL_DEL` pour retirer).
- `fd` : Le descripteur de fichier à surveiller (socket écoute ou socket client).
- `event` : Pointeur vers la configuration de l'événement (`struct epoll_event` remplie).

##### `int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)`

- `epfd` : L'instance epoll.
- `events` : Un tableau vide où Linux va écrire les événements qui se sont produits.
- `maxevents` : Le nombre maximal d'événements à récupérer en une fois (la taille de notre tableau events).
- `timeout` : Temps d'attente max en millisecondes (-1 signifie "attendre indéfiniment sans jamais se réveiller tant qu'il n'y a pas de réseau").

#### Manipulation de fichiers et I/O système

##### `int fcntl(int fd, int cmd, ... /* arg */ )`

- `fd` : Le descripteur de fichier.
- `cmd` : La commande. `F_GETFL` récupère les "flags" (drapeaux) actuels. `F_SETFL` définit de nouveaux drapeaux en passant l'argument optionnel arg (où l'on injecte `O_NONBLOCK`).

##### `ssize_t read(int fd, void *buf, size_t count)`

- `fd` : La socket du client.
- `buf` : Le tableau/buffer en mémoire où stocker les octets reçus (requête HTTP).
- `count` : Le nombre maximal d'octets à lire pour ne pas déborder du buffer.

##### `ssize_t write(int fd, const void *buf, size_t count)`

- `fd` : La socket du client.
- `buf` : Le texte, l'en-tête ou les données binaires du fichier à envoyer.
- `count` : Le nombre exact d'octets à envoyer.

##### `int stat(const char *pathname, struct stat *statbuf)`

- `pathname` : Le chemin du fichier sur le disque (ex: "./www/index.html").
- `statbuf` : Un pointeur vers notre struct stat qui sera remplie par la fonction avec les infos du fichier (poids, permissions, date...).

#### Opérations de bibliothèque C Standard (stdio.h, string.h)

##### `int snprintf(char *str, size_t size, const char *format, ...)`

- `str` : Le buffer de destination.
- `size` : La taille max du buffer, prévient les dépassements de mémoire.
- `format` : Chaîne de formatage (ex: "%s%s").
- `...` : Les variables qui viendront remplacer les %s, %d, etc.

##### `int sscanf(const char *str, const char *format, ...)`

- `str` : La chaîne source à analyser (ici, la première ligne de la requête HTTP).
- `format` : Le motif de découpage ("%15s %255s %15s" = lire 3 mots, avec des limites de longueur max).
- `...` : Les pointeurs vers les variables de destination (method, path, protocol).

##### `FILE *fopen(const char *pathname, const char *mode)`

- `pathname` : Chemin du fichier.
- `mode` : Mode d'ouverture. "rb" signifie "Read Binary" (lecture seule, ne pas modifier les sauts de ligne, crucial pour les images).

##### `size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)`

- `ptr` : Le buffer de destination où stocker le morceau de fichier lu.
- `size` : La taille d'un "élément" (souvent 1 octet).
- `nmemb` : Le nombre d'éléments à lire par itération (la taille totale de notre buffer temporaire).
- `stream` : Le pointeur de fichier `FILE *` ouvert précédemment.