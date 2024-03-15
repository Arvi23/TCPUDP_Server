
server: server.c
	gcc server.c -o server


subscriber: client.c
	gcc client.c -o subscriber

clean:
	rm -f server subscriber