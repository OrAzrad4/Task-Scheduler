// Task Scheduler 
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>


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
	volatile uint32_t watchdog_timer;
} Task;

static Task tasks[MAX_TASKS];   // Static array for 16 missions
static uint8_t task_count = 0;  // How much tasks
static volatile uint32_t current_time = 0; // Main timer

void scheduler_init() {

	memset(tasks, 0, sizeof(tasks));

	for (int i = 0; i < MAX_TASKS; i++) {
		tasks[i].state = TASK_EMPTY;
	}
	
	task_count = 0;
	current_time = 0;
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
		if (tasks[i].state != TASK_EMPTY && tasks[i].priority == priority) {   // Invalid - two tasks with the same priority
			unlock_scheduler();
			return false;
		}
		if (tasks[i].state == TASK_EMPTY && empty_index == -1) {   // First empty place
			empty_index = i;
		}
	}
	if (empty_index == -1) {                     // The array is full of tasks
		unlock_scheduler();
		return false;
	}
	tasks[empty_index].function = task_function;
	tasks[empty_index].period = period;
	tasks[empty_index].execution_time = execution_time;
	tasks[empty_index].priority = priority;
	tasks[empty_index].last_run = 0;

	if (period == 0) {                         // Sign for sporadic task
		tasks[empty_index].is_sporadic = true;
		tasks[empty_index].state = TASK_WAITING;
		tasks[empty_index].next_run = 0;
	}
	else {
		tasks[empty_index].is_sporadic = false;
		tasks[empty_index].state = TASK_READY;
		tasks[empty_index].next_run = current_time;  //ready now for running (we can decide to wait few ms)
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
			if (tasks[i].state == TASK_RUNNING) { // Cant remove running task
				unlock_scheduler(); 
				return false;       
			}
			tasks[i].state = TASK_EMPTY;
			tasks[i].function = NULL;
			task_count--;
			unlock_scheduler();
			return true;
		}
	}
	unlock_scheduler();
	return false;                       // we didnt found the the input task
}

// This function probably call from ISR 
void scheduler_tick() {

	lock_scheduler();
	current_time++;                             // Each 1 ms we need to add 1 to the main timer and the watchdog timer
	for (int i = 0; i < MAX_TASKS; i++) {
		if (tasks[i].state == TASK_EMPTY) continue;



		if (tasks[i].state == TASK_RUNNING) { 
			tasks[i].watchdog_timer++;

			if (tasks[i].watchdog_timer > tasks[i].execution_time) {     // The task run more time then she need
				printf("Watchdog Error: Task %d exceeded max execution time\n", i); // Print outside is better because performance (can change it to flag)
				// To do: stop this function 
			}
		}

		if (tasks[i].is_sporadic == false) {            // Sporadic tasks dont have deadline 
			if (current_time >= tasks[i].next_run) {   // the task need to be waiting now
				if (tasks[i].state == TASK_RUNNING || tasks[i].state == TASK_READY) {    // The task is still in the last period 
					printf("Deadline miss : the task didnt run yet in the last period\n");
				}
				else {
					tasks[i].state = TASK_READY;  // Everything ok (waiting state) now put this task ready
				}

				tasks[i].next_run += tasks[i].period; // If the task is running so current_time < next_run or deadline
			}
			
		}	
	}
	unlock_scheduler();
}




void run_scheduler() {

	while (1) {


		int best_priority = INT_MAX, max_index = -1;    // Define best priority such that the lowest number -> best priority
		lock_scheduler();
		for (int i = 0; i < MAX_TASKS; i++) {
			if (tasks[i].state == TASK_READY) {
				if (tasks[i].priority < best_priority) {
					best_priority = tasks[i].priority;
					max_index = i;
				}
			}
		}
		if (max_index != -1) {   // Task found
			tasks[max_index].state = TASK_RUNNING;
			tasks[max_index].watchdog_timer = 0;                    // ready for new task
			tasks[max_index].last_run = current_time; // in this position time this task start to run
			unlock_scheduler();                     // open the lock for different cores to run in parallel
			tasks[max_index].function();
			lock_scheduler();                         // Prevent race condition in cases of different core run or tick reads status
			tasks[max_index].state = TASK_WAITING;
			unlock_scheduler();
		}
		else {               // No ready task found
			unlock_scheduler();
			__asm__("wfi");   // Low power mode - wait for interrupt
		}
		
	}
}

// This function probably call from ISR 
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
