CLOPT = -c
LIBS = -lpq

all: server cli

Pool.o:		Pool.cpp Pool.h server.h
			g++ $(CLOPT) Pool.cpp

PoolItem.o:		PoolItem.cpp PoolItem.h server.h
			g++ $(CLOPT) PoolItem.cpp

PoolItemListen.o:		PoolItemListen.cpp PoolItemListen.h server.h
			g++ $(CLOPT) PoolItemListen.cpp

PoolItemClient.o:		PoolItemClient.cpp PoolItemClient.h server.h
			g++ $(CLOPT) PoolItemClient.cpp

server:		server.cpp server.h PoolItem.o Pool.o PoolItemListen.o PoolItemClient.o
			g++ -o server server.cpp PoolItem.o Pool.o PoolItemListen.o PoolItemClient.o $(LIBS)

cli:		cli.cpp
			g++ -o cli cli.cpp