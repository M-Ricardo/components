
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include "thrd_pool.h"
#include "spinlock.h"

typedef struct spinlock spinlock_t;

//任务队列的结点
typedef struct task_s {
    void *next; //指向下一个任务的指针
    handler_pt func; //任务的执行函数
    void *arg;      //任务的上下文
} task_t;

//任务队列
//默认是阻塞类型的队列，谁来取任务，如果此时队列为空，谁应该阻塞休眠
typedef struct task_queue_s {
    void *head;     //头指针
    void **tail;    //指向队尾的指针的指针
    int block;      //设置当前是否阻塞类型
    spinlock_t lock;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} task_queue_t;

//线程池
struct thrdpool_s {
    task_queue_t *task_queue;
    atomic_int quit;       //标志是否让线程退出，原子操作
    int thrd_count;        //线程的数量
    pthread_t *thread;
};


//创建任务队列
static task_queue_t * __taskqueue_create() {
    task_queue_t *queue = (task_queue_t *)malloc(sizeof(task_queue_t));
    if(!queue) return NULL;

    int ret;
    ret = pthread_mutex_init(&queue->mutex, NULL);
    //若初始化成功
    if (ret == 0){ 
        ret = pthread_cond_init(&queue->cond, NULL);
        if (ret == 0){
            spinlock_init(&queue->lock);
            queue->head = NULL;
            queue->tail = &queue->head;
            queue->block = 1;   //设置为阻塞
            return queue;
        }
        pthread_cond_destroy(&queue->cond);
    }

    //若初始化失败
    pthread_mutex_destroy(&queue->mutex);
    return NULL;
}

static void __nonblock(task_queue_t *queue){
    pthread_mutex_lock(&queue->mutex);
    queue->block = 0;
    pthread_mutex_unlock(&queue->mutex);
    pthread_cond_broadcast(&queue->cond);
}


//插入新的任务task
static inline void __add_task(task_queue_t *queue, void *task){
    void **link = (void **)task;
    *link = NULL;       //等价于task->next = NULL

    spinlock_lock(&queue->lock);
    *queue->tail = link;    //将最后一个结点的next指向task
    queue->tail = link;     //更新尾指针
    spinlock_unlock(&queue->lock);

    //生产了新的任务，通知消费者
    pthread_cond_signal(&queue->cond);
}

//取出结点（先进先出）
static inline void *__pop_task(task_queue_t *queue){
    spinlock_lock(&queue->lock);
    if (queue->head == NULL){
        spinlock_unlock(&queue->lock);
        return NULL;
    }
    task_t *task;
    task = queue->head;
    queue->head = task->next;
    if (queue->head == NULL){
        queue->tail = &queue->head;
    }
    spinlock_unlock(&queue->lock);
    return task;
}

// 消费者线程取出任务
static inline void *__get_task(task_queue_t *queue){
    task_t *task;

    /*虚假唤醒：当把一个线程唤醒之后，但是其他消费者线程提前把这个任务取走了。
    此时__pop_task(queue)为 NULL。
    因此，需要用while循环进行判断，如果任务队列为NULL，继续休眠*/
    //若队列为空，休眠
    while ((task = __pop_task(queue)) == NULL){
        pthread_mutex_lock(&queue->mutex);
        if (queue->block == 0){
            pthread_mutex_unlock(&queue->mutex);
            return NULL;
        }
        /*
        1. unlock(&mutex)，让出执行权
        2. 在cond处休眠
        3. 当生产者产生任务，发送信号signal
        4. 在cond处唤醒
        5. lonck(&mutx)，接管执行权
        */
        pthread_cond_wait(&queue->cond, &queue->mutex);
        pthread_mutex_unlock(&queue->mutex);
    }

    return task;
}

//销毁任务队列
static void __taskqueue_destory(task_queue_t *queue){
    task_t *task;
    while ((task = __pop_task(queue))){
        free(task);
    }
    spinlock_destroy(&queue->lock);
    pthread_cond_destroy(&queue->cond);
    pthread_mutex_destroy(&queue->mutex);
    free(queue);
}

//消费者线程的工作——取出任务，执行任务
static void *__thrdpoll_worker(void *arg){
    thrdpool_t *pool = (thrdpool_t *)arg;
    task_t *task;
    void *ctx;

    while (atomic_load(&pool->quit) == 0 ){
        task = (task_t *)__get_task(pool->task_queue);
        if (!task) break;
        handler_pt func = task->func;
        ctx = task->arg;
        free(task);
        func(ctx);
    }

    return NULL;
}

//停止
static void __threads_terminate(thrdpool_t *pool){
    atomic_store(&pool->quit, 1);
    /*默认情况下，线程池中的线程在执行任务时可能会阻塞等待任务的到来。
    如果在线程池退出时，仍有线程处于等待任务的阻塞状态，那么这些线程将无法在有新任务时重新启动并执行。
    调用 __nonblock 函数可以将任务队列设置为非阻塞状态，确保所有线程都能正确地退出，
    而不会被阻塞在等待任务的状态中。*/
    __nonblock(pool->task_queue);
    int i;
    for (i=0; i<pool->thrd_count; i++){
        pthread_join(pool->thread[i], NULL); //阻塞调用它的线程，直到指定的线程结束执行。
    }
}

//创建线程池
static int __threads_create(thrdpool_t *pool, size_t thrd_count){
    pthread_attr_t attr;
    int ret;
    ret = pthread_attr_init(&attr);
    if (ret == 0){
        pool->thread = (pthread_t *)malloc(sizeof(pthread_t) * thrd_count);
        if (pool->thread){
            int i = 0;
            for (i=0; i < thrd_count; i++){
                if (pthread_create(&pool->thread[i], &attr, __thrdpoll_worker, pool) != 0){
                    break;
                }
            }
            pool->thrd_count = i;
            pthread_attr_destroy(&attr);

            if (i == thrd_count)
                return 0;
            
            //如果实际创建的线程数 ！= thrd_count，停止当前创建的线程
            __threads_terminate(pool);
            free(pool->thread);
        }
        ret = -1;
    }
    return ret;
}

//停止线程-------用户调用的接口
void thrdpool_terminate(thrdpool_t * pool) {
    atomic_store(&pool->quit, 1);
    __nonblock(pool->task_queue);
}

//创建线程-------用户调用的接口
thrdpool_t *thrdpool_create(int thrd_count){
    thrdpool_t *pool;
    pool = (thrdpool_t*)malloc(sizeof(thrdpool_t));
    if (!pool) return NULL;

    //创建任务队列
    task_queue_t *queue = __taskqueue_create();
    if (queue){
        pool->task_queue = queue;
        atomic_init(&pool->quit, 0);
        if(__threads_create(pool, thrd_count) == 0){
            return pool;
        }
        __taskqueue_destory(pool->task_queue);
    }
    free(pool);
    return NULL;
    
}


//生产者抛出任务到线程池(加到任务队列)
int thrdpool_post(thrdpool_t *pool, handler_pt func, void *arg){
    if (atomic_load(&pool->quit) == 1){
        return -1;
    }
    task_t *task = (task_t *)malloc(sizeof(task_t));
    if (!task) return -1;
    task->func = func;
    task->arg = arg;

    __add_task(pool->task_queue, task);
    return 0;
}

//等待线程池中的所有线程完成任务，并清理线程池的资源
void thrdpool_waitdone(thrdpool_t *pool){
    int i;
    for (i=0; i<pool->thrd_count; i++){
        pthread_join(pool->thread[i], NULL);
    }
    __taskqueue_destory(pool->task_queue);
    free(pool->thread);
    free(pool);
}

