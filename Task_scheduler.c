// Task Scheduler 
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define MAX_TASKS 16
#define TICK_MS 1

static volatile int scheduler_lock = 0;

static inline void lock_scheduler() {
	while (__sync_lock_test_and_set(&scheduler_lock, 1)) {
	}
}

static inline void unlock_scheduler() {
	__sync_lock_release(&scheduler_lock);
}





typedef enum {
	TASK_EMPTY = 0, // Empty place 
	TASK_READY,     // Ready state
	TASK_RUNNING,   // Running state
	TASK_WAITING    // Waiting state
} task_state;

typedef struct {
	void (*function)(void);  // The code of function
	uint32_t period;         // The function need to run each period time
	uint32_t execution_time; // Max time for running
	uint32_t last_run;       // When the function run last time
	uint32_t next_run;       // When the function need to be run again
	uint8_t priority;
	bool is_sporadic;
	task_state state;
} Task;

static Task tasks[MAX_TASKS];   // Static array for 16 missions
static uint8_t task_count = 0;  // How much tasks
static uint32_t current_time = 0; // Main timer
static volatile uint32_t watchdog_timer = 0;  

void scheduler_init() {

	memset(tasks, 0, sizeof(tasks));

	for (int i = 0; i < MAX_TASKS; i++) {
		tasks[i].state = TASK_EMPTY;
	}
	
	task_count = 0;
	current_time = 0;
	watchdog_timer = 0;
}


bool add_task(void (*task_function)(void), uint32_t period, uint32_t execution_time, uint8_t priority) {

	if (task_function == NULL) return false;
	lock_scheduler();

	if (task_count == MAX_TASKS) {
		unlock_scheduler();
		return false; // If the array tasks is full return false
	}

	int empty_index=-1;
	for (int i = 0; i < MAX_TASKS; i++) {
		if (tasks[i].state != TASK_EMPTY && tasks[i].priority == priority) {
			unlock_scheduler();
			return false;
		}
		if (tasks[i].state == TASK_EMPTY && empty_index == -1) {
			empty_index = i;
		}
	}
	if (empty_index == -1) {
		unlock_scheduler();
		return false;
	}
	tasks[empty_index].function = task_function;
	tasks[empty_index].period = period;
	tasks[empty_index].execution_time = execution_time;
	tasks[empty_index].priority = priority;
	tasks[empty_index].last_run = 0;

	if (period == 0) {
		tasks[empty_index].is_sporadic = true;
		tasks[empty_index].state = TASK_WAITING;
		tasks[empty_index].next_run = 0;
	}
	else {
		tasks[empty_index].is_sporadic = false;
		tasks[empty_index].state = TASK_READY;
		tasks[empty_index].next_run = current_time;  //ready now
	}

	task_count++;
	unlock_scheduler();
	return true;
}


bool remove_task(void (*task_function)(void)) {

	if (task_function == NULL || task_count==0) return false;
	lock_scheduler();
	for (int i = 0; i < MAX_TASKS; i++) {
		if (tasks[i].state != TASK_EMPTY && tasks[i].function == task_function) {
			tasks[i].state = TASK_EMPTY;
			tasks[i].function = NULL;
			task_count--;
			unlock_scheduler();
			return true;
		}
	}
	unlock_scheduler();
	return false;
}


void scheduler_tick() {

	lock_scheduler();
	current_time++;
	watchdog_timer++;
	for (int i = 0; i < MAX_TASKS; i++) {
		if (tasks[i].state == TASK_EMPTY) continue;

		if (tasks[i].state == TASK_RUNNING) {
			if (watchdog_timer > tasks[i].execution_time) {
				printf("Watchdog Error: Task %d exceeded max execution time\n", i); // Print outside is better because performance
			}
		}

		if (tasks[i].is_sporadic == false) {
			if (current_time >= tasks[i].next_run) {
				if (tasks[i].state == TASK_RUNNING || tasks[i].state == TASK_READY) {
					printf("Deadline miss : the task didnt run yet in the last period\n");
				}
				else {
					tasks[i].state = TASK_READY;  // Everything ok now put this task ready
				}

				tasks[i].next_run += tasks[i].period;
			}
			
		}	
	}
	unlock_scheduler();
}




void run_scheduler() {

	while (1) {


		int max = -1, max_index = -1;
		lock_scheduler();
		for (int i = 0; i < MAX_TASKS; i++) {
			if (tasks[i].state == TASK_READY) {
				if (tasks[i].priority > max) {
					max = tasks[i].priority;
					max_index = i;
				}
			}
		}
		if (max != -1) {   // Task found
			tasks[max_index].state = TASK_RUNNING;
			watchdog_timer = 0;
			unlock_scheduler();
			tasks[max_index].last_run = current_time;
			tasks[max_index].function();
			lock_scheduler();
			tasks[max_index].state = TASK_WAITING;
			unlock_scheduler();
		}
		else {
			unlock_scheduler();
		}
	}
}

bool trigger_sporadic_task(void (*task_function)(void)) {
	if (task_function == NULL) return false;

	lock_scheduler();
	for (int i = 0; i < MAX_TASKS; i++) {
		if (tasks[i].state != TASK_EMPTY && tasks[i].is_sporadic && tasks[i].function == task_function) {
			tasks[i].state = TASK_READY; 
			unlock_scheduler();
			return true;
		}
	}
	unlock_scheduler();
	return false; 
}
