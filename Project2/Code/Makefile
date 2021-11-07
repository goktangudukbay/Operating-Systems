

all: schedule 

schedule: schedule.c
	gcc schedule.c -lm -std=c99 -D _DEFAULT_SOURCE -lpthread -o schedule

clean:
	rm -fr schedule *dSYM
