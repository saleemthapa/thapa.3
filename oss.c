#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <signal.h>
#include <string.h>
#include <semaphore.h>
#include <fcntl.h>
#include <mqueue.h>
#include <time.h>
#include "shared.h"

#define MAX_WORKERS 10  // maximum number of workers
#define MAX_PROCESSES 20 // maximum number of processes
#define MAX_RUNTIME 2    // maximum allowed runtime for a worker in seconds

#define OSS_TO_WORKER_QUEUE_NAME "/oss_to_worker_queue"
#define WORKER_TO_OSS_QUEUE_NAME "/worker_to_oss_queue"

mqd_t oss_to_worker_queue, worker_to_oss_queue;
int max_workers = MAX_WORKERS;
int max_processes = MAX_PROCESSES;
int timeLimit; // Time limit for worker process launch
char logFileName[256]; // Log file name
sem_t mutex;
struct SystemClock *clock_ptr;

// Process Control Block (PCB) structure
struct PCB {
    int occupied;       // either true or false
    pid_t pid;          // process id of this child
    int startSeconds;   // time when it was forked
    int startNano;      // time when it was forked
    int entry;          // entry in the process table
};
struct PCB processTable[MAX_PROCESSES]; // Process table

// Function to log a message with a timestamp
void logMessage(const char *message) {
    FILE *logFile = fopen(logFileName, "a");
    if (logFile != NULL) {
	        struct timespec currentTime;
        clock_gettime(CLOCK_MONOTONIC, &currentTime);
        fprintf(logFile, "OSS: %s at time %ld:%ld\n", message, currentTime.tv_sec, currentTime.tv_nsec);
        fclose(logFile);
    }
}

//void initializeSharedResources(struct PCB processTable[]) {
 void initializeSharedResources() { 
// Create shared memory segment for the clock
    int shm_clock_id = shmget(IPC_PRIVATE, sizeof(struct SystemClock), IPC_CREAT | 0666);
    if (shm_clock_id == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    // Attach the shared memory segment to the OSS process
    clock_ptr = (struct SystemClock *)shmat(shm_clock_id, NULL, 0);
    if ((void *)clock_ptr == (void *)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    // Initialize the clock (seconds and nanoseconds) as needed
    clock_ptr->seconds = 0;
    clock_ptr->nanoseconds = 0; 
      // Create and open the message queues
    struct mq_attr mq_attributes;
    mq_attributes.mq_flags = 0;
    mq_attributes.mq_maxmsg = 10; // Maximum number of messages in the queue
    mq_attributes.mq_msgsize = sizeof(struct Message); // Size of a message
    mq_attributes.mq_curmsgs = 0;
	
    oss_to_worker_queue = mq_open(OSS_TO_WORKER_QUEUE_NAME, O_CREAT | O_RDWR, 0666, &mq_attributes);
    worker_to_oss_queue = mq_open(WORKER_TO_OSS_QUEUE_NAME, O_CREAT | O_RDWR, 0666, &mq_attributes);

    if (oss_to_worker_queue == (mqd_t)-1 || worker_to_oss_queue == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }

    // Open the log file for writing
    FILE *logFile = fopen(logFileName, "w");
    if (logFile != NULL) {
        fclose(logFile);
    }
}

void createWorkerProcesses(int max_workers, struct PCB processTable[]) {
    int totalWorkers = 0;
    int workersLaunched = 0;
    int randomTime = 0;
    int randomNano = 0;
    while (totalWorkers < max_workers && workersLaunched < max_workers && clock_ptr->seconds < (unsigned int)timeLimit) {
        // Generate a random time interval for worker process launch
        randomTime = (rand() % (timeLimit - 1)) + 1;
        randomNano = rand() % 1000;
        clock_ptr->nanoseconds = randomNano;

        pid_t worker_pid = fork();

        if (worker_pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (worker_pid == 0) {
            // Child process (worker)
	   char *worker_argv[] = {"./worker", NULL, NULL};
           char work_duration[32];
	   snprintf(work_duration, sizeof(work_duration), "%d", processTable[totalWorkers].entry);
	   worker_argv[1] = work_duration;
          int dev_null_fd = open("/dev/null", O_WRONLY);
if (dev_null_fd == -1) {
	    perror("open /dev/null");
    exit(EXIT_FAILURE);
}

dup2(dev_null_fd, STDERR_FILENO); // Redirect stderr to /dev/null
close(dev_null_fd);

	   execvp("./worker", worker_argv);
	   perror("execvp");
	   exit(EXIT_FAILURE);
        } else {
            // Parent process (OSS)
            processTable[totalWorkers].occupied = 1;
            processTable[totalWorkers].pid = worker_pid;
            processTable[totalWorkers].startSeconds = clock_ptr->seconds;
            processTable[totalWorkers].startNano = clock_ptr->nanoseconds;

            totalWorkers++;
            workersLaunched++;

            // Log sending and receiving messages
            char sendLog[100], receiveLog[100];
            snprintf(sendLog, sizeof(sendLog), "Sending message to worker %d PID %d at time %d:%d", processTable[totalWorkers - 1].entry, worker_pid, clock_ptr->seconds, clock_ptr->nanoseconds);
            logMessage(sendLog);
            snprintf(receiveLog, sizeof(receiveLog), "Receiving message from worker %d PID %d at time %d:%d", processTable[totalWorkers - 1].entry, worker_pid, clock_ptr->seconds, clock_ptr->nanoseconds);
            logMessage(receiveLog);
        }
    }
    sleep(randomTime);
}

void monitorWorkerProcesses(struct PCB processTable[]) {
    int terminated_count = 0;

    while (terminated_count < max_workers) {
        for (int i = 0; i < max_processes; i++) {
            if (processTable[i].occupied) {
                int status;
                int result = waitpid(processTable[i].pid, &status, WNOHANG);
                if (result == processTable[i].pid) {
                    // Child process has terminated
                    processTable[i].occupied = 0;
                    terminated_count++;
                  // Log the termination
                    char terminationMessage[100];
                    snprintf(terminationMessage, sizeof(terminationMessage), "Worker %d PID %d has terminated at time %u:%u", i, processTable[i].pid, clock_ptr->seconds, clock_ptr->nanoseconds);
                    logMessage(terminationMessage);
		   // Handle other actions if needed
                }
            }
        }
    }
}

void displayProcessTable(struct PCB processTable[]) {
    static time_t lastDisplayTime = 0;
    time_t currentTime = time(NULL);
    const int displayInterval = 5;  // Display every 5 seconds

    // Check if it's time to display the process table
    if (currentTime - lastDisplayTime >= displayInterval) {
        lastDisplayTime = currentTime;
        printf("OSS PID: %d SysClockS: %u SysClockNano: %u\n", getpid(), clock_ptr->seconds, clock_ptr->nanoseconds);
        printf("Process Table:\n");
        printf("Entry \t Occupied \t  PID \t StartS \t StartN\n");
        for (int i = 0; i < max_workers; i++) {
            printf("%d \t  %d \t \t %d \t  %d  \t %d\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
        }
        printf("\n");
    }
}

int main(int argc, char *argv[]) {
    // Parsing command-line arguments
   int opt;
   int optionsProvided = 0;

while ((opt = getopt(argc, argv, "n:s:t:f:")) != -1) {
    switch (opt) {
        case 'n':
            max_workers = atoi(optarg);
            optionsProvided = 1; // Set the flag when a valid option is provided
            break;
        case 's':
            max_processes = atoi(optarg);
            optionsProvided = 1;
            break;
        case 't':
            timeLimit = atoi(optarg);
            optionsProvided = 1;
            break;
        case 'f':
            strncpy(logFileName, optarg, sizeof(logFileName));
            optionsProvided = 1;
            break;
        default:
            // Invalid option encountered
            fprintf(stderr, "Invalid option: %c\n", optopt);
            break;
    }
}

// Display usage only if no valid options are provided
if (!optionsProvided) {
    fprintf(stderr, "Usage: %s -n <max_workers> -s <max_processes> -t <timelimit> -f <logfile>\n", argv[0]);
    return 1;
}




    // Initialize shared resources and create worker processes
    //initializeSharedResources(processTable);
    initializeSharedResources();
    createWorkerProcesses(max_workers, processTable);
	   // Monitoring and managing worker processes
    monitorWorkerProcesses(processTable);
    displayProcessTable(processTable);
    return 0;
}
