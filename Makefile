INC=include
LIB=lib
SRC=src

none :

server : Server.c dtp
	gcc -Wall -std=c99 -Iinclude Server.c -o server -Wl,-R,lib -Llib -ldtp -lpthread

client : Client.c dtp
	gcc -Wall -std=c99 -Iinclude Client.c -o client -Wl,-R,lib -Llib -ldtp -lpthread

dtp : $(LIB)/libdtp.so

$(LIB)/libdtp.so : $(LIB)/libgate.o $(LIB)/libdmn.o $(LIB)/libconn.o $(LIB)/libpacket.o
	gcc -Wall -shared -fPIC $^ -Wl,-soname,libdtp.so -o $@

$(LIB)/libgate.o : $(SRC)/gate.c $(INC)/gate.h $(INC)/packet.h
	gcc -Wall -c -fPIC -I$(INC) $< -o $@

$(LIB)/libdmn.o : $(SRC)/daemons.c $(INC)/gate.h $(INC)/packet.h
	gcc -Wall -c -fPIC -I$(INC) $< -o $@

$(LIB)/libconn.o : $(SRC)/connect.c $(INC)/gate.h $(INC)/packet.h
	gcc -Wall -c -fPIC -I$(INC) $< -o $@

$(LIB)/libpacket.o : $(INC)/packet.h $(SRC)/packet.c
	gcc -Wall -c -fPIC -I$(INC) $(SRC)/packet.c -o $@

clean :
	rm -f lib/* server client
