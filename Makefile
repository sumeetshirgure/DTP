INC=include
LIB=lib
SRC=src

none :

server : Server.c dtp
	gcc -std=c99 -Iinclude Server.c -o server -Wl,-R,lib -Llib -ldtp -lpthread

client : Client.c dtp
	gcc -std=c99 -Iinclude Client.c -o client -Wl,-R,lib -Llib -ldtp -lpthread

dtp : libdtp.so

libdtp.so : $(LIB)/libgate.o $(LIB)/libbuffer.o
	gcc -shared -fPIC $^ -Wl,-soname,libdtp.so -o $(LIB)/libdtp.so

$(LIB)/libgate.o : $(SRC)/gate.c $(INC)/gate.h
	gcc -c -fPIC -I$(INC) $(SRC)/gate.c -o $(LIB)/libgate.o

$(LIB)/libbuffer.o : $(SRC)/buffer.c $(INC)/buffer.h
	gcc -c -fPIC -I$(INC) $(SRC)/buffer.c -o $(LIB)/libbuffer.o

clean :
	rm -f lib/* server client
