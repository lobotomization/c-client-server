all: server client

client: client.c
		gcc client.c -o client -lpthread -std=gnu99 -Wall

server: server.c
		gcc server.c -o server -lrt -std=gnu99 -Wall





