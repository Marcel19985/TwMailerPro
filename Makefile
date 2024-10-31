# Makefile for twmailer client and server

CC = gcc
CFLAGS = -Wall -g
CLIENT = twmailer-client
SERVER = twmailer-server
CLIENT_SRC = twmailer-client.c
SERVER_SRC = twmailer-server.c

# Target to compile both client and server
all: $(CLIENT) $(SERVER)

# Compile the client program
$(CLIENT): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC)

# Compile the server program
$(SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC)

# Clean up the compiled programs
clean:
	rm -f $(CLIENT) $(SERVER)

# PHONY targets
.PHONY: all clean
