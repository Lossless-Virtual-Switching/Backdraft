#ifndef _VPORT_CONST_H
#define _VPORT_CONST_H
// #define MAX_QUEUES_PER_DIR 16384
#define MAX_QUEUES_PER_DIR 32

#define SLOTS_PER_LLRING 64
#define SLOTS_WATERMARK ((SLOTS_PER_LLRING >> 3) * 7) /* 87.5% */
#define EXTENDABLE 1
#define QUEUE_LENGTH_UPPER_BOUND 4096

// If you want to change this, go and update the code
// for enqeueu and dequeue
#define SINGLE_PRODUCER 1
#define SINGLE_CONSUMER 1

#define VPORT_DIR_PREFIX "vports"
#define PORT_DIR_LEN PORT_NAME_LEN + 256
#define TMP_DIR "/tmp"

#define RATE_INIT_VALUE 20000000
#endif
