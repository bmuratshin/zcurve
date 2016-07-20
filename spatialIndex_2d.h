#ifndef __SPATIAL_INDEX_2D_H
#define __SPATIAL_INDEX_2D_H

#include "sp_tree_2d.h"

/* bit2Key stuff ------------------------------------------------------------------------------- */
struct bit2Key_s {
	uint64 val_;
};
typedef struct bit2Key_s bit2Key_t;

extern int64 bit2Key_getBit (bit2Key_t *pk, int idx);
extern void  bit2Key_clearKey (bit2Key_t *pk);
extern void  bit2Key_setLowBits (bit2Key_t *pk, int idx);
extern void  bit2Key_clearLowBits (bit2Key_t *pk, int idx);
extern void  bit2Key_fromLong (bit2Key_t *pk, const uint64 *buffer);
extern void  bit2Key_toLong (const bit2Key_t *pk, uint64 *buffer);
extern void  bit2Key_fromXY (bit2Key_t *pk, uint32 x, uint32 y);
extern void  bit2Key_toXY (bit2Key_t *pk, uint32 *x, uint32 *y);


/* spatial2Query stuff ------------------------------------------------------------------------------- */

struct spatial2Query_s {
	bit2Key_t lowKey;
	bit2Key_t highKey;
	int curBitNum;
	struct spatial2Query_s *prevQuery;
};
typedef struct spatial2Query_s spatial2Query_t;

struct spt_query2_def_s {
	uint32 minX_, minY_, maxX_, maxY_;

	/*mem_pool_t memAlloc;*/

	spatial2Query_t *queryHead;
	spatial2Query_t *freeHead;

	bit2Key_t currentKey;
	bit2Key_t lastKey;

	zcurve_scan_ctx_t qctx_;
	bool subQueryFinished;

	ItemPointerData iptr_;
};
typedef struct spt_query2_def_s spt_query2_def_t;

extern void spt_query2_def_t_CTOR (spt_query2_def_t *ps, Relation rel, uint32 minx, uint32 miny, uint32 maxx, uint32 maxy);
extern void spt_query2_def_t_DTOR (spt_query2_def_t *ps);


/* BTree2dPointSpatial stuff ------------------------------------------------------------------------------- */

extern int  pointSpatial2d_moveFirst(spt_query2_def_t *q, uint32 *x, uint32 * y, ItemPointerData *iptr);
extern int  pointSpatial2d_moveNext(spt_query2_def_t *q, uint32 *x, uint32 * y, ItemPointerData *iptr);

/*private*/
extern void pointSpatial2d_closeQuery(spt_query2_def_t *q);
extern spatial2Query_t *pointSpatial2d_createQuery (spt_query2_def_t *q);
extern void pointSpatial2d_freeQuery (spt_query2_def_t *q, spatial2Query_t *);
extern void pointSpatial2d_releaseSubQuery (spt_query2_def_t *q);
extern void pointSpatial2d_setSpatialQuery (spt_query2_def_t *q);

extern int  pointSpatial2d_queryFind (spt_query2_def_t *q, uint64 start_val);
extern int  pointSpatial2d_queryNextKey (spt_query2_def_t *q);
extern int  pointSpatial2d_findNextMatch (spt_query2_def_t *q, uint32 *x, uint32 * y, ItemPointerData *iptr);
extern int  pointSpatial2d_checkKey (spt_query2_def_t *q, uint32 *x, uint32 * y);
extern int  pointSpatial2d_checkNextPage(spt_query2_def_t *q);
    


#endif /* __SPATIAL_INDEX_2D_H */