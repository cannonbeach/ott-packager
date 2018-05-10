CC=gcc
CFLAGS=-g -c -O0 -m64 -Wall -Wfatal-errors -fomit-frame-pointer -funroll-loops
SRC=./source
INC=-I./include
OBJS=crc.o tsdecode.o fgetopt.o mempool.o dataqueue.o udpsource.o tsreceive.o hlsmux.o mp4core.o background.o cJSON.o cJSON_Utils.o
LIB=libfillet.a

all: $(LIB) fillet

fillet: fillet.o $(OBJS)
	$(CC) fillet.o $(OBJS) -L./ -lm -lpthread -o fillet

$(LIB): $(OBJS)
	ar rcs $(LIB) $(OBJS)
	@echo finishing building lib

fillet.o: $(SRC)/fillet.c
	$(CC) $(CFLAGS) $(INC) $(SRC)/fillet.c

background.o: $(SRC)/background.c
	$(CC) $(CFLAGS) $(INC) $(SRC)/background.c

tsreceive.o: $(SRC)/tsreceive.c
	$(CC) $(CFLAGS) $(INC) $(SRC)/tsreceive.c

udpsource.o: $(SRC)/udpsource.c
	$(CC) $(CFLAGS) $(INC) $(SRC)/udpsource.c

dataqueue.o: $(SRC)/dataqueue.c
	$(CC) $(CFLAGS) $(INC) $(SRC)/dataqueue.c

mempool.o: $(SRC)/mempool.c
	$(CC) $(CFLAGS) $(INC) $(SRC)/mempool.c

mp4core.o: $(SRC)/mp4core.c
	$(CC) $(CFLAGS) $(INC) $(SRC)/mp4core.c

hlsmux.o: $(SRC)/hlsmux.c
	$(CC) $(CFLAGS) $(INC) $(SRC)/hlsmux.c

fgetopt.o: $(SRC)/fgetopt.c
	$(CC) $(CFLAGS) $(INC) $(SRC)/fgetopt.c

tsdecode.o: $(SRC)/tsdecode.c
	$(CC) $(CFLAGS) $(INC) $(SRC)/tsdecode.c

crc.o: $(SRC)/crc.c
	$(CC) $(CFLAGS) $(INC) $(SRC)/crc.c

cJSON.o: $(SRC)/cJSON.c
	$(CC) $(CFLAGS) $(INC) $(SRC)/cJSON.c

cJSON_Utils.o: $(SRC)/cJSON_Utils.c
	$(CC) $(CFLAGS) $(INC) $(SRC)/cJSON_Utils.c

clean:
	rm -rf *o fillet
