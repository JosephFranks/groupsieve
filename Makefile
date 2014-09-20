COMPILER = gcc
CCFLAGS = -O

all: groupsieve

debug:
	make DEBUG=TRUE

ifeq ($(DEBUG), TRUE)
 CCFLAGS += -g
endif

groupsieve: groupsieve.c groupsieve.h
	$(COMPILER) $(CCFLAGS) -pthread -o groupsieve groupsieve.c groupsieve.h -L./usr/include/math.h -lm

clean:
	rm -f groupsieve
