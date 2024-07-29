#ifndef TPOOL_H
#define TPOOL_H

#include <pthread.h>

typedef struct tpool_work_t {
    pthread_t thread;
} tpool_work_t;

#endif // TPOOL_H
