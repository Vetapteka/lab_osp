CFLAGS=-Wall -Wextra -Werror -O2
TARGETS= lab1saaN3251 libaerN3253.so libsaaN3251.so

.PHONY: all clean

all: $(TARGETS)

clean:
	rm -rf *.o $(TARGETS)

lab1saaN3251: main.c plugin_api.h
	gcc $(CFLAGS) -o lab1saaN3251 main.c -ldl

libsaaN3251.so: lib.c plugin_api.h
	gcc $(CFLAGS) -shared -fPIC -o libsaaN3251.so libsaaN3251.c -ldl 
	
libaerN3253.so: lib.c plugin_api.h
	gcc $(CFLAGS) -shared -fPIC -o libaerN3253.so libaerN3253.c -ldl
