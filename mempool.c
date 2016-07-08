#include "postgres.h"
#include "mempool.h"

#ifdef __ALLOC
#undef __ALLOC
#endif
#ifdef __FREE
#undef __FREE
#endif

static const int mp_block_size = (8192);
#define __ALLOC(a) (/*test_*/palloc(a))
#define __FREE(a, b) (pfree (a))


size_t mp_size(mem_pool_t *mp)  
{ 
	Assert(mp);
	return mp->total_pages_ * mp_block_size; 
}

void
mp_CTOR(mem_pool_t *mp) 
{
	Assert(mp);
	mp->mp_first = NULL;
	mp->total_bytes_ = 0;
	mp->total_pages_ = 0;
}

void
mp_DTOR(mem_pool_t *mp)
{
	Assert(mp);
	mp_reset(mp);
}

int 
mp_reset(mem_pool_t *mp)
{
	mem_block_t * mb = NULL, *next;

	Assert(mp);
	mb = mp->mp_first;
	while (mb)
	{
		next = mb->mb_next;
		__FREE(mb, mb->mb_size);
		mb = next;
	}
	mp->mp_first = NULL;
	mp->total_bytes_ = 0;
	mp->total_pages_ = 0;
	return 0;
}

static size_t 
__align(size_t len)
{
	const size_t al = sizeof(size_t);
	const size_t sh = (al == 4)? 2 : 3;
	return ((((len) + al - 1) >> sh) << sh);
}

void *
mp_alloc(mem_pool_t *mp, size_t len1)
{
	unsigned char *ptr;
	size_t len = __align(len1);
	mem_block_t * mb = NULL;
	size_t hlen = __align((sizeof(mem_block_t))); /* we can have a doubles so structure also must be aligned */
	mem_block_t * f = NULL;

	Assert(mp);
	f = mp->mp_first;

	if (!f || f->mb_size - f->mb_fill < (int)len)
	{
		if (len > mp_block_size - hlen)
		{
			mb = (mem_block_t *) __ALLOC(hlen + len);
			if (NULL == mb)
			{
				elog(ERROR, "No more mem");
				return NULL;
			}
			mb->mb_size = len + hlen;
			mb->mb_fill = hlen;
			if (f)
			{
				mb->mb_next = f->mb_next;
				f->mb_next = mb;
			}
			else
			{
				mb->mb_next = NULL;
				mp->mp_first = mb;
			}
		}
		else
		{
			mb = (mem_block_t *) __ALLOC(mp_block_size);
			if (NULL == mb)
			{
				elog(ERROR, "No more mem");
				return NULL;
			}
			mb->mb_size = mp_block_size;
			mb->mb_fill = hlen;
			mb->mb_next = mp->mp_first;
			mp->mp_first = mb;
			mp->total_pages_++;
		}
	}
	else
	{
		mb = f;
	}
	ptr = ((unsigned char*)mb) + mb->mb_fill;
	mb->mb_fill += len;
	mp->total_bytes_ += len;
	return (ptr);
}

