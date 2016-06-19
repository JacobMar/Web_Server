#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "util.h"

#define MAX_THREADS 100
#define MAX_QUEUE_SIZE 100
#define MAX_REQUEST_LENGTH 1024
#define MAX_ROOT_FILE_LEN 248
#define FILENAME_SIZE 1024
#define TRUE 1
#define FALSE 0

char ROOT_FILE_NAME[MAX_ROOT_FILE_LEN]; /* Use for root access */
int MAX_QUEUE_LEN = 0; /*Varible to hold global bounded queue length given by command line */
int current_queue_length = 0; /*Must use a variable to make sure the queue is first in, first out */
int DISPATCHERS_EXIT = 0; /*Global to indicate if all dispatchers have exitted*/
FILE *LOG_FILE;
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER, log_lock = PTHREAD_MUTEX_INITIALIZER; /* Lock for queue, log file */
pthread_cond_t queue_full = PTHREAD_COND_INITIALIZER, queue_empty = PTHREAD_COND_INITIALIZER;

typedef struct request_queue
{
        int             m_socket;
        char    m_szRequest[MAX_REQUEST_LENGTH];
} request_queue_t;

struct Node
{
	request_queue_t data;
	struct Node * next;
};

struct Node* front = NULL;
struct Node* rear = NULL;
int queue_is_empty()
{
	if (front == NULL && rear == NULL)
	{
		return 1;
	}
	return 0;
}

void enqueue(int m_socket, char * file_description) {

	struct Node* temp = 
		(struct Node*)malloc(sizeof(struct Node));

	temp->data.m_socket =m_socket; 
	strcpy(temp->data.m_szRequest, file_description);

	temp->next = NULL;

	if(queue_is_empty())
	{
		current_queue_length++;
		front = rear = temp;
		return;
	}
	rear->next = temp;
	rear = temp;
	current_queue_length++;
}

void dequeue() 
{

	struct Node* temp = front;
	if(queue_is_empty()) {
		printf("CANNOT DEQUEUE EMPTY Q, Queue is Empty\n");
		return;
	}
	if(front == rear) {
		front = rear = NULL;
	}
	else {
		front = front->next;
	}
	free(temp);
	current_queue_length--;
}

int queue_is_full()
{
	//printf("Current q len = %d, max q len = %d\n", current_queue_length, MAX_QUEUE_LEN);
	return (current_queue_length == MAX_QUEUE_LEN);
}


void peek(request_queue_t* request)
{
	strcpy(request->m_szRequest, front->data.m_szRequest);
	request->m_socket = front->data.m_socket;
	return;
}

char * get_content_type(char * request)
{
	char *token;
	token = strtok(request,".");
	token = strtok(NULL,"\0");
	if(strncmp(token,"htm",strlen("htm")) == 0)//html or htm
	{
		return "text/html";
	}
	else if(strcmp("jpg",token) == 0) //jpg
	{
		return "image/jpeg";
	}
	else if(strcmp("gif",token) == 0) //gif
	{
		return "image/gif";
	}
	else //text
	{
		return "text/plain";
	}
}


void * dispatch(void * arg)
{
	int fd = 0; /* file descriptor to be set by accept_connection */
	int id = (int) arg; /* Id of the current thread */
	char filename[MAX_REQUEST_LENGTH];
	memset(filename, 0, MAX_REQUEST_LENGTH+1);
	
	while(TRUE)
	{
			//printf("Hitting lock in dispatcher22\n");
		if ((fd = accept_connection()) < 0 )
		{
			perror("Failure in accepting connection\n");
			pthread_exit(NULL); /*  Quit on failed connection as per assignment */
		}
		/* Get request returns 0 on success, nonzero on failure.  Accounting for failures by not exiting thread */
		if (get_request(fd, filename) != 0)
		{
			continue;
		}

	/*Lock for queue access */
	pthread_mutex_lock(&queue_lock);
	/* if queue is full, wait for it to empty */
	while (queue_is_full())
	{
		pthread_cond_wait(&queue_full, &queue_lock);
	}
	/* enqueue a request */
	enqueue(fd, filename);
	pthread_mutex_unlock(&queue_lock);/*Unlock and change CV to allow other access */
	pthread_cond_signal(&queue_empty);
	}
	return NULL;
}

void * worker(void * arg)
{
	/* If success in opening, process the file */
	struct stat file_info; /*Struct for holding file information such as the bytes, etc...*/
	char directory_name[FILENAME_SIZE];
	char content_type[100];
	int request_counter = 0; /* Variable which denotes which request the current request is, for log */
	int request_fd = 0, id = 0, i = 0, directory_fd = 0, byte_size = 0;
	char * data; /* Data from the requested file */
	request_queue_t *current_request;
	current_request = malloc(sizeof(request_queue_t));
	id = (int)arg;
	while (TRUE)
	{
		pthread_mutex_lock(&queue_lock);
		while(queue_is_empty())
		{
			pthread_cond_wait(&queue_empty, &queue_lock);
		}
		
		if (DISPATCHERS_EXIT)
		{
			pthread_mutex_unlock(&queue_lock);
			return NULL;
		}

		peek(current_request); /* Setting equal to object related to node at top of stack */
		dequeue();	/* dequeue the node */
		/* Allow access for other threads */
		request_counter++;
		strcpy(directory_name, current_request->m_szRequest);
		request_fd = current_request->m_socket;

		pthread_cond_signal(&queue_full);
		pthread_mutex_unlock(&queue_lock);

		/* Opening requests given file name/directory for processing*/
		if ((directory_fd  = open(directory_name+1, O_RDONLY)) == -1)
		{
			return_error(request_fd, "Failure in accessing requested file/dir\n");
			/*Lock for accessing log file */
			pthread_mutex_lock(&log_lock);
			if (fprintf(LOG_FILE, "[%d][%d][%d][%s][%s]\n", id, request_counter, request_fd, directory_name, "File not found") < 0)
			{
				perror("fprintf write error\n");
			}
			fflush(LOG_FILE);
			pthread_mutex_unlock(&log_lock);
			continue;
		}

		fstat(directory_fd, &file_info);
		byte_size = file_info.st_size;
		/*Malloc size of data according to how large the file is, given by the stats*/
		data = malloc(byte_size);
		if (data == NULL)
		{
			printf("Malloc error\n");
		}
		/*Read the data from the file into the allocated data memory*/
		fflush(stdout);
		if (read(directory_fd, data, byte_size) < 0)
		{
			perror("Error reading file\n");
		}
		close(directory_fd);
		
		pthread_mutex_lock(&log_lock);
		if (fprintf(LOG_FILE,"[%d][%d][%d][%s][%d]\n", id, request_counter, request_fd, directory_name, byte_size) < 0)
		{
			perror("File write error\n");
		}
		fflush(LOG_FILE);
		pthread_mutex_unlock(&log_lock);
		
		strcpy(content_type, get_content_type(directory_name));
		fflush(stdout);
		fflush(stdout);
		if(return_result(request_fd, content_type, data, byte_size) != 0)
		{
			printf("Error in return result, %d\n", id);
		}
	
		fflush(stdout);
		free(data);

	}
		free(current_request);
	return;
}


/* Receives command line args....
* 1.  Port #
* 2.  Path to web root location, where files will be served from
* 3.  num_dispatcher, number of dispatcher threads to start
* 4.  num_workers, number of workers to start
* 5.  qlen, length of the qeue
* 6.  cache_entries (optional)
*/
int main(int argc, char ** argv)
{
        //Error check first.
        if(argc != 6 && argc != 7)
        {
                printf("usage: %s port path num_dispatcher num_workers queue_length [cache_size]\n", argv[0]);
                return -1;
        }			
	int i = 0; //Var for loops
	int PORT_NUM = atoi(argv[1]); //Variable to hold portnum.
	int DISPATCHER_NUM = atoi(argv[3]); //Variable to hold command line num of dispatchers
	int WORKER_NUM = atoi(argv[4]); //Variable to hold number of workers from command line
	MAX_QUEUE_LEN = atoi(argv[5]); //Length from command line, bounded queue length
	strcpy(ROOT_FILE_NAME, argv[2]); //Storing the root file name in the global variable ROOT_FILE_NAME


	if((PORT_NUM < 1025) || (PORT_NUM > 65535))
	{
		perror("Invalid port number\n");
	}

	init(PORT_NUM); /* Initializing, must be done before calling other functions */

	LOG_FILE = fopen("webserver_log", "w");
	if (LOG_FILE == NULL)
	{
		perror("Error openning log file\n");
	}
	/* Changing the directory to the desired root */
	if (chdir(ROOT_FILE_NAME) != 0)
	{
		perror("Failure in changing directory to desired root\n");
	}
	if (MAX_QUEUE_LEN > MAX_QUEUE_SIZE)
	{
		perror("Over max Q size\n");
		MAX_QUEUE_LEN = MAX_QUEUE_SIZE;
	}

	/* Creating thread structs */
	pthread_t dispatchers[DISPATCHER_NUM], workers[WORKER_NUM];
	/* Creating dispatcher threads */
	for (i = 0; i < DISPATCHER_NUM; i++)
	{
		if (pthread_create(&dispatchers[i], NULL, dispatch, (void*)i) == -1)
		{
			perror("error in thread creation (dispatchers)\n");
		}
	}
	for (i = 0; i < WORKER_NUM; i++)
	{
		if (pthread_create(&workers[i], NULL, worker, (void*)i) == -1)
		{
			perror("Error in thread creation (workers)\n");
		}
	}
	
	for (i = 0; i < DISPATCHER_NUM; i++)
	{
		pthread_join(dispatchers[i], NULL);
	}

	pthread_mutex_lock(&queue_lock);
	while (queue_is_full())
	{
		pthread_cond_wait(&queue_full, &queue_lock);
	}
	DISPATCHERS_EXIT = 1;	
	pthread_cond_broadcast(&queue_empty);
	pthread_mutex_unlock(&queue_lock);
	
	for (i = 0; i < WORKER_NUM; i++)
	{
		pthread_join(workers[i], NULL);
	}

	fclose(LOG_FILE);
	return 1;
}
	


