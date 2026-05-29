# 🚀 C-Web — Serveur HTTP Asynchrone Haute Performance en C

Bienvenue dans la documentation technique de **C-Web**, un serveur web statique ultra-performant écrit en C sous Linux.

Conçu pour maximiser la réactivité et l’efficacité des ressources système, C-Web repose sur une architecture événementielle asynchrone basée sur l’API noyau `epoll` en mode **Edge-Triggered (`EPOLLET`)**.

Ce document sert à la fois de :

* Spécification technique
* Documentation d’architecture
* Guide de sécurité
* Manuel développeur / DevOps

---

# 🛠️ Table des matières

1. [Architecture Réseau & Boucle d’Événements](#1-architecture-réseau--boucle-dévenements)
2. [Modèle de Contexte Client Asynchrone](#2-modèle-de-contexte-client-asynchrone)
3. [Cycle de Vie d’une Requête HTTP](#3-cycle-de-vie-dune-requête-http)
4. [Mitigations de Sécurité Robustes](#4-mitigations-de-sécurité-robustes)
5. [Optimisation des Performances Noyau](#5-optimisation-des-performances-noyau)
6. [Compilation, Déploiement et Tests](#6-compilation-déploiement-et-tests)

---

# 1. Architecture Réseau & Boucle d’Événements

C-Web abandonne volontairement les modèles classiques :

* **Process-per-connection**
* **Thread-per-connection**

Ces approches deviennent extrêmement coûteuses à grande échelle à cause :

* des changements de contexte CPU,
* de la consommation mémoire,
* de la contention entre threads.

À la place, le serveur utilise un modèle :

* **Single-thread**
* **Event-driven**
* **Asynchrone**
* basé sur `epoll`

Un seul thread peut ainsi gérer simultanément plusieurs milliers de connexions TCP.

---

## 🔬 Pourquoi `epoll` plutôt que `select` ou `poll` ?

### `select` / `poll`

Les APIs traditionnelles nécessitent de parcourir l’intégralité des descripteurs à chaque boucle :

```text
Complexité : O(N)
```

Cela devient rapidement inefficace lorsque le nombre de sockets augmente.

### `epoll`

`epoll` repose sur :

* un arbre rouge-noir pour les sockets surveillées,
* une liste interne des descripteurs réellement actifs.

Le noyau ne retourne donc que les sockets ayant déclenché un événement.

```text
Complexité amortie : O(1)
```

---

## ⚡ Edge-Triggered (`EPOLLET`) vs Level-Triggered

Par défaut, `epoll` fonctionne en mode **Level-Triggered** :

> tant que des données restent disponibles, le noyau renvoie des notifications.

C-Web utilise volontairement le mode :

```c
EPOLLET
```

afin de réduire drastiquement :

* les réveils CPU,
* les appels système inutiles,
* la charge globale de la boucle événementielle.

---

## ⚠️ Conséquence critique du mode Edge-Triggered

En mode `EPOLLET`, le noyau ne notifie qu’une seule fois lors du changement d’état de la socket.

Le serveur doit donc impérativement :

```c
read()
```

dans une boucle jusqu’à :

```c
EAGAIN
```

ou :

```c
EWOULDBLOCK
```

Sinon :

* des données restent dans le buffer noyau,
* aucune nouvelle notification n’est envoyée,
* la connexion devient bloquée définitivement (starvation).

---

## 🔁 Boucle d’Événements

```text
[ Client Connects ]
        │
        ▼
[ accept() non-blocking ]
        │
        ▼
Allocate ClientContext
        │
        ▼
[ epoll_ctl: ADD EPOLLIN | EPOLLET ]
        │
        ▼
┌──────────────────────────────────────────────┐
│            epoll_wait()                     │
└──────────────────────────────────────────────┘
        │
        ├── listen socket active
        │       └── accept loop
        │
        └── client socket active
                └── read loop
                        until EAGAIN
```

---

# 2. Modèle de Contexte Client Asynchrone

Chaque connexion TCP possède une structure persistante dédiée :

```c
#define BUFFER_SIZE 4096

typedef struct {
    int fd;
    char read_buf[BUFFER_SIZE];
    int total_read;
} ClientContext;
```

---

## 📦 Rôle des champs

| Champ        | Description                          |
| ------------ | ------------------------------------ |
| `fd`         | Descripteur de la socket client      |
| `read_buf`   | Tampon mémoire d’accumulation HTTP   |
| `total_read` | Nombre d’octets actuellement stockés |

---

## 🧠 Gestion des unions `epoll_data_t`

L’union `epoll_data_t` partage son espace mémoire entre :

* `fd`
* `ptr`

C-Web sépare strictement les usages :

### Socket d’écoute

```c
ev.data.fd = listen_sock;
```

### Sockets clients

```c
ev.data.ptr = client;
```

---

## 🔀 Dispatch sécurisé

```c
if (events[n].data.fd == listen_sock) {

    // Nouvelle connexion

} else {

    ClientContext *client =
        (ClientContext *)events[n].data.ptr;

    // Traitement réseau
}
```

Cette approche évite :

* les collisions mémoire,
* les erreurs de cast,
* les confusions de descripteurs.

---

# 3. Cycle de Vie d’une Requête HTTP

Le traitement principal est orchestré par :

```c
handle_http_request(ClientContext *client)
```

---

## 3.1 Accumulation Asynchrone Non-Bloquante

Les données sont lues progressivement :

```c
int bytes_read = read(
    client->fd,
    client->read_buf + client->total_read,
    BUFFER_SIZE - client->total_read - 1
);
```

---

### Cas `EAGAIN`

```c
if (errno == EAGAIN)
```

Le serveur :

* suspend immédiatement la lecture,
* conserve l’état mémoire,
* retourne à `epoll_wait()`.

---

## Détection de fin HTTP

Le parseur attend :

```text
\r\n\r\n
```

via :

```c
strstr()
```

---

## 3.2 Parsing HTTP

Extraction sécurisée :

```c
sscanf(
    client->read_buf,
    "%15s %255s %15s",
    method,
    path,
    protocol
);
```

---

## Méthodes supportées

### Implémentée

```http
GET
```

### Rejetées

* POST
* PUT
* DELETE
* PATCH

Réponse :

```http
501 Not Implemented
```

---

# 4. Mitigations de Sécurité Robustes

---

# 🛡️ 4.1 Protection contre le Directory Traversal

Exemple d’attaque :

```text
../../etc/passwd
```

---

## Solution : `realpath()`

Le serveur :

1. Canonicalise la racine web
2. Canonicalise le fichier demandé
3. Vérifie l’inclusion stricte

---

## Validation de confinement

```c
if (
    strncmp(
        resolved_file,
        resolved_base,
        strlen(resolved_base)
    ) != 0
) {
    // Refus immédiat
}
```

Cette protection neutralise :

* `../`
* `%2e%2e`
* liens symboliques
* chemins ambigus

---

# 🛡️ 4.2 Protection Anti-Overflow

Le serveur impose une limite stricte :

```c
if (client->total_read >= BUFFER_SIZE - 1)
```

Cela empêche :

* buffer overflow,
* corruption mémoire,
* segmentation fault,
* stack smashing.

---

# 🛡️ 4.3 Vidange Active des Buffers TCP

Fermer brutalement une socket contenant encore des données non lues provoque :

```text
RST (TCP Reset)
```

Le client reçoit alors :

```text
Connection reset by peer
```

---

## Solution : draining actif

```c
char trash[512];

while (
    read(client->fd, trash, sizeof(trash)) > 0
) {
    // purge du buffer noyau
}
```

Le serveur peut ensuite fermer proprement la connexion via :

```text
FIN
```

---

# 🛡️ 4.4 Validation des fichiers réguliers

Le serveur refuse les répertoires :

```c
struct stat st;

if (
    stat(resolved_file, &st) == -1 ||
    !S_ISREG(st.st_mode)
) {
    send_404(client->fd);
}
```

Cela évite :

* les fuites de métadonnées,
* les listings accidentels,
* les comportements non définis.

---

# 5. Optimisation des Performances Noyau

---

# 🚀 5.1 Zero-Copy avec `sendfile()`

Approche classique :

```text
Disk → User Space → Kernel → Socket
```

C-Web utilise :

```c
sendfile()
```

Le transfert devient :

```text
Disk/Page Cache → Kernel Socket Buffer
```

---

## Gains

* zéro copie utilisateur,
* moins de changements de contexte,
* CPU réduit,
* saturation rapide du débit réseau.

---

# 🤐 5.2 Agrégation réseau avec `TCP_CORK`

Afin d’éviter :

* un paquet pour les headers,
* un paquet pour le fichier,

le serveur utilise :

```c
int cork = 1;

setsockopt(
    client->fd,
    IPPROTO_TCP,
    TCP_CORK,
    &cork,
    sizeof(cork)
);
```

Linux agrège alors :

* headers HTTP
* contenu du fichier

dans un seul flux optimisé MTU.

---

# 6. Compilation, Déploiement et Tests

---

# 🏗️ Compilation

## Initialisation

```bash
make init
```

---

## Compilation

```bash
make
```

Options :

```text
-Wall -Wextra
```

---

## Exécution des tests

```bash
make test
```

---

## Nettoyage

```bash
make clean
```

---

# 🧪 Suite de Tests d’Intégration

Le projet embarque un banc de tests Python automatisé simulant :

* clients normaux,
* clients lents,
* clients malveillants,
* requêtes fragmentées,
* tentatives d’exploitation.

---

## Cas couverts

| ID | Test                      | Validation        |
| -- | ------------------------- | ----------------- |
| 1  | GET standard              | 200 OK            |
| 2  | Directory Traversal       | Bloqué            |
| 3  | Traversal encodé `%2e%2e` | Bloqué            |
| 4  | Client lent fragmenté     | Reconstitué       |
| 5  | Fichier inexistant        | 404               |
| 6  | HTTP malformé             | Pas de crash      |
| 7  | POST                      | 501               |
| 8  | Fragmentation agressive   | Géré              |
| 9  | Overflow 5000 octets      | Protection active |
| 10 | Accès répertoire          | Rejet propre      |

---

# 🎯 Objectifs Techniques

C-Web démontre :

* une architecture événementielle bas niveau moderne,
* une gestion mémoire sécurisée,
* une exploitation avancée des APIs Linux,
* une optimisation noyau proche des serveurs industriels.

---

# 📌 Exemple de lancement

```bash
make
./c-web
```

Puis :

```bash
curl http://127.0.0.1:8080/
```

---

# 📚 Technologies utilisées

* Langage C
* Linux
* epoll
* sockets POSIX
* TCP/IP
* sendfile
* TCP_CORK
* non-blocking I/O

---

# 👨‍💻 Auteur

Projet développé dans un objectif :

* d’apprentissage système bas niveau,
* d’ingénierie réseau,
* d’optimisation Linux,
* et de compréhension avancée des serveurs HTTP.

---

# 🚀 Push Final

```bash
git add README.md
git commit -m "docs: write comprehensive high-performance C-Web server documentation"
git push origin main
```
