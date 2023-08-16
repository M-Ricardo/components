


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>



#define MP_ALIGNMENT       		32
#define MP_PAGE_SIZE			4096
#define MP_MAX_ALLOC_FROM_POOL	(MP_PAGE_SIZE-1)

// 按指定的对齐方式 alignment 对齐。
#define mp_align(n, alignment) (((n)+(alignment-1)) & ~(alignment-1))
#define mp_align_ptr(p, alignment) (void *)((((size_t)p)+(alignment-1)) & ~(alignment-1))


// 大块内存结构
struct mp_large_s {
	struct mp_large_s *next;	// 指向下一个大块内存
	void *alloc;				// 指向实际分配的大块内存
};

// 小块内存结构
struct mp_node_s {

	unsigned char *last;		// 指向内存池中已分配结点的末尾，即下次可分配内存结点的首地址
	unsigned char *end;			// 指向内存池的末尾
	
	struct mp_node_s *next;		// 指向下一个结点
	size_t failed;				// 当前的内存池分配失败的次数
};

// 内存池结构
struct mp_pool_s {

	size_t max;					// 内存池可分配的最大空间，超过的话用大块内存

	struct mp_node_s *current;	// 指向当前内存池
	struct mp_large_s *large;	// 指向大块内存链表

	struct mp_node_s head[0];	// 指向小块内存链表

};

struct mp_pool_s *mp_create_pool(size_t size);
void mp_destory_pool(struct mp_pool_s *pool);
void *mp_alloc(struct mp_pool_s *pool, size_t size);
void *mp_nalloc(struct mp_pool_s *pool, size_t size);
void *mp_calloc(struct mp_pool_s *pool, size_t size);
void mp_free(struct mp_pool_s *pool, void *p);

// 创建内存池
struct mp_pool_s *mp_create_pool(size_t size) {

	struct mp_pool_s *p;
	// posix_memalign 用于分配内存块，并保证所分配的内存块以指定的对齐方式进行对齐。
	// 参数:1)一个指向指针的指针，用于保存分配得到的内存块的地址。2)所需的内存对齐方式. 3)要分配的内存块的大小
	// 一开始并不知道大块内存具体大小，因此只需要分配：申请空间的大小 + 内存池结构体大小 + 小块内存结点
	int ret = posix_memalign((void **)&p, MP_ALIGNMENT, size + sizeof(struct mp_pool_s) + sizeof(struct mp_node_s));
	if (ret) {
		return NULL;
	}
	
	p->max = (size < MP_MAX_ALLOC_FROM_POOL) ? size : MP_MAX_ALLOC_FROM_POOL;	// 设置小块内存可分配的最大空间
	p->current = p->head; 														// 设置当前指向的内存块
	p->large = NULL;															// 设置大块内存

	p->head->last = (unsigned char *)p + sizeof(struct mp_pool_s) + sizeof(struct mp_node_s);
	p->head->end = p->head->last + size;
	p->head->failed = 0;

	return p;

}

// 内存池销毁
void mp_destory_pool(struct mp_pool_s *pool) {

	struct mp_node_s *h, *n;
	struct mp_large_s *l;

	// 释放大块内存
	for (l = pool->large; l; l = l->next) {
		if (l->alloc) {
			free(l->alloc);
		}
	}

	// 释放小块内存
	h = pool->head->next;
	while (h) {
		n = h->next;
		free(h);
		h = n;
	}

	// 释放内存池
	free(pool);

}

// 重置内存池
void mp_reset_pool(struct mp_pool_s *pool) {

	struct mp_node_s *h;
	struct mp_large_s *l;

	// 释放大块内存
	for (l = pool->large; l; l = l->next) {
		if (l->alloc) {
			free(l->alloc);
		}
	}

	pool->large = NULL;

	// 通过指针复位重置小块内存（不调用free归还内存）
	for (h = pool->head; h; h = h->next) {
		h->last = (unsigned char *)h + sizeof(struct mp_node_s);
	}

}

// 分配小块内存
static void *mp_alloc_block(struct mp_pool_s *pool, size_t size) {

	unsigned char *m;
	struct mp_node_s *h = pool->head;
	// 第一个小块内存中，可分配的内存大小
	size_t psize = (size_t)(h->end - (unsigned char *)h);
	
	// 申请内存，大小与第一个小块内存一样
	int ret = posix_memalign((void **)&m, MP_ALIGNMENT, psize);
	if (ret) return NULL;

	struct mp_node_s *p, *new_node, *current;

	// 初始化新的内存块
	new_node = (struct mp_node_s*)m;
	new_node->end = m + psize;
	new_node->next = NULL;
	new_node->failed = 0;

	// 将指针m移动到可分配内存的开始位置
	m += sizeof(struct mp_node_s);
	m = mp_align_ptr(m, MP_ALIGNMENT);
	new_node->last = m + size;

	// 从当前指向的内存块开始，寻找最后一个内存块链表的结点
	current = pool->current;
	for (p = current; p->next; p = p->next) {
		// 若某个小块内存连续5次都分配失败，则跳过这个小块内存，下次不再遍历它
		if (p->failed++ > 4) { //
			current = p->next;
		}
	}

	// 将新创建的内存块，尾插到小内存块链表
	p->next = new_node;

	// 更新pool->current指针，判断 current 是否为空？
	// 若非空，指向current指向的结点；若为空，代表之前所有的内存块都分配失败，则指向新的内存块
	pool->current = current ? current : new_node;

	return m;

}

// 分配大块内存
static void *mp_alloc_large(struct mp_pool_s *pool, size_t size) {

	void *p = malloc(size);
	if (p == NULL) return NULL;

	size_t n = 0;
	struct mp_large_s *large;

	// 遍历大块内存链表，找到可以挂载 p 的位置
	for (large = pool->large; large; large = large->next) {
		// 找到可以挂载的地方
		if (large->alloc == NULL) {
			large->alloc = p;
			return p;
		}
		// 若连续 4 次都没找到，就不再寻找了
		if (n ++ > 3) break;
	}

	// 创建一个新的大块内存结点，用来挂载p
	large = mp_alloc(pool, sizeof(struct mp_large_s));
	if (large == NULL) {
		free(p);
		return NULL;
	}

	// 将新创建的结点，头插到大块内存链表中
	large->alloc = p;
	large->next = pool->large;
	pool->large = large;

	return p;
}

// 对齐方式分配内存池
void *mp_memalign(struct mp_pool_s *pool, size_t size, size_t alignment) {

	void *p;
	
	int ret = posix_memalign(&p, alignment, size);
	if (ret) {
		return NULL;
	}

	struct mp_large_s *large = mp_alloc(pool, sizeof(struct mp_large_s));
	if (large == NULL) {
		free(p);
		return NULL;
	}

	large->alloc = p;
	large->next = pool->large;
	pool->large = large;

	return p;
}


// 非对齐方式分配内存池
void *mp_alloc(struct mp_pool_s *pool, size_t size) {

	unsigned char *m;
	struct mp_node_s *p;

	// 1、申请小块内存
	if (size <= pool->max) {

		p = pool->current;

		do {
			// 遍历小块内存链表，寻找可用的空间分配内存
			m = mp_align_ptr(p->last, MP_ALIGNMENT);
			// 若当前结点的剩余空间足够分配
			if ((size_t)(p->end - m) >= size) {
				p->last = m + size;
				return m;
			}
			// 若当前内存块的剩余内存小于所需内存，则到下一个内存块中寻找
			p = p->next;
		} while (p);

		// 没找到合适的小块内存，则申请小块内存
		return mp_alloc_block(pool, size);
	}

	// 2、申请大块内存
	return mp_alloc_large(pool, size);
	
}

// 非对齐分配内存池
void *mp_nalloc(struct mp_pool_s *pool, size_t size) {

	unsigned char *m;
	struct mp_node_s *p;

	if (size <= pool->max) {
		p = pool->current;

		do {
			m = p->last;
			if ((size_t)(p->end - m) >= size) {
				p->last = m+size;
				return m;
			}
			p = p->next;
		} while (p);

		return mp_alloc_block(pool, size);
	}

	return mp_alloc_large(pool, size);
	
}

// 分配并清零内存空间。
void *mp_calloc(struct mp_pool_s *pool, size_t size) {
	// 在给定的内存池中进行内存分配
	void *p = mp_alloc(pool, size);
	if (p) {
		//将分配到的内存块清零
		memset(p, 0, size);
	}

	return p;
	
}

// 内存池释放，只针对大块内存
void mp_free(struct mp_pool_s *pool, void *p) {

	struct mp_large_s *l;
	for (l = pool->large; l; l = l->next) {
		if (p == l->alloc) {
			free(l->alloc);
			l->alloc = NULL;
			return ;
		}
	}
	
}


int main(int argc, char *argv[]) {

	int size = 1 << 12;

	struct mp_pool_s *p = mp_create_pool(size);

	int i = 0;
	for (i = 0;i < 10;i ++) {

		void *mp = mp_alloc(p, 512);
	}

	printf("mp_create_pool: %ld\n", p->max);
	printf("mp_align(123, 32): %d, mp_align(17, 32): %d\n", mp_align(24, 32), mp_align(17, 32));

	int j = 0;
	for (i = 0;i < 5;i ++) {

		char *pp = mp_calloc(p, 32);
		for (j = 0;j < 32;j ++) {
			if (pp[j]) {
				printf("calloc wrong\n");
			}
			printf("calloc success\n");
		}
	}

	printf("mp_reset_pool\n");
	for (i = 0;i < 5;i ++) {
		void *l = mp_alloc(p, 8192);
		mp_free(p, l);
	}
	mp_reset_pool(p);

	printf("mp_destory_pool\n");
	for (i = 0;i < 58;i ++) {
		mp_alloc(p, 256);
	}

	mp_destory_pool(p);

	return 0;

}
