COMPILE_FLAGS = -std=gnu11 -g -Wall -Wextra
VALGRIND_FLAGS = -s --track-origins=yes --malloc-fill=0x40 --free-fill=0x23 --leak-check=full --show-leak-kinds=all 
BINARIES = upush_server upush_client
PORT = 4041

all: clean $(BINARIES)

srun: clean compile
	./upush_server $(PORT) 0

crun: clean compile
	./upush_client Bob 127.0.0.1 $(PORT) 5 5

clean:
	rm -f $(BINARIES) && clear && echo "Cleaned binaries and cleared terminal!"


#Compile
compile: clean $(BINARIES) 

upush_server: upush_server.c send_packet.c common.c
	gcc $(COMPILE_FLAGS) upush_server.c send_packet.c common.c -o upush_server

upush_client: upush_client.c send_packet.c common.c
	gcc $(COMPILE_FLAGS) upush_client.c send_packet.c common.c -o upush_client


#Valgrind
vcrun: clean compile
	valgrind $(VALGRIND_FLAGS) ./upush_client Bob 127.0.0.1 $(PORT) 5 20

vsrun: clean compile
	valgrind $(VALGRIND_FLAGS) ./upush_server $(PORT) 0
