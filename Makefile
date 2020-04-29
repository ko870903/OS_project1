all: scheduler

scheduler:
	gcc scheduler.c -o scheduler

clean:
	rm scheduler
