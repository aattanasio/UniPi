# make rule primaria con dummy target ‘all’--> non crea alcun file all ma fa un complete build
# 											   che dipende dai target client e server scritti sotto
all: server cli td kd

server: server.c const.h
	gcc -Wall server.c -o server

cli: client.c
	gcc -Wall client.c -o cli

td: td.c
	gcc -Wall td.c -o td

kd: kd.c
	gcc -Wall kd.c -o kd

# fa funzionare il comando clean
# anche in presenza di un file chiamato 'clean'
.PHONY: clean 

clean:
	rm -f server client td kd
