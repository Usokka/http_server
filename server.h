#ifndef SERVER_H
#define SERVER_H

#define BUFFER_SIZE 4096

typedef struct {
  int fd;
  char read_buf[BUFFER_SIZE];
  int total_read;
} ClientContext;

void start_server(int port);
int set_nonblocking(int fd);

#endif