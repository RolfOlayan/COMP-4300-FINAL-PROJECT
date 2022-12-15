//-----------------------------------------
// NAME: Rolf Olayan
// STUDENT NUMBER: #7842749
// COURSE: COMP 4300
// Final Project
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
void load_tasks_sjf(FILE *task_file);
void load_tasks_mlfq(FILE *task_file);
static void* task_driver();
void simulate_sjf();
void simulate_mlfq();
void handle_task(Node* task);
void mlfq_reset_priority();
void mlfq_next_job(Node* processed_task);
void log_response_time(Node* task);
void log_turnaround_time(Node* task);
void print_statistics();
void initialize_queue(int priority, Node* node);
void insert_at_end(int priority, Node* node);
int not_empty(int priority);
int is_first_task(int priority, Node* task);
static void microsleep(unsigned int usecs);
struct timespec diff(struct timespec start, struct timespec end);

//Time variables
long type0_turnaround_time;
long type1_turnaround_time;
long type2_turnaround_time;

long type0_response_time;
long type1_response_time;
long type2_response_time;

int type0_count;
int type1_count;
int type2_count;

//Used to hold the tasks
Node* sjf_list;
Queue* high_priority;
Queue* med_priority;
Queue* low_priority;
Node* curr_task;

//Lock and conditional variable
pthread_cond_t need_work = PTHREAD_COND_INITIALIZER;
pthread_cond_t doing_work = PTHREAD_COND_INITIALIZER;
pthread_mutex_t main_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

int active_threads;
int scheduler_on;
int policy_mode;
int reset_priority = RESET_INTERVAL;

struct timespec arrival;

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
			policy_mode = user_input - '0';
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	FILE *task_file = fopen("tasks.txt", "r");
	assert(task_file != NULL);
	
	if (task_file != NULL)
	{
		sjf_list = NULL;		
		high_priority = malloc(sizeof(Queue));
		med_priority = malloc(sizeof(Queue));
		low_priority = malloc(sizeof(Queue));
		initialize_queue(HIGH, NULL);
		initialize_queue(MEDIUM, NULL);
		initialize_queue(LOW, NULL);
		
		user_prompts();
		clock_gettime(CLOCK_REALTIME, &arrival);
		system("clear");
		
		if (policy_mode == 1) //When using the SJF policy
		{
			load_tasks_sjf(task_file);
			simulate_sjf();
		}
		else if (policy_mode == 2) //When using the MLFQ policy
		{
			load_tasks_mlfq(task_file);
			simulate_mlfq();
		}
		
		print_statistics();
		fclose(task_file);
	}
	
	printf("\nProgram completed successfully.\nProgrammed by Rolf Olayan.\n\n");
    return EXIT_SUCCESS;
}

//Loads all the tasks into the list, sorted by the task length
void load_tasks_sjf(FILE *task_file)
{
	char buffer[BUFFER_SIZE];
	char delim[] = " ";
	
	while (fgets(buffer, BUFFER_SIZE, task_file))
	{
		Node* curr = sjf_list;
		Node* next = NULL;
		Node* new_node = malloc(sizeof(Node));
		
		char* token = strtok(buffer, delim);
		new_node->task_name = malloc(sizeof(token));
		strcpy(new_node->task_name, token);
		
		token = strtok(NULL, delim);
		new_node->task_type = atoi(token);

		if (new_node->task_type == 0)
		{
			type0_count++;
		}
		else if (new_node->task_type == 1)
		{
			type1_count++;
		}
		else
		{
			type2_count++;
		}
		
		token = strtok(NULL, delim);
		new_node->task_length = atoi(token);
		new_node->response_time = -1;
		
		if (curr == NULL) //First node
		{
			sjf_list = new_node;
		}
		else //Loops through the list until the appropriate spot is found
		{
			next = (Node*)curr->next;
			
			while (next != NULL && new_node->task_length > next->task_length)
			{
				curr = next;
				next = (Node*)curr->next;
			}
			
			if (new_node->task_length < curr->task_length && strcmp(curr->task_name, sjf_list->task_name) == 0)
			{
				new_node->next = (struct Node*)curr;
				sjf_list = new_node;
			}
			else
			{
				curr->next = (struct Node*)new_node;
				new_node->next = (struct Node*)next;
			}
		}
	}
}

//Loads all the tasks into the highest priority list on load
void load_tasks_mlfq(FILE *task_file)
{
	char buffer[BUFFER_SIZE];
	char delim[] = " ";
	
	while (fgets(buffer, BUFFER_SIZE, task_file))
	{
		Node* new_node = malloc(sizeof(Node));
		
		char* token = strtok(buffer, delim);
		new_node->task_name = malloc(sizeof(token));
		strcpy(new_node->task_name, token);
		
		token = strtok(NULL, delim);
		new_node->task_type = atoi(token);
		
		if (new_node->task_type == 0)
		{
			type0_count++;
		}
		else if (new_node->task_type == 1)
		{
			type1_count++;
		}
		else
		{
			type2_count++;
		}

		token = strtok(NULL, delim);
		new_node->task_length = atoi(token);
		new_node->time_allotment = MAX_ALLOTMENT;		
		new_node->response_time = -1;
		
		insert_at_end(HIGH, new_node);
	}
}

static void* task_driver()
{
	struct timespec completion, first_run;
	
	if (scheduler_on && active_threads <= MAX_THREADS) //Handles the scheduling/signalling
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
			
			if (curr_task == NULL)
			{
				break;
			}

			while (active_threads == MAX_THREADS)
			{
				pthread_cond_wait(&need_work, &lock);
			}
			
			//Gets the current task and increments our position in the list
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
				
				handle_task(process_task);
				
				//For logging the turnaround time
				clock_gettime(CLOCK_REALTIME, &completion);
				process_task->turnaround_time += 100;
				// process_task->turnaround_time += diff(first_run, completion).tv_nsec/1000;
								
				if (policy_mode == 1)
				{
					log_turnaround_time(process_task);
					curr_task = (Node*)curr_task->next;
				}
				else
				{
					if (process_task->task_length <= 0)
					{
						log_turnaround_time(process_task);
					}

					if (reset_priority <= 0)
					{
						reset_priority = RESET_INTERVAL;
						mlfq_reset_priority();
					}
					else
					{
						mlfq_next_job(process_task);
					}
				}
				
				active_threads--;
				pthread_mutex_unlock(&lock);
			}
		}
	}
 
    return NULL;
}

//Simulates the SJF scheduling policy
void simulate_sjf()
{
	printf("\nSimulating SJF with %d CPU(s)\n", MAX_THREADS);
	pthread_t workers[MAX_THREADS];
	pthread_t scheduler;
	curr_task = sjf_list;
	
	for (int i = 0; i < MAX_THREADS; i++)
	{
		pthread_create(&workers[i], NULL, task_driver, NULL);
	}
	
	scheduler_on = 1;
	while (curr_task != NULL)
	{
		pthread_create(&scheduler, NULL, task_driver, NULL);
	}

	// pthread_join(workers[0], NULL);
	pthread_join(scheduler, NULL);	
}

//Simulates the MLFQ scheduling policy
void simulate_mlfq()
{
	printf("\nSimulating MLFQ with %d CPU(s)\n", MAX_THREADS);
	pthread_t workers[MAX_THREADS];
	pthread_t scheduler;
	curr_task = (Node*)high_priority->head;

	for (int i = 0; i < MAX_THREADS; i++)
	{
		pthread_create(&workers[i], NULL, task_driver, NULL);
	}
	
	scheduler_on = 1;
	while (curr_task != NULL)
	{
		pthread_create(&scheduler, NULL, task_driver, NULL);
	}

	// pthread_join(workers[0], NULL);
	pthread_join(scheduler, NULL);
}

//Handles tasks
void handle_task(Node* task)
{
	if (policy_mode == 1)
	{
		microsleep(task->task_length);
	}
	else if (policy_mode == 2)
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

void mlfq_next_job(Node* processed_task)
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

//Stores the response times into the appropriate variables
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

//Stores the turnaround times into the appropriate variables
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

//Prints out the average turnaround and average response times to the console
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

void initialize_queue(int priority, Node* node)
{	
	if (priority == HIGH)
	{
		high_priority->head = (struct Node*)node;
		high_priority->tail = (struct Node*)node;
	}
	else if (priority == MEDIUM)
	{
		med_priority->head = (struct Node*)node;
		med_priority->tail = (struct Node*)node;
	}
	else if (priority == LOW)
	{
		low_priority->head = (struct Node*)node;
		low_priority->tail = (struct Node*)node;
	}	
}

void insert_at_end(int priority, Node* node)
{
	Node* end = NULL;
	node->next = NULL;
	
	if (priority == HIGH)
	{
		if (not_empty(HIGH))
		{
			end = (Node*)high_priority->tail;
			end->next = (struct Node*)node;
			high_priority->tail = (struct Node*)node;
		}
		else
		{
			initialize_queue(priority, node);
		}
	}
	else if (priority == MEDIUM)
	{
		if (not_empty(MEDIUM))
		{
			end = (Node*)med_priority->tail;
			end->next = (struct Node*)node;
			med_priority->tail = (struct Node*)node;
		}
		else
		{
			initialize_queue(priority, node);
		}
	}
	else if (priority == LOW)
	{
		if (not_empty(LOW))
		{
			end = (Node*)low_priority->tail;
			end->next = (struct Node*)node;
			low_priority->tail = (struct Node*)node;
		}
		else
		{
			initialize_queue(priority, node);
		}
	}	
}

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