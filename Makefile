CXX = g++
CXXFLAGS = -std=c++11 -Wall -O2

all: server client

server: server.cpp protocol.h
	$(CXX) $(CXXFLAGS) server.cpp -o server

client: client.cpp protocol.h
	$(CXX) $(CXXFLAGS) client.cpp -o client

run_serv: server
	./server $(arg1) $(arg2) $(arg3)

run_cli: client
	./client $(arg1) $(arg2)

clean:
	rm -f server client
