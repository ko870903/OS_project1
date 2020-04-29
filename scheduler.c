#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <signal.h>
#include <errno.h>

#define UNIT_TIME 1000000UL
#define TIME_QUANTUM 500

#define MAX_POLICY_NAME 16
#define MAX_PROCESS_NUM 16
#define MAX_PROCESS_NAME 32

#define PRIORITY_1 90
#define PRIORITY_2 50
#define PRIORITY_3 10

int ready_ptr = 0, run_ptr = 0, running = 0;
int t, count, timer, hold;

// merge_sort
int tmp[MAX_PROCESS_NUM], sjf_index[MAX_PROCESS_NUM], quene_index[MAX_PROCESS_NUM];
void merge_sort(int start, int end, int index[], int compare[]) {
	if (start >= end) return;

	int middle = (start + end) / 2;
	merge_sort(start, middle, index, compare);
	merge_sort(middle + 1, end, index, compare);
	
	int f_ptr = start, b_ptr = middle + 1, ptr = 0;
	while (f_ptr <= middle || b_ptr <= end) {
	if (f_ptr <= middle && b_ptr <= end) 
		if (compare[index[f_ptr]] <= compare[index[b_ptr]]) tmp[ptr++] = index[f_ptr++]; 
		else tmp[ptr++] = index[b_ptr++];
	else if (f_ptr <= middle) 
		tmp[ptr++] = index[f_ptr++];
	else if (b_ptr <= end) 
		tmp[ptr++] = index[b_ptr++];
	}
	for (int i = 0; i < ptr; i++) index[start + i] = tmp[i];
}

// basic function
void SET_CPU(pid_t pid, int x) {
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(x, &mask);
	sched_setaffinity(pid, sizeof(&mask), &mask);
}

void SET_PRIORITY(pid_t pid, int policy, int priority) {
	struct sched_param sp;
	sp.sched_priority = priority;
	sched_setscheduler(pid, policy, &sp);
}

void unit_of_time() {
	volatile unsigned long i;
	for (i = 0; i < UNIT_TIME; i++);
}

void run_process(char *name, int index, int exc_time) {
	pid_t my_pid = getpid(); 	
	printf("%s %d\n", name, my_pid);

	struct timespec start, end;
	syscall(228, CLOCK_REALTIME, &start);
	for (int i = 0; i < exc_time; i++) unit_of_time();
	syscall(228, CLOCK_REALTIME, &end);
	syscall(333, "[Project1] %d %ld.%.09ld %ld.%.09ld\n", my_pid, start.tv_sec, start.tv_nsec, end.tv_sec, end.tv_nsec);
	exit(0);	
}

void create_process(pid_t *pid, char *name, int index, int exc_time) {
	(*pid) = fork();
	if ((*pid) == 0) run_process(name, index, exc_time);
	if ((*pid) > 0) {
		SET_PRIORITY((*pid), SCHED_FIFO, PRIORITY_3);
		SET_CPU((*pid), 1);
	}
}

// FIFO
void FIFO_child(int signum) {
	wait(NULL);
	count++;
	running = 0;
	run_ptr++;
}

void change_FIFO_priority(pid_t pid[], int process_index[]) {
	if (!running && run_ptr < ready_ptr) {
		SET_PRIORITY(pid[process_index[run_ptr]], SCHED_FIFO, PRIORITY_1);
		running = 1;
	}
	if (running && (run_ptr + 1) < ready_ptr) 
		SET_PRIORITY(pid[process_index[run_ptr + 1]], SCHED_FIFO, PRIORITY_2);
}

void FIFO(int n, char name[MAX_PROCESS_NUM][MAX_PROCESS_NAME], int ready_time[MAX_PROCESS_NUM], int exc_time[MAX_PROCESS_NUM]) {
	pid_t pid[MAX_PROCESS_NUM];
	int process_index[MAX_PROCESS_NUM];
	ready_ptr = 0, run_ptr = 0, running = 0;
	
	for (int i = 0; i < n; i++) process_index[i] = i;
	merge_sort(0, n - 1, process_index, ready_time);
	
	signal(SIGCHLD, FIFO_child);

	int ready_process = process_index[ready_ptr];

	for (t = 0, count = 0; count < n; t++) {
		change_FIFO_priority(pid, process_index);
		while (ready_ptr < n && t == ready_time[ready_process]) {
			create_process(&pid[ready_process], name[ready_process], ready_process, exc_time[ready_process]);
			ready_process = process_index[++ready_ptr];
			change_FIFO_priority(pid, process_index);
		}
		unit_of_time();
	}
}

// SJF
void SJF_child(int signum) {
	wait(NULL);
	count++;
	running = 0;
}

void change_SJF_priority(pid_t pid[], int exc_time[]) {
	if (!running && sjf_index[0] > 0) {
		SET_PRIORITY(pid[sjf_index[1]], SCHED_FIFO, PRIORITY_1);
		for (int i = 1; i < sjf_index[0]; i++) sjf_index[i] = sjf_index[i + 1];
		sjf_index[0] -= 1;
		running = 1;
	}
	if (running && sjf_index[0] > 0) 
		SET_PRIORITY(pid[sjf_index[1]], SCHED_FIFO, PRIORITY_2);
}

void SJF(int n, char name[MAX_PROCESS_NUM][MAX_PROCESS_NAME], int ready_time[MAX_PROCESS_NUM], int exc_time[MAX_PROCESS_NUM]) {
	pid_t pid[MAX_PROCESS_NUM];
	int process_index[MAX_PROCESS_NUM];
	ready_ptr = 0, running = 0;

	for (int i = 0; i < n; i++) process_index[i] = i;
	merge_sort(0, n - 1, process_index, exc_time);
	merge_sort(0, n - 1, process_index, ready_time);

	signal(SIGCHLD, SJF_child);
	
	int ready_process = process_index[ready_ptr];
	sjf_index[0] = 0;

	for (t = 0, count = 0; count < n; t++) {
		change_SJF_priority(pid, exc_time);
		while (ready_ptr < n && t == ready_time[ready_process]) {
			if (running && sjf_index[0] > 0) 
				SET_PRIORITY(pid[sjf_index[1]], SCHED_FIFO, PRIORITY_3);
			create_process(&pid[ready_process], name[ready_process], ready_process, exc_time[ready_process]);
			if (sjf_index[0] == 0) {
				sjf_index[++sjf_index[0]] = ready_process;
			} else {
				sjf_index[++sjf_index[0]] = ready_process;
				merge_sort(1, sjf_index[0], sjf_index, exc_time);
			} 
			ready_process = process_index[++ready_ptr];
			change_SJF_priority(pid, exc_time);
		}
		unit_of_time();
	}
}

// RR
void RR_child(int signum) {
	wait(NULL);
	count++;
	for (int i = 1; i < quene_index[0]; i++) quene_index[i] = quene_index[i + 1];
	quene_index[0]--;
	running = 0;
	timer = 0;
	hold = 0;
}

void change_RR_priority(pid_t pid[], int exc_time[]) {
	if (!running && quene_index[0] > 0) {
		SET_PRIORITY(pid[quene_index[1]], SCHED_FIFO, PRIORITY_1);
		timer = 0;
		running = 1;
		if (exc_time[quene_index[1]] <= TIME_QUANTUM) hold = 1;
	}
	if (running && quene_index[0] > 1) 
		SET_PRIORITY(pid[quene_index[2]], SCHED_FIFO, PRIORITY_2);
}

void RR(int n, char name[MAX_PROCESS_NUM][MAX_PROCESS_NAME], int ready_time[MAX_PROCESS_NUM], int exc_time[MAX_PROCESS_NUM]) {
	pid_t pid[MAX_PROCESS_NUM];
	int process_index[MAX_PROCESS_NUM];
	ready_ptr = 0, run_ptr = 0, running = 0, hold = 0;
	
	for (int i = 0; i < n; i++) process_index[i] = i;
	merge_sort(0, n - 1, process_index, ready_time);
	
	signal(SIGCHLD, RR_child);

	int ready_process = process_index[ready_ptr];
	quene_index[0] = 0;

	for (t = 0, count = 0; count < n; t++) {
		change_RR_priority(pid, exc_time);
		while (ready_ptr < n && t == ready_time[ready_process]) {
			create_process(&pid[ready_process], name[ready_process], ready_process, exc_time[ready_process]);
			quene_index[++quene_index[0]] = ready_process;
			ready_process = process_index[++ready_ptr];
			change_RR_priority(pid, exc_time);
		}
		unit_of_time(); timer++;
		if (!hold && running && timer == TIME_QUANTUM) {
			int index = quene_index[1];
			for (int i = 1; i < quene_index[0]; i++) quene_index[i] = quene_index[i + 1];
			SET_PRIORITY(pid[index], SCHED_FIFO, PRIORITY_3);
			exc_time[index] -= TIME_QUANTUM;
			quene_index[quene_index[0]] = index;			
			running = 0;
			change_RR_priority(pid, exc_time);
		}
	}
}

// PSJF
void PSJF_child(int signum) {
	wait(NULL);
	count++;
	for (int i = 1; i < sjf_index[0]; i++) sjf_index[i] = sjf_index[i + 1];
	sjf_index[0] -= 1;
	running = 0;
}

void change_PSJF_priority(pid_t pid[], int exc_time[]) {
	if (!running && sjf_index[0] > 0) {
		SET_PRIORITY(pid[sjf_index[1]], SCHED_FIFO, PRIORITY_1);
		running = 1;
	}
	if (running && sjf_index[0] > 1) 
		SET_PRIORITY(pid[sjf_index[2]], SCHED_FIFO, PRIORITY_2);
}

void PSJF(int n, char name[MAX_PROCESS_NUM][MAX_PROCESS_NAME], int ready_time[MAX_PROCESS_NUM], int exc_time[MAX_PROCESS_NUM]) {
	pid_t pid[MAX_PROCESS_NUM];
	int process_index[MAX_PROCESS_NUM];
	ready_ptr = 0, running = 0;

	for (int i = 0; i < n; i++) process_index[i] = i;
	merge_sort(0, n - 1, process_index, exc_time);
	merge_sort(0, n - 1, process_index, ready_time);

	signal(SIGCHLD, PSJF_child);
	
	int ready_process = process_index[ready_ptr];
	sjf_index[0] = 0;

	for (t = 0, count = 0; count < n; t++) {
		change_PSJF_priority(pid, exc_time);
		while (ready_ptr < n && t == ready_time[ready_process]) {
			if (running && sjf_index[0] > 1) 
				SET_PRIORITY(pid[sjf_index[2]], SCHED_FIFO, PRIORITY_3);
			create_process(&pid[ready_process], name[ready_process], ready_process, exc_time[ready_process]);
			if (sjf_index[0] == 0) {
				sjf_index[++sjf_index[0]] = ready_process;
			} else {
				sjf_index[++sjf_index[0]] = ready_process;
				int origin_index = sjf_index[1];
				merge_sort(1, sjf_index[0], sjf_index, exc_time);
				int new_index = sjf_index[1];
				if (origin_index != new_index) {
					SET_PRIORITY(pid[origin_index], SCHED_FIFO, PRIORITY_2);
					running = 0;				
				}
			} 
			ready_process = process_index[++ready_ptr];
			change_PSJF_priority(pid, exc_time);
		}
		unit_of_time();
		if (running) exc_time[sjf_index[1]]--;
	}
}

// main function
int main(int argc, char *argv[]) {
	pid_t my_pid = getpid();
	SET_CPU(my_pid, 0);
	SET_PRIORITY(my_pid, SCHED_FIFO, PRIORITY_2);

	int n;
	char policy[MAX_POLICY_NAME], name[MAX_PROCESS_NUM][MAX_PROCESS_NAME];
	int ready_time[MAX_PROCESS_NUM], exc_time[MAX_PROCESS_NUM];
	scanf("%s %d", policy, &n);
	for (int i = 0; i < n; i++) scanf("%s %d %d", name[i], &ready_time[i], &exc_time[i]);	
	if (!strcmp(policy, "FIFO")) FIFO(n, name, ready_time, exc_time);
	else if (!strcmp(policy, "RR")) RR(n, name, ready_time, exc_time);
	else if (!strcmp(policy, "SJF")) SJF(n, name, ready_time, exc_time);
	else if (!strcmp(policy, "PSJF")) PSJF(n, name, ready_time, exc_time);

	return 0;
}

