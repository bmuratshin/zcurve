/*
 * contrib/zcurve/sp_query_2d.h
 *
 *
 * sp_query_2d.h -- 2d spatial lookup stuff
 *		
 *
 * Modified by Boris Muratshin, mailto:bmuratshin@gmail.com
 */
#ifndef __ZCURVE_SPATIAL_INDEX_2D_H
#define __ZCURVE_SPATIAL_INDEX_2D_H

#include "bitkey.h"
#include "sp_tree_2d.h"

/* subquery definition */
typedef struct spatial2Query_s {
	bit2Key_t lowKey_;	/* the begining of index interval */
	bit2Key_t highKey_;	/* the end of index interval */
	int curBitNum_;		/* the number of key bit that will be used to split this one to subqueries (if necessary, sure) */
	struct spatial2Query_s *prevQuery_; 	/* pointer to subqueries queue */
} spatial2Query_t;

/* top level spatial query definition */
typedef struct spt_query2_s {
	uint32 minX_, minY_, maxX_, maxY_;	/* lookup extent, debug only */

	spatial2Query_t *queryHead_;		/* subqueries queue */
	spatial2Query_t *freeHead_;		/* finished subqueries are reused */

	zcurve_scan_ctx_t qctx_;		/* low level cursor context */

	bit2Key_t currentKey_;			/* cursor position value, initially, left bottom corner of lookup extent */
	bit2Key_t lastKey_;			/* the max value for currently executed subquery, initially, right upper corner of lookup extent */

	bool subQueryFinished_;			/* automata state flag */
	ItemPointerData iptr_;			/* temporarily stored current t_tid */
} spt_query2_t;

/* constructor */
extern void spt_query2_CTOR (spt_query2_t *ps, Relation rel, uint32 minx, uint32 miny, uint32 maxx, uint32 maxy);

/* destructor */
extern void spt_query2_DTOR (spt_query2_t *ps);




/* public interface -------------------------------------- */

/* spatial cursor start, returns not 0 in case of cuccess, resulting data in x,y,iptr */
extern int  spt_query2_moveFirst(spt_query2_t *q, uint32 *x, uint32 * y, ItemPointerData *iptr);

/* main loop iteration, returns not 0 in case of cuccess, resulting data in x,y,iptr */
extern int  spt_query2_moveNext(spt_query2_t *q, uint32 *x, uint32 * y, ItemPointerData *iptr);



/* private interface -------------------------------------- */

/* if freeHead_ is not empty gets memory there or just palloc some */
extern spatial2Query_t *spt_query2_createQuery (spt_query2_t *q);

/* closes index tree cursor */
extern void spt_query2_closeQuery(spt_query2_t *q);

/* push subquery to the reuse list */
extern void spt_query2_freeQuery(spt_query2_t *q, spatial2Query_t *);

/* signels automata what subquery on the top of queue as finished and pops it out */
extern void spt_query2_releaseSubQuery(spt_query2_t *q);

/* performs index cursor lookup for start_val */
extern int spt_query2_queryFind(spt_query2_t *q, uint64 start_val);

/* moves cursor forward */
extern int spt_query2_queryNextKey(spt_query2_t *q);

/*
  gets an subquery from queue, 
  split it if necessary till the full satisfaction and then 
  test for an appropriate data
*/
extern int spt_query2_findNextMatch(spt_query2_t *q, uint32 *x, uint32 * y, ItemPointerData *iptr);

/* split current cursor value back to x & y and check it complies to query extent */
extern int spt_query2_checkKey(spt_query2_t *q, uint32 *x, uint32 * y);

/* 
  If cursor points not to the end of page just return OK.
  When yes, we need to test the begining of the next page if it is equal to the end of subquery diapason.
  If so, we should continue on the next page.
  Note, subquery MUST end at the last page item's value.
*/
extern int spt_query2_checkNextPage(spt_query2_t *q);
    

#endif /* __ZCURVE_SPATIAL_INDEX_2D_H */