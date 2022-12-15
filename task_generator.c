//-----------------------------------------
// NAME: Rolf Olayan
// STUDENT NUMBER: #7842749
// COURSE: COMP 4300
// Final Project - Task generator
//-----------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#define TASK_NUM 100
#define SHORT_TASK 33
#define MED_TASK 66
#define LONG_TASK 99
#define SHORT_LEN_MIN 100
#define SHORT_LEN_MAX 1500
#define MED_LEN_MIN 2000
#define MED_LEN_MAX 5000
#define LONG_LEN_MIN 10000
#define LONG_LEN_MAX 30000


int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	FILE *task_file = fopen("tasks.txt", "w");
	assert(task_file != NULL);
	
	if (task_file != NULL)
	{
		srand(time(NULL));

		for (int i = 0; i < TASK_NUM; i++)
		{
			int task_length = 0;
			int type = rand() % 100;
			int short_count = 0;
			int med_count = 0;
			int long_count = 0;

			if (type <= SHORT_TASK)
			{
				printf("short\n");
				task_length = rand() % (SHORT_LEN_MAX + 1 - SHORT_LEN_MIN) + SHORT_LEN_MIN;
				fprintf(task_file,"short_task_%d 0 %d\n", short_count, task_length);
				short_count++;
			}
			else if (type <= MED_TASK)
			{
				printf("med\n");
				task_length = rand() % (MED_LEN_MAX + 1 - MED_LEN_MIN) + MED_LEN_MIN;
				fprintf(task_file,"med_task_%d 1 %d\n", short_count, task_length);
				med_count++;
			}
			else if (type <= LONG_TASK)
			{
				printf("long\n");
				task_length = rand() % (LONG_LEN_MAX + 1 - LONG_LEN_MIN) + LONG_LEN_MIN;
				fprintf(task_file,"long_task_%d 2 %d\n", short_count, task_length);
				long_count++;
			}
		}
	}
	
	fclose(task_file);
	printf("\nProgram completed successfully.\nProgrammed by Rolf Olayan.\n\n");
    return EXIT_SUCCESS;
}
