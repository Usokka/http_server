CC = gcc
# Flags de compilation typiques d'un bon projet étudiant
CFLAGS = -Wall -Wextra -g

TARGET = cweb

# Liste des fichiers sources
SRCS = main.c server.c http.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Règle générique pour compiler les .c en .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

fclean: clean
	rm -rf www/

# Cible pour préparer l'environnement de test
init:
	mkdir -p www
	echo "<h1>Bienvenue sur C-Web</h1><p>Serveur ecrit en C !</p>" > www/index.html