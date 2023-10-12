#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/shm.h>
#include <mqueue.h>
#include <signal.h>
#include "shared.h"

#define OSS_TO_WORKER_QUEUE_NAME "/oss_to_worker_queue"
#define WORKER_TO_OSS_QUEUE_NAME "/worker_to_oss_queue"
#define MAX_CHILD_RUNTIME 20

struct SystemClock *clock_ptr;
mqd_t oss_to_worker_queue, worker_to_oss_queue;

// Define message data type
enum MessageType {
    TERMINATE_WORKER = 1, // Example message type for termination
    // Add more message types as needed
};

// Helper function to compare two struct timespec objects
int timespec_compare(const struct timespec *a, const struct timespec *b) {
    if (a->tv_sec < b->tv_sec) return -1;
    if (a->tv_sec > b->tv_sec) return 1;
    if (a->tv_nsec < b->tv_nsec) return -1;
    if (a->tv_nsec > b->tv_nsec) return 1;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <seconds> <nanoseconds>\n", argv[0]);
        return 1;
    }

    int work_seconds = atoi(argv[1]);
    int work_nanoseconds = atoi(argv[2]);

    // Attach the worker process to the shared memory segment for the clock
    int shm_clock_id = shmget(IPC_PRIVATE, sizeof(struct SystemClock), IPC_CREAT | 0666);
    if (shm_clock_id == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    // Attach the shared memory segment to the worker process
    clock_ptr = (struct SystemClock *)shmat(shm_clock_id, NULL, 0);
    if ((void *)clock_ptr == (void *)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    srand(time(NULL));
    int random = rand() % 1000000; // Generate a random number

    printf("WORKER PID: %d PPID: %d Called with oss: TermTimeS: %d TermTimeNano: %d\n",
           getpid(), getppid(), work_seconds, work_nanoseconds);

    // Simulate work by sleeping, but ensure it doesn't exceed MAX_CHILD_RUNTIME
    struct timespec max_duration;
    max_duration.tv_sec = MAX_CHILD_RUNTIME;
    max_duration.tv_nsec = 0;
	
    struct timespec work_duration;
    work_duration.tv_sec = work_seconds;
    work_duration.tv_nsec = work_nanoseconds;

    if (timespec_compare(&work_duration, &max_duration) > 0) {
        printf("Worker %d exceeded the maximum allowed runtime.\n",getpid());
        work_duration = max_duration;
    }

    nanosleep(&work_duration, NULL);

    if (random % 2 == 0) {
        printf("WORKER PID: %d PPID: %d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n",
               getpid(), getppid(), clock_ptr->seconds, clock_ptr->nanoseconds, work_seconds, work_nanoseconds);
    } else {
        printf("WORKER PID: %d PPID: %d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n",
               getpid(), getppid(), clock_ptr->seconds, clock_ptr->nanoseconds, work_seconds, work_nanoseconds);
   // Simulate time passing, assuming that the clock updates by 1 nanosecond
clock_ptr->nanoseconds++;
if (clock_ptr->nanoseconds >= 1000000000) {
    clock_ptr->seconds++;
    clock_ptr->nanoseconds = 0;
  }

    // Send a message back to OSS
    struct Message message;
    message.mtype = 1;
    message.sender_pid = getpid();
    message.data1 = TERMINATE_WORKER; // Example termination message
    message.data2 = 100;

    worker_to_oss_queue = mq_open(WORKER_TO_OSS_QUEUE_NAME, O_RDWR);
    if (worker_to_oss_queue == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }

    oss_to_worker_queue = mq_open(OSS_TO_WORKER_QUEUE_NAME, O_RDWR);
    if (oss_to_worker_queue == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }

    if (mq_send(worker_to_oss_queue, (const char *)&message, sizeof(message), 0) == -1) {
        perror("mq_send");
        exit(EXIT_FAILURE);
    }

    printf("Received message from OSS:\n");
    printf("Message type: %ld\n", message.mtype);
    printf("Sender PID: %d\n", message.sender_pid);
    printf("Data1: %d\n", message.data1);
    printf("Data2: %d\n", message.data2);

    // Message receiving and processing loop
    while (1) {
        // Receive a message from the OSS process
        if (mq_receive(oss_to_worker_queue, (char *)&message, sizeof(struct Message), NULL) == -1) {
            perror("mq_receive");
            exit(EXIT_FAILURE);
        }

        // Process the received message
        if (message.data1 == TERMINATE_WORKER) {
            printf("Received a termination message from OSS. Terminating...\n");
            break; // Terminate the worker process
        }

        // Handle other message types and data as needed
    }

    // Close the message queues
    mq_close(worker_to_oss_queue);
    mq_close(oss_to_worker_queue);

    // Detach the shared memory segment
    shmdt(clock_ptr);

    return 0;
}
}
