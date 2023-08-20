
// build: gcc -o deadlock deadlock.c -lpthread -ldl


#define _GNU_SOURCE
#include <dlfcn.h>


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include <stdint.h>


//·········································有向图的结构和基本操作接口····························································
#if 1 	// 图结构


typedef unsigned long int uint64;


#define MAX		100

enum Type {PROCESS, RESOURCE};

// 顶点资源信息
struct source_type {

	uint64 id;			// 拥有该资源的线程 id
	enum Type type;		// 顶点类型：线程 or 资源
	uint64 lock_id;		// 资源（锁） id
	int degress;	    // 资源的出度，该资源被多少顶点（线程）申请
};

// 顶点信息
struct vertex {

	struct source_type s;	// 顶点资源信息
	struct vertex *next;	// 指向下一个顶点（邻接表）

};

// 图结构
struct task_graph {

	struct vertex list[MAX];			// 存储顶点于数组中
	int num;							// 顶点的数量

	struct source_type locklist[MAX];	// 存储所有顶点资源（锁）
	int lockidx; 						// 资源（锁）的数量

	pthread_mutex_t mutex;
};

struct task_graph *tg = NULL;
int path[MAX+1];
int visited[MAX];
int k = 0;
int deadlock = 0;	// 死锁标记

// 创建顶点
struct vertex *create_vertex(struct source_type type) {

	struct vertex *tex = (struct vertex *)malloc(sizeof(struct vertex ));

	tex->s = type;
	tex->next = NULL;

	return tex;

}

// 寻找与资源对应的顶点所在的下标
int search_vertex(struct source_type type) {

	int i = 0;

	for (i = 0;i < tg->num;i ++) {

		if (tg->list[i].s.type == type.type && tg->list[i].s.id == type.id) {
			return i;
		}

	}

	return -1;
}

// 增加节点
void add_vertex(struct source_type type) {

	if (search_vertex(type) == -1) {

		tg->list[tg->num].s = type;
		tg->list[tg->num].next = NULL;
		tg->num ++;

	}

}

// 增加一条边
int add_edge(struct source_type from, struct source_type to) {

	add_vertex(from);
	add_vertex(to);

	struct vertex *v = &(tg->list[search_vertex(from)]);

	while (v->next != NULL) {
		v = v->next;
	}

	v->next = create_vertex(to);

}

// 检查资源i和j之间是否存在边
int verify_edge(struct source_type i, struct source_type j) {

	if (tg->num == 0) return 0;

	// 寻找资源i对应的结点下标
	int idx = search_vertex(i);
	if (idx == -1) {
		return 0;
	}

	struct vertex *v = &(tg->list[idx]);

	while (v != NULL) {

		if (v->s.id == j.id) return 1;

		v = v->next;
		
	}

	return 0;

}

// 删除资源from到to之间的边
int remove_edge(struct source_type from, struct source_type to) {

	int idxi = search_vertex(from);
	int idxj = search_vertex(to);

	if (idxi != -1 && idxj != -1) {

		struct vertex *v = &tg->list[idxi];
		struct vertex *remove;

		while (v->next != NULL) {

			if (v->next->s.id == to.id) {

				remove = v->next;
				v->next = v->next->next;

				free(remove);
				break;

			}

			v = v->next;
		}

	}

}

// 打印环  cycle : 1 --> 2 --> 3 --> 4 --> 2
void print_deadlock(void) {

	int i = 0;

	printf("deadlock : ");
	for (i = 0;i < k-1;i ++) {

		printf("%ld --> ", tg->list[path[i]].s.id);

	}

	printf("%ld\n", tg->list[path[i]].s.id);

}

int DFS(int idx) {

	struct vertex *ver = &tg->list[idx];
	// 如果当前结点已经访问过，说明存在环
	if (visited[idx] == 1) {

		path[k++] = idx;
		print_deadlock();
		deadlock = 1;
		
		return 0;
	}

	// 否则将标记置为1，代表访问了，继续DFS
	visited[idx] = 1;
	path[k++] = idx;

	while (ver->next != NULL) {

		DFS(search_vertex(ver->next->s));
		k --;
		
		ver = ver->next;

	}

	return 1;

}

//从list[idx]的顶点开始，检测是否存在环
int search_for_cycle(int idx) {

	struct vertex *ver = &tg->list[idx];
	visited[idx] = 1;	// 标记idx处顶点被访问过
	k = 0;		
	path[k++] = idx;	// 记录起点

	while (ver->next != NULL) {

		// 把visited数组的开始到idx-1处的值都置为0
		int i = 0;
		for (i = 0;i < tg->num;i ++) {
			if (i == idx) continue;
			
			visited[i] = 0;
		}

		// 把path数组的第i到MAX处的值都置为-1
		for (i = 1;i <= MAX;i ++) {
			path[i] = -1;
		}
		k = 1;

		DFS(search_vertex(ver->next->s));
		ver = ver->next;
	}

}


#endif

// 



#if 1

// 搜索锁lock是否在资源列表中，若有则返回下标位置
int search_lock(uint64 lock) {

	int i = 0;
	
	for (i = 0;i < tg->lockidx;i ++) {
		//如果lock存在就返回它在锁列表中的下标位置
		if (tg->locklist[i].lock_id == lock) {
			return i;
		}
	}

	return -1;
}

// 寻找资源列表中的一个空位置
int search_empty_lock(uint64 lock) {

	int i = 0;
	
	for (i = 0;i < tg->lockidx;i ++) {
		
		if (tg->locklist[i].lock_id == 0) {
			return i;
		}
	}

	return tg->lockidx;

}


/*
获取资源（锁）前，检测该资源是否被其他线程占用。
1. 如果被占用，则创建一条资源申请边，表示当前进程正在向拥有资源的线程申请该资源。
2. 如果没有被占用，则跳过。
*/
void lock_before(uint64_t tid, uint64_t lockaddr) {
	/*
	1. 	if (lockaddr) {
			tid --> lockaddr.tid;
	   	}
	*/

	int idx = 0;

	for (idx = 0;idx < tg->lockidx;idx ++) {

		// 1. 如果被占用
		if (tg->locklist[idx].lock_id == lockaddr) { 
			
			// 创建申请该资源的顶点（PROCESS类型）
			struct source_type from;
			from.id = tid;
			from.type = PROCESS;
			add_vertex(from);

			// 创建拥有该资源的顶点（PROCESS类型)
			struct source_type to;
			to.id = tg->locklist[idx].id;
			to.type = PROCESS;
			add_vertex(to);

			// 申请该资源的结点数量+1
			tg->locklist[idx].degress ++;

			// 如果两个顶点间不存在资源申请边，增加一条边
			if (!verify_edge(from, to))
				add_edge(from, to);

		}

	}
	
	
}


/*
线程获取资源（锁）后，检查该资源是否存在（资源链表中是否存在）
1. 若该资源之前不存在，添加资源到资源链表中
2. 若该资源已经存在，则移除自己对该资源的申请边，表示请求已经得到满足
*/
void lock_after(uint64_t tid, uint64_t lockaddr) {

	/*
		if (!lockaddr) {

			tid --> lockaddr;

		} else {

			lockaddr.tid = tid;
			tid -> lockaddr;

		}
		
	 */
	int idx = 0;
	// 1. 若该资源（锁）不存在，添加资源到资源链表中
	if (-1 == (idx = search_lock(lockaddr))) {// 

		// 寻找资源链表中空闲的位置并添加该资源
		int eidx = search_empty_lock(lockaddr);

		tg->locklist[eidx].id = tid;
		tg->locklist[eidx].lock_id = lockaddr;

		// 资源数量+1
		tg->lockidx ++;
		
	} 
	else {	// 2. 该资源（锁）存在，需要移除自己的请求边

		// 申请该资源的顶点（PROCESS类型）
		struct source_type from;
		from.id = tid;
		from.type = PROCESS;
		add_vertex(from);

		// 拥有该资源的顶点（PROCESS类型）
		struct source_type to;
		to.id = tg->locklist[idx].id;
		to.type = PROCESS;
		add_vertex(to);

		// 申请该资源的顶点数-1
		tg->locklist[idx].degress --;

		// 如果存在该资源申请边，则删除
		if (verify_edge(from, to))
			remove_edge(from, to);

		// 线程占用该资源（锁）
		tg->locklist[idx].id = tid;
		
	}
	 
	
}

// 线程释放该资源后，检查该资源是否还被线程申请，没有则将其从资源链表中移除。
void unlock_after(uint64_t tid, uint64_t lockaddr) {

	// lockaddr.tid = 0;
	// 此锁已被unlock
	int idx = search_lock(lockaddr);

	// 若该资源没有线程申请，则将其从资源链表中移除
	if (tg->locklist[idx].degress == 0) {
		tg->locklist[idx].id = 0;
		tg->locklist[idx].lock_id = 0;
	}
	
}

// ····································死锁检测的线程·········································

// 检测死锁
void check_dead_lock(void) {

	int i = 0;

	deadlock = 0;
	for (i = 0;i < tg->num;i ++) {
		if (deadlock == 1) break;
		// 检测是否存在环，即死锁
		search_for_cycle(i);
	}

	if (deadlock == 0) {
		printf("no deadlock\n");
	}

}

// 检测死锁的线程
static void *thread_routine(void *args) {

	while (1) {

		sleep(5);	//每隔5秒检测一次
		check_dead_lock();

	}

}

// 开启死锁检测
void start_check(void) {

	tg = (struct task_graph*)malloc(sizeof(struct task_graph));
	tg->num = 0;
	tg->lockidx = 0;
	
	pthread_t tid;

	// 创建检测死锁的线程
	pthread_create(&tid, NULL, thread_routine, NULL);

}
// ··························································································

// ·······································hook··············································
// 1.定义原类型
typedef int (*pthread_mutex_lock_t)(pthread_mutex_t *mutex);
pthread_mutex_lock_t pthread_mutex_lock_f = NULL;

typedef int (*pthread_mutex_unlock_t)(pthread_mutex_t *mutex);
pthread_mutex_unlock_t pthread_mutex_unlock_f = NULL;

// 2.实现
int pthread_mutex_lock(pthread_mutex_t *mutex) {

	
	pthread_t selfid = pthread_self();
	
	// 获取锁之前，检测这个锁是否被占用。
	// 有的话就创建资源申请边，没有的话就跳过
	lock_before((uint64_t)selfid, (uint64_t)mutex);
	
	pthread_mutex_lock_f(mutex);

	// 获取锁之后，检测这个锁是否存在与资源列表中。
	// 存在的话，移除自己对这个锁的申请边，表示请求得到满足了；不存在的话，把这个锁加入资源列表中。
	lock_after((uint64_t)selfid, (uint64_t)mutex);

	
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {

	pthread_mutex_unlock_f(mutex);

	pthread_t selfid = pthread_self();
	unlock_after((uint64_t)selfid, (uint64_t)mutex);
	
}

// 3. 初始化
void init_hook(void) {

	if (!pthread_mutex_lock_f)
		pthread_mutex_lock_f = dlsym(RTLD_NEXT, "pthread_mutex_lock");

	if (!pthread_mutex_unlock_f)
		pthread_mutex_unlock_f = dlsym(RTLD_NEXT, "pthread_mutex_unlock");
	
}

// ··························································································

#endif

// 

#if 1//sample

pthread_mutex_t r1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t r2 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t r3 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t r4 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t r5 = PTHREAD_MUTEX_INITIALIZER;



void *t1_cb(void *arg) {
	pthread_t selfid = pthread_self(); 
	printf("thread_routine 1 : %ld \n", selfid);

	pthread_mutex_lock(&r1);
	sleep(1);	// 休眠，允许期间其他线程能运行
	pthread_mutex_lock(&r2);
	
	pthread_mutex_unlock(&r2);
	pthread_mutex_unlock(&r1);

}

void *t2_cb(void *arg) {
	pthread_t selfid = pthread_self(); 
	printf("thread_routine 2 : %ld \n", selfid);

	pthread_mutex_lock(&r2);
	sleep(1);
	pthread_mutex_lock(&r3);

	pthread_mutex_unlock(&r3);
	pthread_mutex_unlock(&r2);

}

void *t3_cb(void *arg) {
	pthread_t selfid = pthread_self(); 
	printf("thread_routine 3 : %ld \n", selfid);
	
	pthread_mutex_lock(&r3);
	sleep(1);
	pthread_mutex_lock(&r4);

	pthread_mutex_unlock(&r4);
	pthread_mutex_unlock(&r3);

}

void *t4_cb(void *arg) {
	pthread_t selfid = pthread_self(); 
	printf("thread_routine 4 : %ld \n", selfid);
	
	pthread_mutex_lock(&r4);
	sleep(1);
	pthread_mutex_lock(&r5);

	pthread_mutex_unlock(&r5);
	pthread_mutex_unlock(&r4);

}

void *t5_cb(void *arg) {
	pthread_t selfid = pthread_self(); 
	printf("thread_routine 5 : %ld \n", selfid);
	
	pthread_mutex_lock(&r5);
	sleep(1);
	pthread_mutex_lock(&r1);

	pthread_mutex_unlock(&r1);
	pthread_mutex_unlock(&r5);

}




// deadlock
// 

int main() {

	init_hook();

	// 启动检测死锁的线程
	start_check();
	printf("start_check\n");

	pthread_t t1, t2, t3, t4, t5;

	pthread_create(&t1, NULL, t1_cb, NULL);
	pthread_create(&t2, NULL, t2_cb, NULL);
	pthread_create(&t3, NULL, t3_cb, NULL);
	pthread_create(&t4, NULL, t4_cb, NULL);
	pthread_create(&t5, NULL, t5_cb, NULL);


	pthread_join(t1, NULL);
	pthread_join(t2, NULL);
	pthread_join(t3, NULL);
	pthread_join(t4, NULL);
	pthread_join(t5, NULL);

	printf("complete\n");

}

#endif


