CC = gcc
CFLAGS = -Wall -Wextra -Iinclude

SRC = src
BUILD = build
OBJDIR = $(BUILD)/obj
BINDIR = $(BUILD)/bin

# Liste des programmes
PROGRAMS = client serveur proxy

# Tous les fichiers .c
CSRC = $(wildcard $(SRC)/*.c)

# Corresponding objects in build/obj
OBJS = $(patsubst $(SRC)/%.c,$(OBJDIR)/%.o,$(CSRC))

all: dirs $(PROGRAMS:%=$(BINDIR)/%)

# ----- Directories -----
dirs:
	mkdir -p $(OBJDIR)
	mkdir -p $(BINDIR)

# ----- Object files -----
$(OBJDIR)/%.o: $(SRC)/%.c include/simpleSocketAPI.h
	$(CC) $(CFLAGS) -c $< -o $@

# ----- Executables -----
$(BINDIR)/%: $(OBJDIR)/%.o $(filter-out $(OBJDIR)/%.o,$(OBJS))
	$(CC) $^ -o $@

# ----- Clean -----
clean:
	rm -rf $(OBJDIR)/*
	rm -rf $(BINDIR)/*

.PHONY: all clean dirs
