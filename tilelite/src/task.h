#pragma once

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>

typedef enum { TASK_FIREFORGET, TASK_SINGLEWAIT, TASK_MULTIWAIT } task_type;

typedef struct { int32_t executing_thread_idx; } task_extra_info;

typedef void* (*task_fn)(void*, const task_extra_info*);

typedef struct {
  int32_t id;
  task_type type;
  pthread_mutex_t lock;
  pthread_cond_t cv;
  task_fn execute;
  void* arg;
  sem_t* waitall_sema;
} task;

task* task_create(task_fn execute, void* arg);
void task_destroy(task* t);
