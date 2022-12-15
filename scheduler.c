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

typedef struct NODE {
	char* task_name;
	int task_type;
	int task_length;
	int time_allotment;
	long response_time;
	long turnaround_time;
	struct timespec first_run;
    struct Node* next;
} Node;

void user_prompts();
void mlfq_reset_priority();
void mlfq_next_job(Node* processed_task);
void load_tasks_sjf(FILE *task_file);
void load_tasks_mlfq(FILE *task_file);
static void* sjf_task_driver();
void simulate_sjf();
void simulate_mlfq();
void log_turnaround_time(Node* task);
void log_response_time(Node* task);
void print_statistics();
static void microsleep(unsigned int usecs);
void handle_task(Node* task);
struct timespec diff(struct timespec start, struct timespec end);
void insert_at_end(Node* head, Node* node);

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
Node* high_priority;
Node* med_priority;
Node* low_priority;
Node* curr_task;

//Lock and conditional variable
pthread_cond_t need_work = PTHREAD_COND_INITIALIZER;
pthread_cond_t doing_work = PTHREAD_COND_INITIALIZER;
pthread_mutex_t main_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

int max_thread_num;
int active_threads;
int scheduler_on;
int policy_mode;
int reset_priority = 2000;

struct timespec arrival;

// Used to prompt user for their desired simulation settings
void user_prompts()
{
	char user_input;

	while (1)
	{
		system("clear");
		printf("Please specify the amount of CPU cores you want to simulate with (4/8):\n");
		user_input = getchar();
		if (user_input == '4' || user_input == '8')
		{
			max_thread_num = user_input - '0';
			break;
		}
	}

	while (1)
	{
		system("clear");
		printf("Please specify which scheduling algorithm you want to simulate with:\n");
		printf("> 0. SJF\n> 1. MLFQ\n");
		user_input = getchar();
		if (user_input == '0' || user_input == '1')
		{
			policy_mode = user_input - '0';
			system("clear");
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
		high_priority = NULL;
		med_priority = NULL;
		low_priority = NULL;
		
		user_prompts();
		clock_gettime(CLOCK_REALTIME, &arrival);
		
		if (policy_mode == 0) //When using the SJF policy
		{
			load_tasks_sjf(task_file);
			simulate_sjf();
		}
		else if (policy_mode == 1) //When using the MLFQ policy
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
		
		new_node->next = (struct Node*)high_priority;
		high_priority = new_node;
	}
}

static void* sjf_task_driver()
{
	struct timespec completion, first_run;
	
	if (scheduler_on && active_threads <= max_thread_num) //Handles the scheduling/signalling
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

			while (active_threads == max_thread_num)
			{
				pthread_cond_wait(&need_work, &lock);
			}
			
			if (curr_task == NULL)
			{
				break;
			}
			
			//Gets the current task and increments our position in the list
			Node* process_task = curr_task;
			pthread_mutex_unlock(&lock);
			
			if (process_task != NULL)
			{
				pthread_mutex_lock(&lock);
				active_threads++;
				curr_task = (Node*)curr_task->next;
				//Logs the response time
				clock_gettime(CLOCK_REALTIME, &first_run);
				process_task->response_time = diff(arrival, first_run).tv_nsec/1000;
				log_response_time(process_task);
				
				handle_task(process_task);
				
				//Logs the turnaround time
				clock_gettime(CLOCK_REALTIME, &completion);
				process_task->turnaround_time = diff(first_run, completion).tv_nsec/1000;
				log_turnaround_time(process_task);
				active_threads--;
				pthread_mutex_unlock(&lock);
			}
		}
	}
 
    return NULL;
}

static void* mlfq_task_driver()
{
	struct timespec completion, first_run;
	
	if (scheduler_on && active_threads <= max_thread_num) //Handles the scheduling/signalling
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
			
			while (active_threads == max_thread_num)
			{
				pthread_cond_wait(&need_work, &lock);
			}

			if (curr_task == NULL)
			{
				break;
			}
			
			//Gets the current task and increments our position in the list
			Node* process_task = curr_task;
			pthread_mutex_unlock(&lock);
			curr_task = (Node*)curr_task->next;	

			if (process_task != NULL)
			{
				pthread_mutex_lock(&lock);
				active_threads++;
				curr_task = (Node*)curr_task->next;
				//Logs the response time
				clock_gettime(CLOCK_REALTIME, &first_run);
				process_task->response_time = diff(arrival, first_run).tv_nsec/1000;
				log_response_time(process_task);
				
				handle_task(process_task);

				clock_gettime(CLOCK_REALTIME, &completion);
				process_task->turnaround_time = diff(first_run, completion).tv_nsec/1000;
				printf("%ld\n", process_task->turnaround_time);
				
				if (process_task->task_length <= 0)
				{
					clock_gettime(CLOCK_REALTIME, &completion);
					process_task->turnaround_time = diff(first_run, completion).tv_nsec/1000;
					log_turnaround_time(process_task);
				}

				// if (reset_priority <= 2000)
				// {
				// 	mlfq_reset_priority();
				// }

				// mlfq_next_job(process_task);
				active_threads--;
				pthread_mutex_unlock(&lock);
			}
		}
	}
 
    return NULL;
}

void mlfq_reset_priority()
{
	Node* curr;

	if (med_priority != NULL)
	{
		curr = med_priority;
		while (curr != NULL)
		{
			insert_at_end(high_priority, curr);
			curr = (Node*)curr->next;
		}
	}

	if (low_priority != NULL)
	{
		curr = low_priority;
		while (curr != NULL)
		{
			insert_at_end(high_priority, curr);
			curr = (Node*)curr->next;
		}
	}
}

void mlfq_next_job(Node* processed_task)
{
	if (high_priority != NULL)
	{
		if (strcmp(high_priority->task_name, processed_task->task_name) == 0)
		{
			high_priority = (Node*)processed_task->next;
			curr_task = high_priority;
			if (processed_task->time_allotment > 0)
			{
				insert_at_end(high_priority, processed_task);
			}
			else
			{
				insert_at_end(med_priority, processed_task);
			}
		}
		else
		{
			curr_task = high_priority;
		}
	}
	else if (med_priority != NULL)
	{
		if (strcmp(med_priority->task_name, processed_task->task_name) == 0)
		{
			med_priority = (Node*)processed_task->next;
			curr_task = med_priority;
			if (processed_task->time_allotment > 0)
			{
				insert_at_end(med_priority, processed_task);
			}
			else
			{
				insert_at_end(low_priority, processed_task);
			}
		}
		else
		{
			curr_task = med_priority;
		}
	}
	else
	{
		if (strcmp(low_priority->task_name, processed_task->task_name) == 0)
		{
			low_priority = (Node*)processed_task->next;
			curr_task = low_priority;
			insert_at_end(low_priority, processed_task);
		}
		else
		{
			curr_task = low_priority;
		}
	}
}

//Simulates the SJF scheduling policy
void simulate_sjf()
{
	printf("\nUsing SJF with %d CPU(s)\n", max_thread_num);
	pthread_t workers[max_thread_num];
	pthread_t scheduler;
	curr_task = sjf_list;
	
	for (int i = 0; i < max_thread_num; i++)
	{
		pthread_create(&workers[i], NULL, sjf_task_driver, NULL);
	}
	
	scheduler_on = 1;
	while (curr_task != NULL)
	{
		pthread_create(&scheduler, NULL, sjf_task_driver, NULL);
	}

	pthread_join(workers[0], NULL);
	pthread_join(scheduler, NULL);	
}

//Simulates the MLFQ scheduling policy
void simulate_mlfq()
{
	printf("\nUsing MLFQ with %d CPU(s)\n", max_thread_num);
	pthread_t workers[max_thread_num];
	pthread_t scheduler;
	curr_task = high_priority;

	for (int i = 0; i < max_thread_num; i++)
	{
		pthread_create(&workers[i], NULL, mlfq_task_driver, NULL);
	}
	
	scheduler_on = 1;
	while (curr_task != NULL)
	{
		pthread_create(&scheduler, NULL, mlfq_task_driver, NULL);
	}

	// pthread_join(workers[0], NULL);
	pthread_join(scheduler, NULL);
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
	printf("%ld, %ld, %ld\n",type0_turnaround_time, type1_turnaround_time, type2_turnaround_time);
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

//Used when "running tasks"
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

//Handles tasks
void handle_task(Node* task)
{
	if (policy_mode == 0)
	{
		microsleep(task->task_length);
	}
	else if (policy_mode == 1)
	{
		int remainder = task->task_length - TIME_SLICE;

		if (remainder)
		{
			microsleep(TIME_SLICE);
			task->task_length -= TIME_SLICE;
			task->time_allotment -= TIME_SLICE;
			reset_priority -= TIME_SLICE;
		}
		else
		{
			microsleep(TIME_SLICE + remainder);
			task->task_length = 0;
			task->time_allotment = 0;
			reset_priority -= (TIME_SLICE + remainder);
		}
	}
}

void insert_at_end(Node* head, Node* node)
{
	Node* curr_node = head;
	node->next = NULL;

	if (curr_node == NULL)
	{
		curr_node = node;
	}
	else
	{
		while (curr_node->next != NULL)
		{
			curr_node = (Node*)curr_node->next;
		}

		curr_node->next = (struct Node*)node;
	}
}

// Used to calculate difference between start and end time (from "Profiling Code Using clock_gettime by Guy Rutenberg")
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
