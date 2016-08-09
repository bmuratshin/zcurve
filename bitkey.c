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
#include "utils/numeric.h"
#include "utils/builtins.h"
#include "bitkey.h"



static uint32 stoBits[8] = {
	0x0001, 0x0002, 0x0004, 0x0008, 
	0x0010, 0x0020, 0x0040, 0x0080};

static int 
bit2Key_cmp (const bitKey_t *pl, const bitKey_t *pr)
{
	Assert(pl && pr);
	return (pl->vals_[0] == pr->vals_[0])? 0 :
			((pl->vals_[0] > pr->vals_[0]) ? 1 : -1);
}

static bool  
bit2Key_between (const bitKey_t *ckey, const bitKey_t *lKey, const bitKey_t *hKey)
{
	/* bit over bit */
  	uint64 bitMask = 0xAAAAAAAAAAAAAAAAULL;
	int i;
	Assert(val && minval && maxval);

	/* by X & Y */
	for(i = 0; i < 2; i++, bitMask >>= 1)
	{
		/* current coordinate */
		uint64 tmpK = ckey->vals_[0] & bitMask;
		/* diapason High and Low coordinates */
		uint64 tmpL = lKey->vals_[0] & bitMask;
		uint64 tmpH = hKey->vals_[0] & bitMask;

		if (tmpK < tmpL)
			return 0;
		if (tmpK > tmpH)
			return 0;
	}
	/* OK, return true */
	return 1;
}

static int 
bit2Key_getBit (const bitKey_t *pk, int idx)
{
	Assert(NULL != pk);
	return (int)(pk->vals_[0] >> (idx & 0x3f));
}

static void 
bit2Key_clearKey (bitKey_t *pk)
{
	Assert(NULL != pk);
	pk->vals_[0] = 0;
}

static void 
bit2Key_fromCoords (bitKey_t *pk, const uint32 *coords, int n)
{
	int curmask = 0xf, i;
	unsigned char *ptr = NULL;
	uint32 ix = coords[0];
	uint32 iy = coords[1];
	Assert(NULL != pk && NULL != coords && n >= 2);

	pk->vals_[0] = 0;
	ptr = (unsigned char *)&pk->vals_[0];
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

static void 
bit2Key_toCoords (const bitKey_t *pk, uint32 *coords, int n)
{
 	unsigned char *ptr = NULL;
	uint32 ix = 0;
	uint32 iy = 0;
	int i;
	Assert(NULL != pk && NULL != coords && n >= 2);
 	ptr = (unsigned char *)&pk->vals_[0];
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
	coords[0] = ix;
	coords[1] = iy;
}

static void 
bit2Key_setLowBits(bitKey_t *pk, int idx)
{
	uint64 bitMask = 0xAAAAAAAAAAAAAAAALL >> (63 - idx);
	uint64 bit = ((uint64) 1) << ((uint64) (idx & 0x3f));
	Assert(NULL != pk);
	pk->vals_[0] |= bitMask;
	pk->vals_[0] -= bit;
}

static void 
bit2Key_clearLowBits(bitKey_t *pk, int idx)
{
	uint64 bitMask = 0xAAAAAAAAAAAAAAAALL >> (63 - idx);
	uint64 bit = ((uint64) 1) << ((uint64) (idx & 0x3f));
	Assert(NULL != pk);
	pk->vals_[0] &= ~bitMask;
	pk->vals_[0] |= bit;
}

static void 
bit2Key_fromLong (bitKey_t *pk, Datum dt) 
{
	Assert(NULL != pk);
	pk->vals_[0] = DatumGetInt64(DirectFunctionCall1(numeric_int8, dt));
}

static Datum
bit2Key_toLong (const bitKey_t *pk) 
{
	Datum nm = DirectFunctionCall1(int8_numeric, Int64GetDatum(pk->vals_[0]));
	return nm;
}


static zkey_vtab_t key2_vtab_ = {
	bit2Key_cmp,
	bit2Key_between,
	bit2Key_getBit,
	bit2Key_clearKey,
	bit2Key_setLowBits,
	bit2Key_clearLowBits,
	bit2Key_fromLong,
	bit2Key_toLong,
	bit2Key_fromCoords,
	bit2Key_toCoords,
};

void  bitKey_CTOR2 (bitKey_t *pk)
{
	Assert(pk);
	memset(pk->vals_, 0, sizeof(pk->vals_));
	pk->vtab_ = &key2_vtab_;
}

/*------------------------------------------------------------------------------------*/
int   bitKey_cmp (const bitKey_t *l, const bitKey_t *r)
{
	Assert(l && r);
	Assert(l->vtab_ == r->vtab_);
	return l->vtab_->f_cmp(l, r);
}

bool  bitKey_between (const bitKey_t *val, const bitKey_t *minval, const bitKey_t *maxval)
{
	Assert(val && minval && maxval);
	return val->vtab_->f_between(val, minval, maxval);
}

int   bitKey_getBit (const bitKey_t *pk, int idx)
{
	Assert(pk);
	return pk->vtab_->f_getBit(pk, idx);
}

void  bitKey_clearKey (bitKey_t *pk)
{
	Assert(pk);
	return pk->vtab_->f_clearKey(pk);
}

void  bitKey_setLowBits (bitKey_t *pk, int idx)
{
	Assert(pk);
	return pk->vtab_->f_setLowBits(pk, idx);
}

void  bitKey_clearLowBits (bitKey_t *pk, int idx)
{
	Assert(pk);
	return pk->vtab_->f_clearLowBits(pk, idx);
}

void  bitKey_fromLong (bitKey_t *pk, Datum numeric)
{
	Assert(pk && numeric);
	return pk->vtab_->f_fromLong(pk, numeric);
}

Datum bitKey_toLong (const bitKey_t *pk)
{
	Assert(pk);
	return pk->vtab_->f_toLong(pk);
}

void  bitKey_fromCoords (bitKey_t *pk, const uint32 *coords, int n)
{
	Assert(pk && coords);
	return pk->vtab_->f_fromCoords(pk, coords, n);
}

void  bitKey_toCoords (const bitKey_t *pk, uint32 *coords, int n)
{
	Assert(pk && coords);
	return pk->vtab_->f_toCoords(pk, coords, n);
}


