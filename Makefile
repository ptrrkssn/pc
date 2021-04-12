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
	-rm -fr *.o pc \#* *~ core t

push:	clean
	git add -A && git commit -a && git push

tests:	test-setup test-1 # test-2 test-3 test-4 test-5

test-setup:
	rm -fr t && mkdir t && cd t && mkdir -p a/a-d1 a/a-d2 a/a-d1/a-d1-d4 b b/b-d1; touch a/a-f1 a/a-f2 a/a-d2/a-d2-f1 a/a-d1/a-d1-d4/a-d1-d4-f1 b/b-f1

test-1: pc
	@(echo "Test 1 ------------------------" ; cd t && ../pc -vd a/ b && ls -lR b)

test-2: pc
	@(echo "Test 2 ------------------------" ; cd t && ../pc -vr a/ b && ls -lR b)

test-3: pc
	@(echo "Test 3 ------------------------" ; cd t && ../pc -va a/ b && ls -lR b)

test-4: pc
	@(echo "Test 4 ------------------------" ; cd t && ../pc -vM a/ b && ls -lR b)

test-5: pc
	@(echo "Test 5 ------------------------" ; cd t && ../pc -fvM a/ b && ls -lR b)

