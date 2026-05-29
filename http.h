#ifndef HTTP_H
#define HTTP_H

#include "server.h"

// Traite la requête HTTP brute et envoie une réponse
int handle_http_request(ClientContext *client);

#endif