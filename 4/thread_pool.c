#include "thread_pool.h"
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

struct thread_task {
	thread_task_f function;
	void *arg;

    /* PUT HERE OTHER MEMBERS */
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  bool is_finished;
  bool is_pushed;
  bool is_joined;
  void *result;
  struct thread_task *next;

};

struct thread_pool {
  pthread_t *threads;
  pthread_mutex_t mutex;
  pthread_cond_t cond;

  struct thread_task *queue_head;
  struct thread_task *queue_tail;
  size_t queue_length;
  size_t active_tasks;
  size_t thread_capacity;
  size_t active_threads;
};

// CUSTOM
void *thread_pool_worker(void *arg) {
    struct thread_pool *pool = (struct thread_pool *)arg;

    while (true) {
        printf("Worker step... Active threads: %ld, Thread capacity: %zu\n", pool->active_threads, pool->thread_capacity);
        struct thread_task *task;

        pthread_mutex_lock(&pool->mutex);

        while (pool->queue_head == NULL) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }

        task = pool->queue_head;
        if (task != NULL) {
            pool->queue_head = task->next;
            pool->queue_length--;
            pool->active_tasks++;

            if (pool->queue_head == NULL) {
                printf("Queue empty...\n");
                pool->queue_tail = NULL;
            }
        }

        pthread_mutex_unlock(&pool->mutex);

        if (task != NULL) {
            printf("Starting task...\n");
            pthread_mutex_lock(&task->mutex);
            void *result = task->function(task->arg);
            task->result = result;

            task->is_finished = true;
            pool->active_tasks--;
            pthread_mutex_unlock(&task->mutex);
            pthread_cond_signal(&task->cond);
            printf("Task finished.\n");
        }
    }

    return NULL;
}

// DONE
int
thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
    if (max_thread_count <= 0 || max_thread_count > TPOOL_MAX_THREADS) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    *pool = malloc(sizeof(struct thread_pool));
    if (*pool == NULL) {
        return TPOOL_ERR_NOT_IMPLEMENTED;
    }



    (*pool)->threads = malloc(sizeof(pthread_t) * max_thread_count);
    if ((*pool)->threads == NULL) {
        free(*pool);
        return TPOOL_ERR_NOT_IMPLEMENTED;
    }

    pthread_mutex_init(&(*pool)->mutex, NULL);
    pthread_cond_init(&(*pool)->cond, NULL);
    (*pool)->queue_head = NULL;
    (*pool)->queue_tail = NULL;
    (*pool)->thread_capacity = max_thread_count;
    (*pool)->active_threads = 0;
    (*pool)->queue_length = 0;
    (*pool)->active_tasks = 0;

    return 0;
}

// DONE
int
thread_pool_thread_count(const struct thread_pool *pool)
{
	return pool->active_threads;
}
// DONE
int
thread_pool_delete(struct thread_pool *pool)
{

    if (pool->queue_head != NULL || pool->active_tasks > 0) {
        return TPOOL_ERR_HAS_TASKS;
    }

    pthread_cond_broadcast(&pool->cond);

    if (pool->active_threads != 0) {
        for (size_t i = 0; i < pool->thread_capacity; ++i) {
            pthread_join(pool->threads[i], NULL);
        }
    }

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    free(pool->threads);
    free(pool);

    return 0;
}

// DONE
int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
		if (pool->queue_length >= TPOOL_MAX_TASKS) {
		    pthread_mutex_unlock(&pool->mutex);
		    return TPOOL_ERR_TOO_MANY_TASKS;
		}

        pthread_mutex_lock(&pool->mutex);

		if (pool->queue_head == NULL) {
		    pool->queue_head = task;
		    pool->queue_tail = task;
		} else {
		    pool->queue_tail->next = task;
		    pool->queue_tail = task;
		}

		pool->queue_length++;

        if (pool->thread_capacity > pool->active_threads &&
            pool->active_tasks == pool->active_threads) {
            if (pthread_create(&(pool->threads[pool->active_threads++]), NULL, thread_pool_worker, (void*) pool) != 0) {
                pthread_mutex_unlock(&pool->mutex);
            }
        }

        task->next = NULL;
        task->is_pushed = true;

        pthread_mutex_unlock(&pool->mutex);
		pthread_cond_signal(&pool->cond);


		return 0;
}

// DONE
int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
    (*task) = malloc(sizeof(struct thread_task));

    pthread_mutex_init(&(*task)->mutex, NULL);
    pthread_cond_init(&(*task)->cond, NULL);
    (*task)->function = function;
    (*task)->arg = arg;
    (*task)->is_finished = false;
    (*task)->is_pushed = false;
    (*task)->is_joined = false;
    (*task)->result = NULL;
    (*task)->next = NULL;


    return 0;
}

// DONE
bool
thread_task_is_finished(const struct thread_task *task)
{
    return task->is_finished;
}

// DONE
bool
thread_task_is_running(const struct thread_task *task)
{
    return !thread_task_is_finished(task);
}

int
thread_task_join(struct thread_task *task, void **result)
{
    printf("Joining task...\n");
    if (!task->is_pushed) return TPOOL_ERR_TASK_NOT_PUSHED;

    pthread_mutex_lock(&task->mutex);

    while (!thread_task_is_finished(task)) {
        pthread_cond_wait(&task->cond, &task->mutex);
    }

    if (result != NULL) {
        *result = task->result;
    }
    task->is_joined = true;
    pthread_mutex_unlock(&task->mutex);
    printf("Task joined.\n");

    return 0;
}

int
thread_task_delete(struct thread_task *task)
{
     printf("Task deleting.\n");
    if (task->is_pushed && !task->is_joined && !task->is_finished) {
        return TPOOL_ERR_TASK_IN_POOL;
    };
    pthread_mutex_destroy(&task->mutex);
    pthread_cond_destroy(&task->cond);
    free(task);
    printf("Task deleted.\n");
    return 0;
}

#ifdef NEED_TIMED_JOIN

int
thread_task_timed_join(struct thread_task *task, double timeout, void **result)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	(void)timeout;
	(void)result;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task)
{
    if (task == NULL) return TPOOL_ERR_INVALID_ARGUMENT;
    // Реализация этой функции зависит от деталей реализации пула потоков и механизма отслеживания задач.
    // Вам нужно будет обеспечить, чтобы задача автоматически удалилась по завершении, что может потребовать изменений в логике выполнения задач.
    return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif
