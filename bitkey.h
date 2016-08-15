/*
 * contrib/zcurve/bitkey.h
 *
 *
 * bitkey.h -- spatial key operations
 *		
 *
 * Modified by Boris Muratshin, mailto:bmuratshin@gmail.com
 */
#ifndef __ZCURVE_BITKEY_H
#define __ZCURVE_BITKEY_H

	typedef struct bitKey_s bitKey_t;
	typedef struct zkey_vtab_s {
		int(*f_cmp) (const bitKey_t *, const bitKey_t *);
		bool(*f_between) (const bitKey_t *val, const bitKey_t *minval, const bitKey_t *maxval);
		int(*f_getBit) (const bitKey_t *pk, int idx);
		void(*f_clearKey) (bitKey_t *pk);
		void(*f_setLowBits) (bitKey_t *pk, int idx);
		void(*f_clearLowBits) (bitKey_t *pk, int idx);
		void(*f_fromLong) (bitKey_t *pk, Datum numeric);
		Datum(*f_toLong) (const bitKey_t *pk);
		void(*f_fromCoords) (bitKey_t *pk, const uint32 *coords, int n);
		void(*f_toCoords) (const bitKey_t *pk, uint32 *coords, int n);
		void(*f_toStr) (const bitKey_t *pk, char *buf, int buflen);
	} zkey_vtab_t;


	/* key stuff ------------------------------------------------------------------------------- */
#define ZKEY_BUFLEN_BY_WORDS64 3
#define ZKEY_MAX_COORDS 6
	typedef struct bitKey_s {
		zkey_vtab_t 	*vtab_;
		uint64 		vals_[ZKEY_BUFLEN_BY_WORDS64];
	} bitKey_t;


	extern void  bitKey_CTOR(bitKey_t *pk, int ncoords);
	extern int   bitKey_cmp(const bitKey_t *, const bitKey_t *);
	extern bool  bitKey_between(const bitKey_t *val, const bitKey_t *minval, const bitKey_t *maxval);
	extern int   bitKey_getBit(const bitKey_t *pk, int idx);
	extern void  bitKey_clearKey(bitKey_t *pk);
	extern void  bitKey_setLowBits(bitKey_t *pk, int idx);
	extern void  bitKey_clearLowBits(bitKey_t *pk, int idx);
	extern void  bitKey_fromLong(bitKey_t *pk, Datum dt);
	extern Datum bitKey_toLong(const bitKey_t *pk);
	extern void  bitKey_fromCoords(bitKey_t *pk, const uint32 *coords, int n);
	extern void  bitKey_toCoords(const bitKey_t *pk, uint32 *coords, int n);
	extern void  bitKey_toStr(const bitKey_t *pk, char *buf, int buflen);


#endif /* __ZCURVE_BITKEY_H */