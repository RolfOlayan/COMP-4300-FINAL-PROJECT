//-----------------------------------------
// NAME: Rolf Olayan
// STUDENT NUMBER: #7842749
// COURSE: COMP 4300
// Final Project - SJF versus MLFQ scheduling
//-----------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/wait.h>
#include <time.h>
#include <pthread.h>
#define BUFFER_SIZE 100
#define NANOS_PER_USEC 1000
#define USEC_PER_SEC 1000000
#define TIME_SLICE 50
#define MAX_ALLOTMENT 200
#define MAX_THREADS 4
#define HIGH 1
#define MEDIUM 2
#define LOW 3
#define RESET_INTERVAL 5000
#define SJF_POLICY 123
#define MLFQ_POLICY 321

typedef struct NODE {
	char* task_name;
	int task_type;
	int task_length;
	int time_allotment;
	long response_time;
	long turnaround_time;
    struct Node* next;
} Node;

typedef struct QUEUE {
    struct Node* head;
    struct Node* tail;
} Queue;

void user_prompts();
void load_tasks(FILE *task_file);
static void* task_driver();
void simulate();
void handle_task(Node* task);
void mlfq_reset_priority();
void mlfq_next_task(Node* processed_task);
void log_response_time(Node* task);
void log_turnaround_time(Node* task);
void print_statistics();
void initialize_queue(int priority, Node* task);
void insert_at_end(int priority, Node* task);
int not_empty(int priority);
int is_first_task(int priority, Node* task);
static void microsleep(unsigned int usecs);
struct timespec diff(struct timespec start, struct timespec end);

// Time variables
long type0_turnaround_time;
long type1_turnaround_time;
long type2_turnaround_time;
long type0_response_time;
long type1_response_time;
long type2_response_time;

// Used when calculating the time averages
int type0_count;
int type1_count;
int type2_count;

// Used to hold the tasks
Node* curr_task;
Node* sjf_list;
Queue* high_priority;
Queue* med_priority;
Queue* low_priority;

// Lock and conditional variable
pthread_cond_t need_work = PTHREAD_COND_INITIALIZER;
pthread_mutex_t main_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// Thread/algorithm variables
int active_threads;
int scheduler_on;
int policy_mode;
int reset_priority = RESET_INTERVAL;
struct timespec arrival;

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	FILE *task_file = fopen("tasks.txt", "r");
	assert(task_file != NULL);
	
	if (task_file != NULL)
	{
		// Initializes the lists and queues
		sjf_list = NULL;		
		high_priority = malloc(sizeof(Queue));
		med_priority = malloc(sizeof(Queue));
		low_priority = malloc(sizeof(Queue));
		initialize_queue(HIGH, NULL);
		initialize_queue(MEDIUM, NULL);
		initialize_queue(LOW, NULL);
		
		// Prompts the user for their requested scheduling policy
		user_prompts();

		// Gets the arrival (start) time of the program
		// Used for calculating the response time
		clock_gettime(CLOCK_REALTIME, &arrival);
		system("clear");
		
		// Loads in the tasks from the file and conducts the simulation
		load_tasks(task_file);		
		simulate();

		// Prints the statistics at the end and closes the file
		print_statistics();
		fclose(task_file);
	}
	
	printf("\nProgram completed successfully.\nProgrammed by Rolf Olayan.\n\n");
    return EXIT_SUCCESS;
}

// Used to prompt user for their desired simulation settings
void user_prompts()
{
	char user_input;

	while (1)
	{
		system("clear");
		printf("Please specify which scheduling algorithm you want to simulate with:\n");
		printf("> 1. SJF\n> 2. MLFQ\n");
		user_input = getchar();
		if (user_input == '1' || user_input == '2')
		{
			if (user_input == '1')
			{
				policy_mode = SJF_POLICY;
			}
			else if (user_input == '2')
			{
				policy_mode = MLFQ_POLICY;
			}

			break;
		}
	}
}

// Loads all the tasks into the appropriate lists or queues, depending on the scheduling policy
void load_tasks(FILE *task_file)
{
	char buffer[BUFFER_SIZE];
	char delim[] = " ";
	
	while (fgets(buffer, BUFFER_SIZE, task_file))
	{
		Node* new_task = malloc(sizeof(Node));
		Node* curr = sjf_list;
		Node* next = NULL;
		
		char* token = strtok(buffer, delim);
		new_task->task_name = malloc(sizeof(token));
		strcpy(new_task->task_name, token);
		
		token = strtok(NULL, delim);
		new_task->task_type = atoi(token);
		
		// Counts the number of each task type (small/medium/long).
		// Used when calculating the average times of each type.
		if (new_task->task_type == 0)
		{
			type0_count++;
		}
		else if (new_task->task_type == 1)
		{
			type1_count++;
		}
		else
		{
			type2_count++;
		}
		
		token = strtok(NULL, delim);
		new_task->task_length = atoi(token);
		new_task->response_time = -1;
		new_task->time_allotment = MAX_ALLOTMENT;
		
		if (policy_mode == SJF_POLICY)
		{
			if (curr == NULL) //First task
			{
				sjf_list = new_task;
			}
			else //Loops through the list until the appropriate spot is found
			{
				next = (Node*)curr->next;
				
				while (next != NULL && new_task->task_length > next->task_length)
				{
					curr = next;
					next = (Node*)curr->next;
				}
				
				if (new_task->task_length < curr->task_length && strcmp(curr->task_name, sjf_list->task_name) == 0)
				{
					new_task->next = (struct Node*)curr;
					sjf_list = new_task;
				}
				else
				{
					curr->next = (struct Node*)new_task;
					new_task->next = (struct Node*)next;
				}
			}
		}
		else if (policy_mode == MLFQ_POLICY)
		{		
			insert_at_end(HIGH, new_task);
		}
	}
}

// Used for the scheduling and processing of tasks
//     - For the scheduler:
//         - Signals the worker threads when there are available threads left
//     - For the workers:
//         - Logs the response and turnaround times
//         - Handles the task
//         - Updates the reference of curr_task for the task to be handled
static void* task_driver()
{
	struct timespec completion, first_run;
	
	if (scheduler_on && active_threads < MAX_THREADS) //Handles the scheduling/signalling
	{
		pthread_mutex_lock(&main_lock);
		pthread_cond_signal(&need_work);
		pthread_mutex_unlock(&main_lock);
	}
	else //Handles the task
	{
		while (1)
		{
			pthread_mutex_lock(&lock);
			
			// Loops until there's no tasks left
			if (curr_task == NULL)
			{
				break;
			}

			// Waits for the scheduler to signal when there are free threads available
			while (active_threads >= MAX_THREADS)
			{
				pthread_cond_wait(&need_work, &lock);
			}
			
			Node* process_task = curr_task;
			pthread_mutex_unlock(&lock);
			
			if (process_task != NULL)
			{
				pthread_mutex_lock(&lock);
				active_threads++;
				
				//Logs the response time
				if (process_task->response_time == -1)
				{
					clock_gettime(CLOCK_REALTIME, &first_run);
					process_task->response_time = diff(arrival, first_run).tv_nsec/1000;
					log_response_time(process_task);
				}
				
				// Sends to task handler 
				handle_task(process_task);
				
				//For logging the turnaround time
				clock_gettime(CLOCK_REALTIME, &completion);
				process_task->turnaround_time += diff(first_run, completion).tv_nsec/1000;
				
				if (policy_mode == SJF_POLICY)
				{
					// Logs the turnaround time and gets the next task
					log_turnaround_time(process_task);
					curr_task = (Node*)curr_task->next;
				}
				else if (policy_mode == MLFQ_POLICY)
				{
					// Logs the turnaround time if the task is finished
					if (process_task->task_length <= 0)
					{
						log_turnaround_time(process_task);
					}

					// Resets all tasks back to the highest priority after the set amount of time
					if (reset_priority <= 0)
					{
						reset_priority = RESET_INTERVAL;
						mlfq_reset_priority();
					}
					else // Else gets the next highest priority task
					{
						mlfq_next_task(process_task);
					}
				}
				
				active_threads--;
				pthread_mutex_unlock(&lock);
			}
		}
	}
 
    return NULL;
}

// Simulates the appropriate scheduling policy (SJF or MLFQ)
void simulate()
{														   
	pthread_t workers[MAX_THREADS];
	pthread_t scheduler;	
	
	// Gets the first task depending on the scheduling policy
	if (policy_mode == SJF_POLICY)
	{
		printf("\nSimulating SJF with %d CPU(s)\n", MAX_THREADS);
		curr_task = sjf_list;
	}
	else if (policy_mode == MLFQ_POLICY)
	{
		printf("\nSimulating MLFQ with %d CPU(s)\n", MAX_THREADS);
		curr_task = (Node*)high_priority->head;
	}
	
	// Creates the worker threads
	for (int i = 0; i < MAX_THREADS; i++)
	{
		pthread_create(&workers[i], NULL, task_driver, NULL);
	}
	
	// Creates and turns on the scheduler thread
	scheduler_on = 1;
	while (curr_task != NULL)
	{
		pthread_create(&scheduler, NULL, task_driver, NULL);
	}

	pthread_join(scheduler, NULL);
}

// Handles tasks
//     - Simulates the tasks by sleeping for the appropriate length of time
//         - For SJF, simply sleeps for the whole task length
//         - For MLFQ, sleeps for the quantum time slice
//             - Updates the remaining task length and time allotment (used for lowering task priority)
void handle_task(Node* task)
{
	if (policy_mode == SJF_POLICY)
	{
		microsleep(task->task_length);
	}
	else if (policy_mode == MLFQ_POLICY)
	{
		int remainder = task->task_length - TIME_SLICE;

		if (remainder > 0)
		{
			task->task_length -= TIME_SLICE;
			task->time_allotment -= TIME_SLICE;
			reset_priority -= TIME_SLICE;
			microsleep(TIME_SLICE);
		}
		else
		{
			task->task_length = 0;
			task->time_allotment = 0;
			reset_priority -= (TIME_SLICE + remainder);
			microsleep(TIME_SLICE + remainder);
		}
	}
}

// Handles the resetting of priority queues after the reset interval is reached (RESET_INTERVAL)
//     - Moves all tasks from the MEDIUM and LOW priority queues into the HIGH queue
void mlfq_reset_priority()
{
	Node* curr;
	Node* next;

	if (not_empty(MEDIUM))
	{
		curr = (Node*)med_priority->head;
		next = NULL;
		while (curr != NULL)
		{
			next = (Node*)curr->next;
			curr->time_allotment = MAX_ALLOTMENT;
			insert_at_end(HIGH, curr);
			curr = next;
		}
	}

	if (not_empty(LOW))
	{
		curr = (Node*)low_priority->head;
		next = NULL;
		while (curr != NULL)
		{
			next = (Node*)curr->next;
			curr->time_allotment = MAX_ALLOTMENT;
			insert_at_end(HIGH, curr);
			curr = next;
		}
	}

	initialize_queue(MEDIUM, NULL);
	initialize_queue(LOW, NULL);
	curr_task = (Node*)high_priority->head;
}

// Handles the acquirement of the next task
//     - Queues the processed task onto the end of the appropriate priority queue
//     - Updates the priority queue of the processed task
//     - Gets the next task from the highest non-empty priority queue
void mlfq_next_task(Node* processed_task)
{
	if (not_empty(HIGH))
	{
		if (is_first_task(HIGH, processed_task))
		{
			high_priority->head = (struct Node*)processed_task->next;

			if (processed_task->task_length > 0)
			{
				if (processed_task->time_allotment > 0)
				{
					insert_at_end(HIGH, processed_task);
				}
				else
				{
					processed_task->time_allotment = MAX_ALLOTMENT;
					insert_at_end(MEDIUM, processed_task);
				}
			}

			if (not_empty(HIGH))
			{
				curr_task = (Node*)high_priority->head;
			}
			else
			{
				initialize_queue(HIGH, NULL);
				curr_task = (Node*)med_priority->head;
			}
		}
		else
		{
			curr_task = (Node*)high_priority->head;
		}
	}
	else if (not_empty(MEDIUM))
	{
		if (is_first_task(MEDIUM, processed_task))
		{
			med_priority->head = (struct Node*)processed_task->next;

			if (processed_task->task_length > 0)
			{
				if (processed_task->time_allotment > 0)
				{
					insert_at_end(MEDIUM, processed_task);
				}
				else
				{
					processed_task->time_allotment = MAX_ALLOTMENT;
					insert_at_end(LOW, processed_task);
				}
			}
			
			if (not_empty(MEDIUM))
			{
				curr_task = (Node*)med_priority->head;
			}
			else
			{
				initialize_queue(MEDIUM, NULL);
				curr_task = (Node*)low_priority->head;
			}
		}
		else
		{
			curr_task = (Node*)med_priority->head;
		}
	}
	else if (not_empty(LOW))
	{
		if (is_first_task(LOW, processed_task))
		{
			low_priority->head = (struct Node*)processed_task->next;

			if (processed_task->task_length > 0)
			{
				if (processed_task->time_allotment <= 0)
				{
					processed_task->time_allotment = MAX_ALLOTMENT;
				}								
				insert_at_end(LOW, processed_task);
			}

			if (not_empty(LOW))
			{
				curr_task = (Node*)low_priority->head;
			}
			else
			{				
				initialize_queue(LOW, NULL);
				curr_task = NULL;
			}
		}
		else
		{
			curr_task = (Node*)low_priority->head;
		}
	}
	else
	{
		curr_task = NULL;
	}
}

// Stores the response times into the appropriate variables
void log_response_time(Node* task)
{
	if (task->task_type == 0)
	{
		type0_response_time += task->response_time;
	}
	else if (task->task_type == 1)
	{
		type1_response_time += task->response_time;
	}
	else if (task->task_type == 2)
	{
		type2_response_time += task->response_time;
	}
}

// Stores the turnaround times into the appropriate variables
void log_turnaround_time(Node* task)
{
	if (task->task_type == 0)
	{
		type0_turnaround_time += task->turnaround_time;
	}
	else if (task->task_type == 1)
	{
		type1_turnaround_time += task->turnaround_time;
	}
	else if (task->task_type == 2)
	{
		type2_turnaround_time += task->turnaround_time;
	}
}

// Prints out the average turnaround and average response times to the console
void print_statistics()
{
	printf("\nAverage turnaround time per type:\n\n");
	printf("  - Type 0 (Short):\t%ld usec\n", type0_turnaround_time/type0_count);
	printf("  - Type 1 (Medium):\t%ld usec\n", type1_turnaround_time/type1_count);
	printf("  - Type 2 (Long):\t%ld usec\n", type2_turnaround_time/type2_count);
	
	printf("\nAverage response time per type:\n\n");
	printf("  - Type 0 (Short):\t%ld usec\n", type0_response_time/type0_count);
	printf("  - Type 1 (Medium):\t%ld usec\n", type1_response_time/type1_count);
	printf("  - Type 2 (Long):\t%ld usec\n", type2_response_time/type2_count);
}

// Used to initialize a priority queue
void initialize_queue(int priority, Node* task)
{	
	if (priority == HIGH)
	{
		high_priority->head = (struct Node*)task;
		high_priority->tail = (struct Node*)task;
	}
	else if (priority == MEDIUM)
	{
		med_priority->head = (struct Node*)task;
		med_priority->tail = (struct Node*)task;
	}
	else if (priority == LOW)
	{
		low_priority->head = (struct Node*)task;
		low_priority->tail = (struct Node*)task;
	}	
}

// Used to insert a task at the end of a priority queue
void insert_at_end(int priority, Node* task)
{
	Node* end = NULL;
	task->next = NULL;
	
	if (priority == HIGH)
	{
		if (not_empty(HIGH))
		{
			end = (Node*)high_priority->tail;
			end->next = (struct Node*)task;
			high_priority->tail = (struct Node*)task;
		}
		else
		{
			initialize_queue(priority, task);
		}
	}
	else if (priority == MEDIUM)
	{
		if (not_empty(MEDIUM))
		{
			end = (Node*)med_priority->tail;
			end->next = (struct Node*)task;
			med_priority->tail = (struct Node*)task;
		}
		else
		{
			initialize_queue(priority, task);
		}
	}
	else if (priority == LOW)
	{
		if (not_empty(LOW))
		{
			end = (Node*)low_priority->tail;
			end->next = (struct Node*)task;
			low_priority->tail = (struct Node*)task;
		}
		else
		{
			initialize_queue(priority, task);
		}
	}	
}

// Used to check whether a certain priority queue is empty or not
int not_empty(int priority)
{
	int ret_val = 0;

	if (priority == HIGH && (Node*)high_priority->head != NULL)
	{
		ret_val = 1;
	}
	else if (priority == MEDIUM && (Node*)med_priority->head != NULL)
	{
		ret_val = 1;
	}
	else if (priority == LOW && (Node*)low_priority->head != NULL)
	{
		ret_val = 1;
	}

	return ret_val;
}

// Used to check whether a task is at the top of a certain priority queue
int is_first_task(int priority, Node* task)
{
	Node* head = NULL;
	int ret_val = 0;

	if (priority == HIGH)
	{
		if (not_empty(HIGH))
		{
			head = (Node*)high_priority->head;
			ret_val = strcmp(head->task_name, task->task_name) == 0;
		}
	}
	else if (priority == MEDIUM)
	{
		if (not_empty(MEDIUM))
		{
			head = (Node*)med_priority->head;
			ret_val = strcmp(head->task_name, task->task_name) == 0;
		}
	}
	else if (priority == LOW)
	{
		if (not_empty(LOW))
		{
			head = (Node*)low_priority->head;
			ret_val = strcmp(head->task_name, task->task_name) == 0;
		}
	}	

	return ret_val;
}

// Used when "running tasks"
// Implementation of microsleep adapted from Franklin Bristow lectures
static void microsleep(unsigned int usecs)
{
    long seconds = usecs / USEC_PER_SEC;
    long nanos = (usecs % USEC_PER_SEC) * NANOS_PER_USEC;
    struct timespec t = {.tv_sec = seconds, .tv_nsec = nanos};
    int ret;
	
    do
    {
        ret = nanosleep( &t, &t );
        // need to loop, `nanosleep` might return before sleeping
        // for the complete time (see `man nanosleep` for details)
    } while (ret == -1 && (t.tv_sec || t.tv_nsec));
}

// Used to calculate difference between start and end time (from "Profiling Code Using clock_gettime by Guy Rutenberg")
// Source: https://www.guyrutenberg.com/2007/09/22/profiling-code-using-clock_gettime/
struct timespec diff(struct timespec start, struct timespec end)
{
	struct timespec temp;
	if ((end.tv_nsec-start.tv_nsec)<0) {
		temp.tv_sec = end.tv_sec-start.tv_sec-1;
		temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec-start.tv_sec;
		temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	return temp;
}
