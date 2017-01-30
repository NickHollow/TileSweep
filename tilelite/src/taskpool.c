#include "taskpool.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "tl_log.h"

typedef struct {
  int thread_num;
  taskpool* pool;
  pthread_t id;
  tc_sema* sema;
} pool_thread_info;

int pool_queue_pop(pool_queue* q, tc_task** t) {
  pthread_mutex_lock(&q->lock);
  int res = tc_queue_pop(&q->queue, t);
  pthread_mutex_unlock(&q->lock);
  return res;
}

void pool_queue_push(pool_queue* q, tc_task* t) {
  assert(t);
  pthread_mutex_lock(&q->lock);
  tc_queue_push(&q->queue, &t);
  pthread_mutex_unlock(&q->lock);
}

static void pool_add_task(taskpool* pool, tc_task* t, task_priority priority) {
  assert(priority >= 0 && priority < TP_COUNT);
  int queue_idx = atomic_fetch_add(&pool->insert_idx, 1) % pool->num_threads;
  pool_queue* q = &pool->queues[priority][queue_idx];
  pool_queue_push(q, t);
}

static int32_t get_task(taskpool* pool, tc_task** t) {
  for (int32_t p = TP_HIGH; p < TP_COUNT; p++) {
    for (int32_t i = 0; i < pool->num_threads; i++) {
      if (pool_queue_pop(&pool->queues[p][i], t)) {
        return 1;
      }
    }
  }

  return 0;
}

static void* pool_task(void* arg) {
  pool_thread_info* info = (pool_thread_info*)arg;

  const tc_task_extra_info extra = {.executing_thread_idx = info->thread_num};

  for (;;) {
    tc_sema_wait(info->sema);

    tc_task* t = NULL;

    if (get_task(info->pool, &t)) {
      assert(t);
#ifdef DEBUG
      int64_t start_time = tl_usec_now();
#endif
      switch (t->type) {
        case TASK_FIREFORGET: {
          t->execute(t->arg, &extra);
          tc_task_destroy(t);
          break;
        }
        case TASK_SINGLEWAIT: {
          pthread_mutex_lock(&t->lock);
          t->execute(t->arg, &extra);
          pthread_cond_signal(&t->cv);
          pthread_mutex_unlock(&t->lock);
          break;
        }
        case TASK_MULTIWAIT: {
          t->execute(t->arg, &extra);
          tc_sema_post(t->waitall_sema, 1);
          break;
        };
        default:
          break;
      }
#ifdef DEBUG
      tl_log("[pool-%d] task exec: %.3f ms", info->thread_num,
             (tl_usec_now() - start_time) / 1000.0);
#endif
    }
  }
}

taskpool* taskpool_create(int32_t threads) {
  taskpool* pool = (taskpool*)calloc(1, sizeof(taskpool));
  pool->num_threads = threads;
  pool->threads = calloc(threads, sizeof(pool_thread_info));

  for (int32_t p = TP_HIGH; p < TP_COUNT; p++) {
    pool->queues[p] = (pool_queue*)calloc(threads, sizeof(pool_queue));
    for (int32_t i = 0; i < threads; i++) {
      pool_queue* q = &pool->queues[p][i];
      tc_queue_init(&q->queue, sizeof(tc_task*));
      pthread_mutex_init(&q->lock, NULL);
    }
  }

  tc_sema_init(&pool->sema, 0);
  atomic_init(&pool->insert_idx, 0);

  pool_thread_info* pool_threads = (pool_thread_info*)pool->threads;

  for (int32_t i = 0; i < threads; i++) {
    pool_thread_info* t = &pool_threads[i];
    t->thread_num = i;
    t->pool = pool;
    t->sema = &pool->sema;
    pthread_create(&t->id, NULL, pool_task, t);
  }

  return pool;
}

void taskpool_wait(taskpool* pool, tc_task* t, task_priority priority) {
  t->type = TASK_SINGLEWAIT;
  assert(pthread_mutex_init(&t->lock, NULL) == 0);
  assert(pthread_cond_init(&t->cv, NULL) == 0);

  pool_add_task(pool, t, priority);
  pthread_mutex_lock(&t->lock);
  tc_sema_post(&pool->sema, 1);
  pthread_cond_wait(&t->cv, &t->lock);
  pthread_mutex_unlock(&t->lock);

  pthread_mutex_destroy(&t->lock);
  pthread_cond_destroy(&t->cv);
}

void taskpool_post(taskpool* pool, tc_task* t, task_priority priority) {
  t->type = TASK_FIREFORGET;
  pool_add_task(pool, t, priority);
  tc_sema_post(&pool->sema, 1);
}

void taskpool_wait_all(taskpool* pool, tc_task** tasks, int32_t count,
                       task_priority priority) {
  tc_sema wait_sema;
  tc_sema_init(&wait_sema, 0);

  for (int32_t i = 0; i < count; i++) {
    tc_task* t = tasks[i];
    t->type = TASK_MULTIWAIT;
    t->waitall_sema = &wait_sema;
    pool_add_task(pool, t, priority);
  }

  tc_sema_post(&pool->sema, count);

  int32_t left = count;

  while (left > 0) {
    tc_sema_wait(&wait_sema);
    left--;
  }

  tc_sema_deinit(&wait_sema);
}

void taskpool_destroy(taskpool* pool) {
  pool_thread_info* pool_threads = (pool_thread_info*)pool->threads;

  for (int i = 0; i < pool->num_threads; i++) {
    pthread_join(pool_threads[i].id, NULL);
  }

  free(pool->threads);
  free(pool);
}
