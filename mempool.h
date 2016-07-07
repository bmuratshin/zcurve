#ifndef __MEMPOOL_H
#define __MEMPOOL_H

	struct mem_block_s
	{
		struct mem_block_s *mb_next;
		int			mb_fill;
		int			mb_size;
	};
	typedef struct mem_block_s mem_block_t;

	struct mem_pool_s {
		mem_block_t *mp_first;
		size_t total_bytes_;
		size_t total_pages_;
	};
	typedef struct mem_pool_s mem_pool_t;

	extern void mp_CTOR(mem_pool_t *mp);
	extern void mp_DTOR(mem_pool_t *mp);

	extern int  mp_reset(mem_pool_t *mp);
	extern size_t mp_size(mem_pool_t *mp);
	extern void  *mp_alloc(mem_pool_t *mp, size_t len1);

#endif /* __MEMPOOL_H */