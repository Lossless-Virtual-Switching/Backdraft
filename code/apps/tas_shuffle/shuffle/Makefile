CC = gcc
CFLAGS = -O2 -Wall -I . -g

# This flag includes the Pthreads library on a Linux box.
# Others systems will probably require something different.
LIB = -lpthread -lrt -lm

all: server client

server: shuffle_server.c csapp.o
	$(CC) $(CFLAGS) -o shuffle_server shuffle_server.c csapp.o $(LIB)

client: shuffle_client.c csapp.o
	$(CC) $(CFLAGS) -o shuffle_client shuffle_client.c csapp.o $(LIB)

csapp.o:
	$(CC) $(CFLAGS) -c csapp.c

clean:
	rm -f *.o shuffle_server shuffle_client
