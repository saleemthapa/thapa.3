#ifndef SHARED_H
#define SHARED_H

// Define your shared data structures, constants, and semaphores here
// shared.h

// Define a structure for the shared clock
struct SystemClock {
    unsigned int seconds;
    unsigned int nanoseconds;
};

struct Message {
    long mtype;   // Message type (can be used to distinguish message types)
    int sender_pid;  // Process ID of the sender (oss or worker)
    int data1;     // Additional data fields as needed
    int data2;
    // Add more fields as required
};


// Define MAX_WORKERS as a global variable
extern int MAX_WORKERS; 
extern int MAX_PROCESSES;
#endif
