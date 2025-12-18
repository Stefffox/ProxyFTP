# Compilateur et options
CC      = gcc
CFLAGS  = -Wall -Wextra -Iinclude

# Arborescence
SRC     = src
BUILD   = build
OBJDIR  = $(BUILD)/obj
BINDIR  = $(BUILD)/bin

# Programmes à générer
PROGRAMS = client serveur proxy

# Fichiers sources
SRCS = \
	$(SRC)/client.c \
	$(SRC)/serveur.c \
	$(SRC)/proxy.c \
	$(SRC)/simpleSocketAPI.c

# Objets correspondants
OBJS = $(SRCS:$(SRC)/%.c=$(OBJDIR)/%.o)

# Cible par défaut
all: dirs $(PROGRAMS:%=$(BINDIR)/%)

# ----- Répertoires -----
dirs:
	mkdir -p $(OBJDIR)
	mkdir -p $(BINDIR)

# ----- Règle générique pour les .o -----
$(OBJDIR)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# ----- Executables -----
# client dépend de client.o et simpleSocketAPI.o
$(BINDIR)/client: $(OBJDIR)/client.o $(OBJDIR)/simpleSocketAPI.o
	$(CC) $(CFLAGS) $^ -o $@

# serveur (si besoin de simpleSocketAPI, ajoute-le ici)
$(BINDIR)/serveur: $(OBJDIR)/serveur.o $(OBJDIR)/simpleSocketAPI.o
	$(CC) $(CFLAGS) $^ -o $@

# proxy dépend de proxy.o et simpleSocketAPI.o
$(BINDIR)/proxy: $(OBJDIR)/proxy.o $(OBJDIR)/simpleSocketAPI.o
	$(CC) $(CFLAGS) $^ -o $@

# ----- Nettoyage -----
clean:
	rm -rf $(OBJDIR) $(BINDIR)

.PHONY: all clean dirs
