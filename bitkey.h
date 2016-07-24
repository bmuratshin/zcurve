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

/* point 2d key stuff ------------------------------------------------------------------------------- */
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

#endif /* __ZCURVE_BITKEY_H */