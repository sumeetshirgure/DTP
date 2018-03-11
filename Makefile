INC=include
LIB=lib
SRC=src

none :

server : Server.c dtp
	gcc -std=c99 -Iinclude Server.c -o server -Wl,-R,lib -Llib -ldtp

client : Client.c dtp
	gcc -std=c99 -Iinclude Client.c -o client -Wl,-R,lib -Llib -ldtp

dtp : libdtp.so

libdtp.so : libgate.o
	gcc -shared -fPIC $(LIB)/libgate.o -Wl,-soname,libdtp.so -o $(LIB)/libdtp.so

libgate.o : $(SRC)/gate.c $(INC)/gate.h
	gcc -c -fPIC -I$(INC) $(SRC)/gate.c -o $(LIB)/libgate.o

clean :
	rm -f lib/* server client
