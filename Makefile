CFLAGS = -std=gnu99 -Wall -Wextra -Werror -pedantic -g

.PHONY: all clean pack

all: proj2
	
proj2: proj2.c
	gcc $(CFLAGS) proj2.c -o $@ -pthread -lrt

pack: proj2.zip

proj2.zip: proj2.c Makefile
	zip $@ $^

clean:
	rm -f proj2
	rm -f proj2.out
	rm -f *.zip
