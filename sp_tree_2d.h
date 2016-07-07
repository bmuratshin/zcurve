#ifndef __SP_TREE_2D_H
#define __SP_TREE_2D_H


/*
 *	PostgreSQL definitions for ZCurve traits
 *
 *	contrib/zcurve/zcurve.c
 *
 */
extern uint64 zcurve_fromXY (uint32 ix, uint32 iy);
extern void zcurve_toXY (uint64 al, uint32 *px, uint32 *py);

extern BTStack zcurve_search(Relation rel, int keysz, ScanKey scankey, bool nextkey, Buffer *bufP, int access);
extern OffsetNumber zcurve_binsrch(Relation rel, Buffer buf, int keysz, ScanKey scankey, bool nextkey);
extern int32 zcurve_compare(Relation rel, int keysz, ScanKey scankey, Page page, OffsetNumber offnum);
extern void trace_page(Relation rel, ScanKey skey, Page page);

struct zcurve_scan_ctx_s {
	Relation 	rel_;
	uint64		init_zv_;
	Buffer		buf_;
	OffsetNumber	offset_;
	OffsetNumber	max_offset_;
	ScanKeyData 	skey_;

	uint64		cur_val_;	
	uint64		last_page_val_;	
};
typedef struct zcurve_scan_ctx_s zcurve_scan_ctx_t;
extern int zcurve_scan_move_first(zcurve_scan_ctx_t *ctx, uint64 start_val);
extern int zcurve_scan_move_next(zcurve_scan_ctx_t *ctx);
extern int zcurve_scan_ctx_CTOR(zcurve_scan_ctx_t *ctx, Relation rel, uint64 start_val);
extern int zcurve_scan_ctx_DTOR(zcurve_scan_ctx_t *ctx);
extern int zcurve_scan_ctx_is_opened(zcurve_scan_ctx_t *ctx);


extern int trace_tree_by_val(Relation relation, uint64 zv);


#endif  /*__SP_TREE_2D_H*/
