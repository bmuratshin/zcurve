#include "postgres.h"

#include <math.h>
#include <string.h>
#include "postgres.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "utils/rel.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/fmgroids.h"
#include "catalog/namespace.h"
#include "access/nbtree.h"
#include "storage/bufpage.h"

#include "sp_tree_2d.h"
#include "spatialIndex_2d.h"


static uint32 stoBits[8] = {
	0x0001, 0x0002, 0x0004, 0x0008, 
	0x0010, 0x0020, 0x0040, 0x0080};

int64 bit2Key_getBit (bit2Key_t *pk, int idx)
{
	Assert(NULL != pk);
	return (int64)(pk->val_>>(idx&0x3f));
}

void bit2Key_clearKey (bit2Key_t *pk)
{
	Assert(NULL != pk);
	pk->val_ = 0;
}

void 
bit2Key_fromXY (bit2Key_t *pk, uint32 ix, uint32 iy)
{
	int curmask = 0xf, i;
	unsigned char *ptr = NULL;

	Assert(NULL != pk);
	pk->val_ = 0;
	ptr = (unsigned char *)&pk->val_;
	for (i = 0; i < 8; i++)
	{
		int xp = (ix & curmask) >> (i<<2);
		int yp = (iy & curmask) >> (i<<2);
		int tmp = (xp & stoBits[0]) | ((yp & stoBits[0])<<1) |
			((xp & stoBits[1])<<1) | ((yp & stoBits[1])<<2) |
			((xp & stoBits[2])<<2) | ((yp & stoBits[2])<<3) |
			((xp & stoBits[3])<<3) | ((yp & stoBits[3])<<4);
		curmask <<= 4;
		ptr[i] = (unsigned char)tmp;
	}
}

void 
bit2Key_toXY (bit2Key_t *pk, uint32 *px, uint32 *py)
{
 	unsigned char *ptr = NULL;
	uint32 ix = 0;
	uint32 iy = 0;
	int i;

	Assert(NULL != pk);
	Assert(NULL != px);
	Assert(NULL != py);
 	ptr = (unsigned char *)&pk->val_;
	for (i = 0; i < 8; i++)
	{
		int tmp = ptr[i];
		int tmpx = (tmp & stoBits[0]) + 
			((tmp & stoBits[2])>>1) + 
			((tmp & stoBits[4])>>2) + 
			((tmp & stoBits[6])>>3);
		int tmpy = ((tmp & stoBits[1])>>1) + 
			((tmp & stoBits[3])>>2) + 
			((tmp & stoBits[5])>>3) + 
			((tmp & stoBits[7])>>4);
		ix |= tmpx << (i << 2);
		iy |= tmpy << (i << 2);
	}
	*px = ix;
	*py = iy;
}

void 
bit2Key_setLowBits(bit2Key_t *pk, int idx)
{
	uint64 bitMask = 0xAAAAAAAAAAAAAAAALL >> (63-idx);
	uint64 bit = ((uint64) 1) << ((uint64) (idx & 0x3f));
	Assert(NULL != pk);
	pk->val_ |= bitMask;
	pk->val_ -= bit;
}

void 
bit2Key_clearLowBits(bit2Key_t *pk, int idx)
{
	uint64 bitMask = 0xAAAAAAAAAAAAAAAALL >> (63-idx);
	uint64 bit = ((uint64) (1)) << ((uint64) (idx & 0x3f));
	Assert(NULL != pk);
	pk->val_ &= ~bitMask;
	pk->val_ |= bit;
}

void 
bit2Key_fromLong (bit2Key_t *pk, const uint64 *buffer) 
{
	Assert(NULL != pk);
	pk->val_ = *buffer;
}

void 
bit2Key_toLong (const bit2Key_t *pk, uint64 *buffer) 
{
	Assert(NULL != pk);
	*buffer = pk->val_;
}

/*-----------------------------------------------------------------------*/

void 
spt_query2_def_t_CTOR (spt_query2_def_t *ps, Relation rel, uint32 minx, uint32 miny, uint32 maxx, uint32 maxy)
{
	Assert(NULL != ps);
	ps->queryHead = NULL;
	ps->freeHead = NULL;
	ps->minX_ = minx;
	ps->minY_ = miny;
	ps->maxX_ = maxx;
	ps->maxY_ = maxy;
	/*mp_CTOR(&ps->memAlloc);*/
	zcurve_scan_ctx_CTOR(&ps->qctx_, rel, 0);
}

void 
spt_query2_def_t_DTOR (spt_query2_def_t *ps)
{
	Assert(NULL != ps);
	zcurve_scan_ctx_DTOR(&ps->qctx_);
	/*mp_DTOR(&ps->memAlloc);*/
}


spatial2Query_t *
pointSpatial2d_createQuery(spt_query2_def_t *q)
{
	Assert(q);
	if(q->freeHead)
	{
		spatial2Query_t *retval = q->freeHead;
		q->freeHead = q->freeHead->prevQuery;
		return retval;
	}
	return (spatial2Query_t *)palloc(sizeof(spatial2Query_t));/*mp_alloc (&(q->memAlloc), sizeof(spatial2Query_t));*/
}

void 
pointSpatial2d_freeQuery (spt_query2_def_t *q, spatial2Query_t *sq)
{
	Assert(q);
	sq->prevQuery = q->freeHead;
	q->freeHead = sq;
}

void 
pointSpatial2d_closeQuery(spt_query2_def_t *q)
{
	Assert(q);
	if (zcurve_scan_ctx_is_opened(&q->qctx_))
	{
		q->queryHead = q->freeHead = NULL;
		/*mp_reset(&q->memAlloc);*/
		zcurve_scan_ctx_DTOR(&q->qctx_);
	}
}

void 
pointSpatial2d_releaseSubQuery(spt_query2_def_t *q)
{
	Assert(q);
	if(q->queryHead)
	{
		/*uint32 x0,y0;
		uint32 x1,y1;
		bit2Key_toXY(&q->queryHead->lowKey, &x0, &y0);
		bit2Key_toXY(&q->queryHead->highKey, &x1, &y1);
		elog(INFO, "F[%d %d %d %d]",x0, y0, x1, y1);*/

		spatial2Query_t *prevQuery = q->queryHead->prevQuery;
		pointSpatial2d_freeQuery(q, q->queryHead);
		q->queryHead = prevQuery;
		q->subQueryFinished = 1;
	}
}


void 
pointSpatial2d_setSpatialQuery(spt_query2_def_t *q)
{
	/*elog(INFO, "setSpatialQuery[%d %d .. %d %d]", q->minX_, q->minY_, q->maxX_, q->maxY_);*/
	q->queryHead = pointSpatial2d_createQuery (q);
	q->queryHead->prevQuery = NULL;
	q->queryHead->curBitNum = 63;

	bit2Key_fromXY(&q->queryHead->lowKey, q->minX_, q->minY_);
	bit2Key_fromXY(&q->queryHead->highKey, q->maxX_, q->maxY_);
	/*elog(INFO, "setSpatialQuery(%lx, %lx)", q->queryHead->lowKey.val_, q->queryHead->highKey.val_);*/
}

int
pointSpatial2d_moveFirst(spt_query2_def_t *q, uint32 *x, uint32 * y, ItemPointerData *iptr)
{
	pointSpatial2d_setSpatialQuery (q);
	return pointSpatial2d_findNextMatch(q, x, y, iptr);
}

int
pointSpatial2d_moveNext (spt_query2_def_t *q, uint32 *x, uint32 * y, ItemPointerData *iptr)
{
	/*finished*/
	if (!zcurve_scan_ctx_is_opened(&q->qctx_))
		return 0;
	if (q->subQueryFinished)
    		return pointSpatial2d_findNextMatch(q, x, y, iptr);

	if(!pointSpatial2d_checkNextPage(q))
	{
		pointSpatial2d_releaseSubQuery(q);
		return pointSpatial2d_findNextMatch(q, x, y, iptr);
	}

	if (!pointSpatial2d_queryNextKey(q))
	{
		pointSpatial2d_closeQuery(q);
		return 0;
	}

	/*elog(INFO, "moveNext (%lx %lx %lx)", q->currentKey.val_, q->lastKey.val_, q->queryHead->highKey.val_);*/
	while (q->currentKey.val_ <= q->queryHead->highKey.val_)
	{
		if (pointSpatial2d_checkKey(q, x, y))
		{
			*iptr = q->iptr_;
			return 1;
		}
		if (!pointSpatial2d_checkNextPage(q))
			break;

		if (!pointSpatial2d_queryNextKey(q))
		{
			/*elog(INFO, "moveNext closeQuery(%lx %lx %lx)", q->currentKey.val_, q->lastKey.val_, q->queryHead->highKey.val_);*/
			pointSpatial2d_closeQuery(q);
			return 0;
		}
		/*elog(INFO, "moveNext 2(%lx %lx %lx)", q->currentKey.val_, q->lastKey.val_, q->queryHead->highKey.val_);*/
	}
	pointSpatial2d_releaseSubQuery(q);
	return pointSpatial2d_findNextMatch(q, x, y, iptr);
}


int
pointSpatial2d_checkNextPage(spt_query2_def_t *q)
{
	if (q->qctx_.offset_ == q->qctx_.max_offset_)
	{
		if (!zcurve_scan_try_move_next(&q->qctx_, q->queryHead->highKey.val_))
			return 0;
		if (q->qctx_.cur_val_ > q->queryHead->highKey.val_)
			return 0;
		/*elog(INFO, "checkNextPage (%lx %lx)", q->qctx_.cur_val_, q->queryHead->highKey.val_);*/
		return 1;
	}
	return 1;
}


int
pointSpatial2d_findNextMatch(spt_query2_def_t *q, uint32 *x, uint32 * y, ItemPointerData *iptr)
{
	Assert(q);
	while(q->queryHead)
	{
		/*elog(INFO, "1)[%lx]", q->queryHead->lowKey.val_);*/
		q->subQueryFinished = 0;
		if(!pointSpatial2d_queryFind(q, q->queryHead->lowKey.val_))
		{
			pointSpatial2d_closeQuery (q);
			return 0;
		}

		/*elog(INFO, "findNextMatch(%lx %lx %lx)", q->currentKey.val_, q->lastKey.val_, q->queryHead->highKey.val_);*/
		while (q->lastKey.val_ < q->queryHead->highKey.val_)
		{
			/*split query*/
			spatial2Query_t *subQuery = NULL;
			while ( bit2Key_getBit(&q->queryHead->lowKey, q->queryHead->curBitNum) == 
				bit2Key_getBit(&q->queryHead->highKey, q->queryHead->curBitNum))
			{
				q->queryHead->curBitNum--;
			}
			/*elog(INFO, "findNextMatch(curbit=%d)", q->queryHead->curBitNum);*/

			subQuery = pointSpatial2d_createQuery (q);
			subQuery->prevQuery = q->queryHead;
			subQuery->lowKey = q->queryHead->lowKey;
			subQuery->highKey = q->queryHead->highKey;
			bit2Key_setLowBits(&subQuery->highKey, q->queryHead->curBitNum);
			bit2Key_clearLowBits(&q->queryHead->lowKey, q->queryHead->curBitNum);
			subQuery->curBitNum = --q->queryHead->curBitNum;

			/*{
				uint32 x0,y0;
				uint32 x1,y1;

				elog(INFO, "[%lx %lx]", q->queryHead->lowKey.val_, q->queryHead->highKey.val_);
				bit2Key_toXY(&q->queryHead->lowKey, &x0, &y0);
				bit2Key_toXY(&q->queryHead->highKey, &x1, &y1);
				elog(INFO, "Q[%d %d %d %d]",x0, y0, x1, y1);

				elog(INFO, "[%lx %lx]", subQuery->lowKey.val_, subQuery->highKey.val_);
				bit2Key_toXY(&subQuery->lowKey, &x0, &y0);
				bit2Key_toXY(&subQuery->highKey, &x1, &y1);
				elog(INFO, "Q[%d %d %d %d]",x0, y0, x1, y1);
			}*/

			q->queryHead = subQuery;
			q->subQueryFinished = 0;
		}
		/*elog(INFO, "findNextMatch 2(%lx %lx)", q->currentKey.val_, q->lastKey.val_);*/
        	while (q->currentKey.val_ <= q->queryHead->highKey.val_)
		{
			if (pointSpatial2d_checkKey(q, x, y))
			{
				*iptr = q->iptr_;
				return 1;
			}
			if (!pointSpatial2d_checkNextPage(q))
				break;
			if (!pointSpatial2d_queryNextKey(q))
			{
				pointSpatial2d_closeQuery(q);
                		return 0;
			}
		}
		pointSpatial2d_releaseSubQuery(q);
	}
	pointSpatial2d_closeQuery(q);
	return 0;
};

int
pointSpatial2d_checkKey (spt_query2_def_t *q, uint32 *x, uint32 * y)
{
  	uint64 bitMask = 0xAAAAAAAAAAAAAAAAULL;
	bit2Key_t *lKey = &q->queryHead->lowKey;
	bit2Key_t *hKey = &q->queryHead->highKey;
	int i;

	for(i = 0; i < 2; i++, bitMask >>= 1)
	{
		uint64 tmpK = q->currentKey.val_ & bitMask;
		uint64 tmpL = lKey->val_ & bitMask;
		uint64 tmpH = hKey->val_ & bitMask;

		if (tmpK < tmpL)
			return 0;
		if (tmpK > tmpH)
			return 0;
	}
	Assert(x);
	Assert(y);
	bit2Key_toXY (&q->currentKey, x, y);

	/*elog(INFO, "\t\tYes: %d %d %lx", *x, *y, q->currentKey.val_);*/
	return 1;
}


int
pointSpatial2d_queryFind (spt_query2_def_t *q, uint64 start_val)
{
	int ret = zcurve_scan_move_first(&q->qctx_, start_val);
	Assert(q);
	q->currentKey.val_ = q->qctx_.cur_val_;
	q->lastKey.val_ = q->qctx_.last_page_val_;
	q->iptr_ = q->qctx_.iptr_;
	/*elog(INFO, "Found: %lx %lx %lx", start_val, q->currentKey.val_, q->lastKey.val_);*/
	return ret;
}

int
pointSpatial2d_queryNextKey (spt_query2_def_t *q)
{
	int ret = zcurve_scan_move_next(&q->qctx_);
	Assert(q);
	q->currentKey.val_ = q->qctx_.cur_val_;
	q->lastKey.val_ = q->qctx_.last_page_val_;
	q->iptr_ = q->qctx_.iptr_;
	/*elog(INFO, "queryNextKey: %lx %lx", q->currentKey.val_, q->lastKey.val_);*/
	return ret;
}

