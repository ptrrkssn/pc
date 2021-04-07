# Makefile

CC=gcc
CFLAGS=-O -g -Wall

OBJS=pc.o btree.o digest.o

pc: $(OBJS)
	$(CC) -o pc $(OBJS) -lmd -lz

digest.o: digest.c digest.h
btree.o: btree.c btree.h
pc.o: pc.c digest.h btree.h

clean:
	-rm -f *.o pc \#* *~ core
