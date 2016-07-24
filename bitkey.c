/*
 * contrib/zcurve/bitkey.c
 *
 *
 * bitkey.c -- spatial key operations
 *		
 *
 * Modified by Boris Muratshin, mailto:bmuratshin@gmail.com
 */
#include "postgres.h"
#include "bitkey.h"


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
