# Caio Eloi
# Makefile para o Trabalho 1 de Redes — Jogo de Senha (Mastermind)

CXX      = g++
CXXFLAGS = -std=c++11 -Wall -O2

all: server client

server: server.cpp protocol.h
	$(CXX) $(CXXFLAGS) -o server server.cpp

client: client.cpp protocol.h
	$(CXX) $(CXXFLAGS) -o client client.cpp

run_serv: server
	./server $(arg1) $(arg2) $(arg3)

run_cli: client
	./client $(arg1) $(arg2)

clean:
	rm -f server client
