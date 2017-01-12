/*
 * Copyright (c) 2016...2017, Alex Artyushin, Boris Muratshin
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * The names of the authors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * contrib/zcurve/bitkey.c
 *
 *
 * bitkey.c -- spatial key operations
 *		
 *
 * Author:	Boris Muratshin, mailto:bmuratshin@gmail.com
 *
 */

#include "postgres.h"
#include "utils/numeric.h"
#include "utils/builtins.h"
#include "bitkey.h"
#include "hilbert2.h"

#define WITH_HACKED_NUMERIC
#ifdef WITH_HACKED_NUMERIC
#include "ex_numeric.h"
#endif

/* 2D -------------------------------------------------------------------------------------------------------- */

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
	Assert(ckey && lKey && hKey);

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
	uint64 bitMask = 0xAAAAAAAAAAAAAAAAULL >> (63 - idx);
	uint64 bit = ((uint64) 1) << ((uint64) (idx & 0x3f));
	Assert(NULL != pk);
	pk->vals_[0] |= bitMask;
	pk->vals_[0] -= bit;
}

static void 
bit2Key_clearLowBits(bitKey_t *pk, int idx)
{
	uint64 bitMask = 0xAAAAAAAAAAAAAAAAULL >> (63 - idx);
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

static void  
bit2Key_toStr(const bitKey_t *pk, char *buf, int buflen)
{
	uint32 coords[2];
	bit2Key_toCoords (pk, coords, 2);
	Assert(pk && buf && buflen > 128);
	sprintf(buf, "[%x %x]: %d %d", 
		(int)((pk->vals_[0] >> 32) & 0xffffffff),
		(int)(pk->vals_[0] & 0xffffffff),
		(int)coords[0],
		(int)coords[1]);
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
	bit2Key_toStr,
};

static void  bitKey_CTOR2 (bitKey_t *pk)
{
	Assert(pk);
	memset(pk->vals_, 0, sizeof(pk->vals_));
	pk->vtab_ = &key2_vtab_;
}


/* 3D -------------------------------------------------------------------------------------------------------- */

static int
bit3Key_cmp(const bitKey_t *pl, const bitKey_t *pr)
{
	Assert(pl && pr);
	if ((pl->vals_[0] != pr->vals_[0]))
		return ((pl->vals_[0] > pr->vals_[0]) ? 1 : -1);
	if ((pl->vals_[1] != pr->vals_[1]))
		return ((pl->vals_[1] > pr->vals_[1]) ? 1 : -1);
	return 0;
}

static bool
bit3Key_between(const bitKey_t *ckey, const bitKey_t *lKey, const bitKey_t *hKey)
{
	/* bit over bit */
	uint64 bitMask = 0x0000924924924924ULL;
	int i;
	Assert(ckey && lKey && hKey);

	/* by X & Y & Z*/
	for (i = 0; i < 3; i++, bitMask >>= 1)
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

		tmpK = ((ckey->vals_[0] >> 48) | (ckey->vals_[1] << 16)) & bitMask;
		/* diapason High and Low coordinates */
		tmpL = ((lKey->vals_[0] >> 48) | (lKey->vals_[1] << 16)) & bitMask;
		tmpH = ((hKey->vals_[0] >> 48) | (hKey->vals_[1] << 16)) & bitMask;

		if (tmpK < tmpL)
			return 0;
		if (tmpK > tmpH)
			return 0;
	}
	/* OK, return true */
	return 1;
}

static int
bit3Key_getBit(const bitKey_t *pk, int idx)
{
	int ix0 = idx >> 6; 
	int ix1 = idx & 0x3f;
	Assert(NULL != pk && idx < 96 && idx >= 0);
	return (int)(pk->vals_[ix0] >> ix1);
}

static void
bit3Key_clearKey(bitKey_t *pk)
{
	Assert(NULL != pk);
	pk->vals_[0] = 0;
	pk->vals_[1] = 0;
}

static const uint64 smasks[3] = {
	0x9249249249249249ULL,
	0x2492492492492492ULL,
	0x4924924924924924ULL,
};

static void
bit3Key_setLowBits(bitKey_t *pk, int idx)
{
	Assert(NULL != pk && idx < 96 && idx >= 0);
	if (idx >= 64)
	{
		unsigned lidx = (idx - 64) & 0x3ff;
		pk->vals_[1] |= (smasks[0] >> (63 - lidx));
		pk->vals_[1] -= (1ULL << lidx);
		pk->vals_[0] |= smasks[idx % 3];
	}
	else
	{
		pk->vals_[0] |= (smasks[0] >> (63 - idx));
		pk->vals_[0] -= (1ULL << idx);
	}
}

static void
bit3Key_clearLowBits(bitKey_t *pk, int idx)
{
	Assert(NULL != pk && idx < 96 && idx >= 0);
	if (idx >= 64)
	{
		unsigned lidx = (idx - 64) & 0x3ff;
		pk->vals_[1] &= ~(smasks[0] >> (63 - lidx));
		pk->vals_[1] |= (1ULL << lidx);
		pk->vals_[0] &= ~smasks[idx % 3];
	}
	else
	{
		pk->vals_[0] &= ~(smasks[0] >> (63 - idx));
		pk->vals_[0] |= (1ULL << idx);
	}
}

#ifdef WITH_HACKED_NUMERIC
static void test_numeric(const Numeric pvar, uint32_t *xdata)
{
	const int n = NUMERIC_NDIGITS(pvar);
	int i,j;
	int16_t *sdata = NUMERIC_DIGITS(pvar);

	/*elog(INFO, "NDIGITS=%d WEIGHT=%d DSCALE=%d SIGN=%d", (int)NUMERIC_NDIGITS(pvar), (int)NUMERIC_WEIGHT(pvar), (int)NUMERIC_DSCALE(pvar), (int)NUMERIC_SIGN(pvar));*/

	xdata[0] = xdata[1] = xdata[2] = xdata[3] = 0;
	for (i = 0; i < n; i++)
	{
		int64_t ch = sdata[i];
		for (j = 0; j < 4; j++)
		{
			int64_t val = ((int64_t)xdata[j]) * 10000;
			val += ch;
			xdata[j] = val & 0xffffffff;
			ch = val >> 32;
		}
	}
}
#endif

static void
bit3Key_fromLong(bitKey_t *pk, Datum dt)
{
#ifdef WITH_HACKED_NUMERIC
	uint32_t 	xdata[8];
	const int n = NUMERIC_NDIGITS(DatumGetNumeric(dt));

	Assert(NULL != pk);
	if (n < 5)
	{
		pk->vals_[0] = DatumGetInt64(DirectFunctionCall1(numeric_int8, dt));
		pk->vals_[1] = 0;
		return;
	}

	test_numeric(DatumGetNumeric(dt), xdata);
	pk->vals_[0] = (((uint64_t)xdata[1]) << 32) + xdata[0];
	pk->vals_[1] = (((uint64_t)xdata[3]) << 32) + xdata[2];
	return;
#else
	Datum		divisor_numeric;
	Datum		divisor_int64;
	Datum		low_result;
	Datum		upper_result;

	divisor_int64 = Int64GetDatum((int64) (1ULL << 48));
	divisor_numeric = DirectFunctionCall1(int8_numeric, divisor_int64);

	low_result = DirectFunctionCall2(numeric_mod, dt, divisor_numeric);
	upper_result = DirectFunctionCall2(numeric_div_trunc, dt, divisor_numeric);
	pk->vals_[0] = DatumGetInt64(DirectFunctionCall1(numeric_int8, low_result));
	pk->vals_[1] = DatumGetInt64(DirectFunctionCall1(numeric_int8, upper_result));
	pk->vals_[0] |= (pk->vals_[1] & 0xffff) << 48;
	pk->vals_[1] >>= 16;
#endif
#if 0
	if ((pk->vals_[0] & 0xffffffff) != xdata[0])
	{
		elog(ERROR, "bitKey <%lx %lx %lx vs %x %x %x>", pk->vals_[0], pk->vals_[1], pk->vals_[2], xdata[0], xdata[1], xdata[2]);
	}
#endif
}

static Datum
bit3Key_toLong(const bitKey_t *pk)
{
	uint64 lo = pk->vals_[0] & 0xffffffffffff;
	uint64 hi = (pk->vals_[0] >> 48) | (pk->vals_[1] << 16);
	uint64 mul = 1ULL << 48;
	Datum  low_result = DirectFunctionCall1(int8_numeric, Int64GetDatum(lo));
	Datum  upper_result = DirectFunctionCall1(int8_numeric, Int64GetDatum(hi));
	Datum  mul_result = DirectFunctionCall1(int8_numeric, Int64GetDatum(mul));
	Datum nm = DirectFunctionCall2(numeric_mul, mul_result, upper_result);
	return  DirectFunctionCall2(numeric_add, nm, low_result);
}



static uint32 key3ToBits[16] = {
	0, 1, 1 << 3, 1 | (1 << 3),
	(1 << 6), (1 << 6) | 1, (1 << 6) | (1 << 3), 1 | (1 << 3) | (1 << 6),
	(1 << 9), (1 << 9) | 1, (1 << 9) | (1 << 3), 1 | (1 << 3) | (1 << 9),
	(1 << 6) | (1 << 9), (1 << 6) | (1 << 9) | 1, (1 << 9) | (1 << 6) | (1 << 3), 1 | (1 << 3) | (1 << 6) | (1 << 9),
};

static void
bit3Key_fromCoords(bitKey_t *pk, const uint32 *coords, int n)
{
	uint32 x = coords[0];
	uint32 y = coords[1];
	uint32 z = coords[2];
	int ix0, ix1, i;

	Assert(pk && coords && n >= 3);
	pk->vals_[0] = pk->vals_[1] = 0;
	for (i = 0; i < 8; i++)
	{
		uint64 tmp =
			(key3ToBits[x & 0xf] << 2) |
			(key3ToBits[y & 0xf] << 1) |
			(key3ToBits[z & 0xf]);
		ix0 = (i * 12);
		pk->vals_[0] |= tmp << ix0;
		ix1 = ix0 + 12;
		pk->vals_[1] |= (ix1 >> 6) * (ix0 >= 64 ?
			(tmp << (ix0 - 64)) :
			(tmp >> (64 - ix0)));
		x >>= 4; y >>= 4; z >>= 4;
	}
}

static void
bit3Key_toCoords(const bitKey_t *pk, uint32 *coords, int n)
{
	int i;
	uint64 vals[2] = { pk->vals_[0], pk->vals_[1] };
	uint32 x = 0, y = 0, z = 0;

	Assert(pk && coords && n >= 3);
	/* 8 X 4 */
	for (i = 0; i < 8; i++)
	{
		uint32 tmp = vals[0] & 0xfff;
		uint32 tmpz = (tmp & 1) +
			((tmp & (1 << 3)) >> 2) +
			((tmp & (1 << 6)) >> 4) +
			((tmp & (1 << 9)) >> 6);

		uint32 tmpy = ((tmp & (1 << 1)) >> 1) +
			((tmp & (1 << 4)) >> 3) +
			((tmp & (1 << 7)) >> 5) +
			((tmp & (1 << 10)) >> 7);

		uint32 tmpx = ((tmp & (1 << 2)) >> 2) +
			((tmp & (1 << 5)) >> 4) +
			((tmp & (1 << 8)) >> 6) +
			((tmp & (1 << 11)) >> 8);

		x |= tmpx << (i << 2);
		y |= tmpy << (i << 2);
		z |= tmpz << (i << 2);

		vals[0] >>= 12;
		vals[0] |= (0xfff & vals[1]) << (64 - 12);
		vals[1] >>= 12;
	}
	coords[0] = x;
	coords[1] = y;
	coords[2] = z;
}

static void  
bit3Key_toStr(const bitKey_t *pk, char *buf, int buflen)
{
	uint32 coords[3];
	bit2Key_toCoords (pk, coords, 3);
	Assert(pk && buf && buflen > 128);
	sprintf(buf, "[%x %x %x]: %d %d %d", 
		(int)(pk->vals_[1] & 0xffffffff),
		(int)((pk->vals_[0] >> 32) & 0xffffffff),
		(int)(pk->vals_[0] & 0xffffffff),
		(int)coords[0],
		(int)coords[1],
		(int)coords[2]);
}



static zkey_vtab_t key3_vtab_ = {
	bit3Key_cmp,
	bit3Key_between,
	bit3Key_getBit,
	bit3Key_clearKey,
	bit3Key_setLowBits,
	bit3Key_clearLowBits,
	bit3Key_fromLong,
	bit3Key_toLong,
	bit3Key_fromCoords,
	bit3Key_toCoords,
	bit3Key_toStr,
};

static void  bitKey_CTOR3(bitKey_t *pk)
{
	Assert(pk);
	memset(pk->vals_, 0, sizeof(pk->vals_));
	pk->vtab_ = &key3_vtab_;
}


/*-- Hilbert2D ----------------------------------------------------------------------------------*/

static void 
hilb2Key_fromCoords (bitKey_t *pk, const uint32 *coords, int n)
{
	uint32 res[2] = {0,0};
	Assert(NULL != pk && NULL != coords && n >= 2);
	hilbert_c2i(2, 30, coords, res);
	pk->vals_[0] = res[0] + (((uint64)res[1]) << 32);
}

static void 
hilb2Key_toCoords (const bitKey_t *pk, uint32 *coords, int n)
{
	uint32 res[2] = {
		(uint32)pk->vals_[0],
		(uint32)(pk->vals_[1] >> 32)};
	Assert(NULL != pk && NULL != coords && n >= 2);
	hilbert_i2c(2, 30, res, coords);
}

static zkey_vtab_t hilb2_vtab_ = {
	bit2Key_cmp,
	bit2Key_between,
	bit2Key_getBit,
	bit2Key_clearKey,
	bit2Key_setLowBits,
	bit2Key_clearLowBits,
	bit2Key_fromLong,
	bit2Key_toLong,
	hilb2Key_fromCoords,
	hilb2Key_toCoords,
	bit2Key_toStr,
};

static void  hilbKey_CTOR2(bitKey_t *pk)
{
	Assert(pk);
	memset(pk->vals_, 0, sizeof(pk->vals_));
	pk->vtab_ = &hilb2_vtab_;
}

/*-- Hilbert3D ----------------------------------------------------------------------------------*/
static void 
hilb3Key_fromCoords (bitKey_t *pk, const uint32 *coords, int n)
{
	uint32 res[3] = {0,0,0};
	Assert(NULL != pk && NULL != coords && n >= 3);
	hilbert_c2i(3, 30, coords, res);
	pk->vals_[0] = res[0] + (((uint64)res[1]) << 32);
	pk->vals_[1] = res[2];
}

static void 
hilb3Key_toCoords (const bitKey_t *pk, uint32 *coords, int n)
{
	uint32 res[3] = {
		(uint32)pk->vals_[0],
		(uint32)(pk->vals_[1] >> 32),
		(uint32)pk->vals_[2]};
	Assert(NULL != pk && NULL != coords && n >= 3);
	hilbert_i2c(3, 30, res, coords);
}

static zkey_vtab_t hilb3_vtab_ = {
	bit3Key_cmp,
	bit3Key_between,
	bit3Key_getBit,
	bit3Key_clearKey,
	bit3Key_setLowBits,
	bit3Key_clearLowBits,
	bit3Key_fromLong,
	bit3Key_toLong,
	hilb3Key_fromCoords,
	hilb3Key_toCoords,
	bit3Key_toStr,
};

static void  hilbKey_CTOR3(bitKey_t *pk)
{
	Assert(pk);
	memset(pk->vals_, 0, sizeof(pk->vals_));
	pk->vtab_ = &hilb3_vtab_;
}

/*--- iface ---------------------------------------------------------------------------------*/
void  bitKey_CTOR (bitKey_t *pk, bitkey_type ktype)
{
	Assert(pk);
	switch (ktype) {
		case btZ2D: 
			bitKey_CTOR2(pk);
			break;
		case btZ3D: 
			bitKey_CTOR3(pk);
			break;
		case btHilb2D: 
			hilbKey_CTOR2(pk);
			break;
		case btHilb3D: 
			hilbKey_CTOR3(pk);
			break;
		default:
			elog(ERROR, "bitKey for '%d' type has not been yet realized", ktype);
	};
}

unsigned bitKey_getNCoords(bitkey_type ktype)
{
	switch (ktype) {
		case btZ2D: 	return 2;
		case btZ3D: 	return 3;
		case btHilb2D:	return 2;
		case btHilb3D:	return 3;
		default:
			elog(ERROR, "bitKey for '%d' type has not been yet realized", ktype);
	};
	return 0;
}


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
	pk->vtab_->f_clearKey(pk);
}

void  bitKey_setLowBits (bitKey_t *pk, int idx)
{
	Assert(pk);
	pk->vtab_->f_setLowBits(pk, idx);
}

void  bitKey_clearLowBits (bitKey_t *pk, int idx)
{
	Assert(pk);
	pk->vtab_->f_clearLowBits(pk, idx);
}

void  bitKey_fromLong (bitKey_t *pk, Datum numeric)
{
	Assert(pk && numeric);
	pk->vtab_->f_fromLong(pk, numeric);
}

Datum bitKey_toLong (const bitKey_t *pk)
{
	Assert(pk);
	return pk->vtab_->f_toLong(pk);
}

void  bitKey_fromCoords (bitKey_t *pk, const uint32 *coords, int n)
{
	Assert(pk && coords);
	pk->vtab_->f_fromCoords(pk, coords, n);
}

void  bitKey_toCoords (const bitKey_t *pk, uint32 *coords, int n)
{
	Assert(pk && coords);
	pk->vtab_->f_toCoords(pk, coords, n);
}

void  bitKey_toStr(const bitKey_t *pk, char *buf, int buflen)
{
	Assert(pk && buf && buflen > 128);
	pk->vtab_->f_toStr(pk, buf, buflen);
}

