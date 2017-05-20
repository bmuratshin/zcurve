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
#if 1
#include "postgres.h"
#include "utils/numeric.h"
#include "utils/builtins.h"
#else
#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
typedef uint64_t uint64;
typedef uint32_t uint32;
typedef int64_t int64;
typedef int32_t int32;
#define Assert assert
#endif

#include "bitkey.h"
#include "hilbert2.h"


#define WITH_HACKED_NUMERIC
#ifdef WITH_HACKED_NUMERIC
#include "ex_numeric.h"
#endif

static int 
Log2(int n)
{
	return !!(n & 0xFFFF0000) << 4
		| !!(n & 0xFF00FF00) << 3
		| !!(n & 0xF0F0F0F0) << 2
		| !!(n & 0xCCCCCCCC) << 1
		| !!(n & 0xAAAAAAAA);
}

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

static void
bit2Key_split (const bitKey_t *low, const bitKey_t *high, bitKey_t *lower_high, bitKey_t *upper_low)
{
	int curBitNum = 32 * 2 - 1;
	while ((low->vals_[0] >> curBitNum) == ((high->vals_[0] >> curBitNum)))
	{
		curBitNum--;
	}
	/* cut diapason by curBitNum for new subquery */
	bit2Key_setLowBits(lower_high, curBitNum);
	/* cut diapason by curBitNum for old subquery */
	bit2Key_clearLowBits(upper_low, curBitNum);
}

static void  
bit2Key_limits_from_extent(const uint32 *bl_coords, const uint32 *ur_coords, bitKey_t *minval, bitKey_t *maxval)
{
	bit2Key_fromCoords(minval, bl_coords, 2);
	bit2Key_fromCoords(maxval, ur_coords, 2);
}

static bool  
bit2Key_isSolid (const uint32 *bl_coords, const uint32 *ur_coords, const bitKey_t *minval, const bitKey_t *maxval)
{
	uint32_t lcoords[ZKEY_MAX_COORDS];
	uint32_t hcoords[ZKEY_MAX_COORDS];
	int dcoords[ZKEY_MAX_COORDS], i, ok = 1, diff = 0, odiff = 0;
	uint64_t vol = 1;

	bitKey_toCoords (minval, lcoords, ZKEY_MAX_COORDS);
	bitKey_toCoords (maxval, hcoords, ZKEY_MAX_COORDS);

	for (i=0; i < 2; i++)
	{
		dcoords[i] = hcoords[i] - lcoords[i];
		vol *= (unsigned)(dcoords[i]);
		if (dcoords[i]++)
		{
			diff = 1 << Log2(dcoords[i]);
			if (diff != dcoords[i])
			{
				ok = 0;
				break;
			}
			if (odiff && odiff != diff)
			{
				ok = 0;
				break;
			}
			odiff = diff;
		}
	}
	if (0 == vol)
	{
		ok = 0;
	}
	return ok != 0;
}

static bool  
bit2Key_hasSmth (const uint32 *bl_coords, const uint32 *ur_coords, const bitKey_t *minval, const bitKey_t *maxval)
{
	return true;
}

static zkey_vtab_t key2_vtab_ = {
	bit2Key_cmp,
	bit2Key_clearKey,
	bit2Key_fromLong,
	bit2Key_toLong,
	bit2Key_fromCoords,
	bit2Key_toCoords,
	bit2Key_toStr,
	bit2Key_split,
	bit2Key_limits_from_extent,
	bit2Key_isSolid,
	bit2Key_hasSmth,
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
		unsigned lidx = (idx - 64) & 0x3f;
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
		unsigned lidx = (idx - 64) & 0x3f;
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
	const int weight = NUMERIC_WEIGHT(pvar);
	const int ndigits = NUMERIC_NDIGITS(pvar);
	int i, j;
	int16_t *sdata = NUMERIC_DIGITS(pvar);

	/*elog(INFO, "NDIGITS=%d WEIGHT=%d DSCALE=%d SIGN=%d", (int)NUMERIC_NDIGITS(pvar), (int)NUMERIC_WEIGHT(pvar), (int)NUMERIC_DSCALE(pvar), (int)NUMERIC_SIGN(pvar));*/

	xdata[0] = xdata[1] = xdata[2] = xdata[3] = 0;
	for (i = 0; i <= weight; i++)
	{
		int64_t ch = (i < ndigits) ? sdata[i] : 0;
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
	bit3Key_toCoords (pk, coords, 3);
	Assert(pk && buf && buflen > 128);
	sprintf(buf, "[%x %x %x]: %d %d %d", 
		(int)(pk->vals_[1] & 0xffffffff),
		(int)((pk->vals_[0] >> 32) & 0xffffffff),
		(int)(pk->vals_[0] & 0xffffffff),
		(int)coords[0],
		(int)coords[1],
		(int)coords[2]);
}

static void
bit3Key_split (const bitKey_t *low, const bitKey_t *high, bitKey_t *lower_high, bitKey_t *upper_low)
{
	int curBitNum = 32 * 3 - 1;

	for (;curBitNum;curBitNum--)
	{
		int ix0 = curBitNum >> 6; 
		int ix1 = curBitNum & 0x3f;
		if ((low->vals_[ix0] >> ix1) != (high->vals_[ix0] >> ix1))
			break;
	}
	/* cut diapason by curBitNum for new subquery */
	bit3Key_setLowBits(lower_high, curBitNum);
	/* cut diapason by curBitNum for old subquery */
	bit3Key_clearLowBits(upper_low, curBitNum);
#if 0
	//elog(INFO, "split(%d)", curBitNum);
	//elog((cnt++==50)?ERROR:INFO, "split(%d)", curBitNum);
	//char buf[256];
	//bit3Key_toStr(low, buf, 256);
	//elog(INFO, "%s", buf);
	bit3Key_toStr(high, buf, 256);
	elog(INFO, "%s", buf);
	/* cut diapason by curBitNum for new subquery */
	bit3Key_setLowBits(lower_high, curBitNum);
	/* cut diapason by curBitNum for old subquery */
	bit3Key_clearLowBits(upper_low, curBitNum);
	bit3Key_toStr(lower_high, buf, 256);
	elog(INFO, "%s", buf);
	bit3Key_toStr(upper_low, buf, 256);
	elog(INFO, "%s", buf);
#endif
}

static void
bit3Key_limits_from_extent(const uint32 *bl_coords, const uint32 *ur_coords, bitKey_t *minval, bitKey_t *maxval)
{
	bit3Key_fromCoords(minval, bl_coords, 3);
	bit3Key_fromCoords(maxval, ur_coords, 3);
}

static bool  
bit3Key_isSolid (const uint32 *bl_coords, const uint32 *ur_coords, const bitKey_t *minval, const bitKey_t *maxval)
{
	uint32_t lcoords[ZKEY_MAX_COORDS];
	uint32_t hcoords[ZKEY_MAX_COORDS];
	int dcoords[ZKEY_MAX_COORDS], i, ok = 1, diff = 0, odiff = 0;
	uint64_t vol = 1;

	bitKey_toCoords (minval, lcoords, ZKEY_MAX_COORDS);
	bitKey_toCoords (maxval, hcoords, ZKEY_MAX_COORDS);

	for (i=0; i < 3; i++)
	{
		dcoords[i] = hcoords[i] - lcoords[i];
		vol *= (unsigned)(dcoords[i]);
		if (dcoords[i]++)
		{
			diff = 1 << Log2(dcoords[i]);
			if (diff != dcoords[i])
			{
				ok = 0;
				break;
			}
			if (odiff && odiff != diff)
			{
				ok = 0;
				break;
			}
			odiff = diff;
		}
	}
	if (0 == vol)
	{
		ok = 0;
	}
#if 0
	/* gnuplot line compatible output*/
	elog(INFO, "%d %d",lcoords[0], lcoords[2]);
	elog(INFO, "%d %d",lcoords[0], hcoords[2]);
	elog(INFO, "%d %d",hcoords[0], hcoords[2]);
	elog(INFO, "%d %d",hcoords[0], lcoords[2]);
	elog(INFO, "%d %d %d %d %d %llu %c",lcoords[0], lcoords[2], dcoords[0], dcoords[1], dcoords[2], vol, ok?'*':' ');
	elog(INFO, "");
#endif
	return ok != 0;
}

static bool  
bit3Key_hasSmth (const uint32 *bl_coords, const uint32 *ur_coords, const bitKey_t *minval, const bitKey_t *maxval)
{
	return true;
}

static zkey_vtab_t key3_vtab_ = {
	bit3Key_cmp,
	bit3Key_clearKey,
	bit3Key_fromLong,
	bit3Key_toLong,
	bit3Key_fromCoords,
	bit3Key_toCoords,
	bit3Key_toStr,
	bit3Key_split,
	bit3Key_limits_from_extent,
	bit3Key_isSolid,
	bit3Key_hasSmth,
};

static void  bitKey_CTOR3(bitKey_t *pk)
{
	Assert(pk);
	memset(pk->vals_, 0, sizeof(pk->vals_));
	pk->vtab_ = &key3_vtab_;
}


/* 8D -------------------------------------------------------------------------------------------------------- */

static int
bit8Key_cmp(const bitKey_t *pl, const bitKey_t *pr)
{
	Assert(pl && pr);
	if ((pl->vals_[3] != pr->vals_[3]))
		return ((pl->vals_[3] > pr->vals_[3]) ? 1 : -1);
	if ((pl->vals_[2] != pr->vals_[2]))
		return ((pl->vals_[2] > pr->vals_[2]) ? 1 : -1);
	if ((pl->vals_[1] != pr->vals_[1]))
		return ((pl->vals_[1] > pr->vals_[1]) ? 1 : -1);
	if ((pl->vals_[0] != pr->vals_[0]))
		return ((pl->vals_[0] > pr->vals_[0]) ? 1 : -1);
	return 0;
}

static void
bit8Key_clearKey(bitKey_t *pk)
{
	Assert(NULL != pk);
	pk->vals_[0] = 0;
	pk->vals_[1] = 0;
	pk->vals_[2] = 0;
	pk->vals_[3] = 0;
}

static const uint64 smasks8[8] = {
	0x0101010101010101ULL,
	0x0202020202020202ULL,
	0x0404040404040404ULL,
	0x0808080808080808ULL,
	0x1010101010101010ULL,
	0x2020202020202020ULL,
	0x4040404040404040ULL,
	0x8080808080808080ULL,
};

static void
bit8Key_setLowBits(bitKey_t *pk, int idx)
{
	int lidx = idx;
	Assert(NULL != pk && idx < 256 && idx >= 0);
	for (; lidx > 0; lidx -= 64)
	{
		unsigned ix = lidx >> 6;
		if (idx == lidx)
		{
			unsigned llidx = (lidx % 64);
			pk->vals_[ix] |= (smasks8[7] >> (63 - llidx));
			pk->vals_[ix] -= (1ULL << llidx);
		}
		else
		{
			pk->vals_[ix] |= smasks8[lidx & 7];
		}
	}
}

static void
bit8Key_clearLowBits(bitKey_t *pk, int idx)
{
	int lidx = idx;
	Assert(NULL != pk && idx < 256 && idx >= 0);
	for (; lidx > 0; lidx -= 64)
	{
		unsigned ix = lidx >> 6;
		if (idx == lidx)
		{
			unsigned llidx = (lidx % 64);
			pk->vals_[ix] &= ~(smasks8[7] >> (63 - llidx));
			pk->vals_[ix] |= (1ULL << llidx);
		}
		else
		{
			pk->vals_[ix] &= ~(smasks8[lidx & 7]);
		}
	}
}

#ifdef WITH_HACKED_NUMERIC
static void test_numeric8(const Numeric pvar, uint32_t *xdata)
{
	const int weight = NUMERIC_WEIGHT(pvar);
	const int ndigits = NUMERIC_NDIGITS(pvar);
	int i, j;
	int16_t *sdata = NUMERIC_DIGITS(pvar);

	/*elog(INFO, "NDIGITS=%d WEIGHT=%d DSCALE=%d SIGN=%d", (int)NUMERIC_NDIGITS(pvar), (int)NUMERIC_WEIGHT(pvar), (int)NUMERIC_DSCALE(pvar), (int)NUMERIC_SIGN(pvar));*/

	xdata[0] = xdata[1] = xdata[2] = xdata[3] = 0;
	xdata[4] = xdata[5] = xdata[6] = xdata[7] = 0;
	for (i = 0; i <= weight; i++)
	{
		int64_t ch = (i < ndigits) ? sdata[i] : 0;
		for (j = 0; j < 8; j++)
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
bit8Key_fromLong(bitKey_t *pk, Datum dt)
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

test_numeric8(DatumGetNumeric(dt), xdata);
pk->vals_[0] = (((uint64_t)xdata[1]) << 32) + xdata[0];
pk->vals_[1] = (((uint64_t)xdata[3]) << 32) + xdata[2];
pk->vals_[2] = (((uint64_t)xdata[5]) << 32) + xdata[4];
pk->vals_[3] = (((uint64_t)xdata[7]) << 32) + xdata[6];
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
bit8Key_toLong(const bitKey_t *pk)
{
  int i;
  Datum lval = DirectFunctionCall1(int8_numeric, Int64GetDatum(1ULL << 32));
  Datum mul64_result = DirectFunctionCall2(numeric_mul, lval, lval);
  Datum nm = DirectFunctionCall1(int8_numeric, Int64GetDatum(0ULL));
  for (i = 0; i < 4; i++)
  {
    nm = DirectFunctionCall2(numeric_mul, nm, mul64_result);
    lval = DirectFunctionCall1(int8_numeric, Int64GetDatum(pk->vals_[3 - i]));
    nm = DirectFunctionCall2(numeric_add, nm, lval);
  }
  return nm;
}

static const uint32 key8ToBits[16] = {
	0, 1, (1 << 8), 1 | (1 << 8),
	(1 << 16), (1 << 16) | 1, (1 << 16) | (1 << 8), 1 | (1 << 8) | (1 << 16),
	(1 << 24), (1 << 24) | 1, (1 << 24) | (1 << 8), 1 | (1 << 8) | (1 << 24),
	(1 << 16) | (1 << 24), (1 << 16) | (1 << 24) | 1, (1 << 24) | (1 << 16) | (1 << 8), 1 | (1 << 8) | (1 << 16) | (1 << 24),
};

static void
bit8Key_fromCoords(bitKey_t *pk, const uint32 *coords, int n)
{
	int i, j;
	uint32 x = coords[0];
	uint32 y = coords[1];
	uint32 z = coords[2];
	uint32 a = coords[3];
	uint32 b = coords[4];
	uint32 c = coords[5];
	uint32 d = coords[6];
	uint32 e = coords[7];
	Assert(pk && coords && n >= 8);

	pk->vals_[0] = pk->vals_[1] = pk->vals_[2] = pk->vals_[3] = 0;
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 2; j++)
		{
			uint64 tmp =
				(key8ToBits[x & 0xf]) |
				(key8ToBits[y & 0xf] << 1) |
				(key8ToBits[z & 0xf] << 2) |
				(key8ToBits[a & 0xf] << 3) |
				(key8ToBits[b & 0xf] << 4) |
				(key8ToBits[c & 0xf] << 5) |
				(key8ToBits[d & 0xf] << 6) |
				(key8ToBits[e & 0xf] << 7);
			pk->vals_[i] |= (tmp << (j << 5));
			x >>= 4; y >>= 4; z >>= 4;
			a >>= 4; b >>= 4; c >>= 4;
			d >>= 4; e >>= 4;
		}
	}
}

static void
bit8Key_toCoords(const bitKey_t *pk, uint32 *coords, int n)
{
	int i, j, k;

	Assert(pk && coords && n >= 8);
	memset(coords, 0, 8 * sizeof(*coords));
	/* 8 X 4 */
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 2; j++)
		{
			uint32 tmp = (pk->vals_[i] >> (j << 5)) & 0xffffffff;
			for (k = 0; k < 8; k++)
			{
				uint64 tmpx = 
					((tmp & (1 << k)) >> k) +
					((tmp & (1 << (8 + k))) >> (7 + k)) +
					((tmp & (1 << (16 + k))) >> (14 + k)) +
					((tmp & (1 << (24 + k))) >> (21 + k));
				coords[k] |= tmpx << (((i << 1) + j) << 2);
			}
		}
	}
}

static void
bit8Key_toStr(const bitKey_t *pk, char *buf, int buflen)
{
	uint32 coords[8];
	bit8Key_toCoords(pk, coords, 8);
	Assert(pk && buf && buflen >= 256);
	sprintf(buf, "[%x %x %x %x %x %x %x %x]: %d %d %d %d %d %d %d %d",
		(int)((pk->vals_[3] >> 32) & 0xffffffff),
		(int)(pk->vals_[3] & 0xffffffff),
		(int)((pk->vals_[2] >> 32) & 0xffffffff),
		(int)(pk->vals_[2] & 0xffffffff),
		(int)((pk->vals_[1] >> 32) & 0xffffffff),
		(int)(pk->vals_[1] & 0xffffffff),
		(int)((pk->vals_[0] >> 32) & 0xffffffff),
		(int)(pk->vals_[0] & 0xffffffff),
		(int)coords[0],
		(int)coords[1],
		(int)coords[2],
		(int)coords[3],
		(int)coords[4],
		(int)coords[5],
		(int)coords[6],
		(int)coords[7]);
}

static void
bit8Key_split(const bitKey_t *low, const bitKey_t *high, bitKey_t *lower_high, bitKey_t *upper_low)
{
	static int cnt = 0;
	char buf[256];
	int curBitNum = 32 * 8 - 1;

	for (; curBitNum; curBitNum--)
	{
		int ix0 = curBitNum >> 6;
		int ix1 = curBitNum & 0x3f;
		if ((low->vals_[ix0] >> ix1) != (high->vals_[ix0] >> ix1))
			break;
	}
	/* cut diapason by curBitNum for new subquery */
	bit8Key_setLowBits(lower_high, curBitNum);
	/* cut diapason by curBitNum for old subquery */
	bit8Key_clearLowBits(upper_low, curBitNum);
#if 0
	//elog(INFO, "split(%d)", curBitNum);
	//elog((cnt++==20)?ERROR:INFO, "split(%d)", curBitNum);
	bit8Key_toStr(low, buf, 256);
	elog(INFO, "%s", buf);
	bit8Key_toStr(high, buf, 256);
	elog(INFO, "%s", buf);
	/* cut diapason by curBitNum for new subquery */
	bit8Key_setLowBits(lower_high, curBitNum);
	/* cut diapason by curBitNum for old subquery */
	bit8Key_clearLowBits(upper_low, curBitNum);
	bit8Key_toStr(lower_high, buf, 256);
	elog(INFO, "%s", buf);
	bit8Key_toStr(upper_low, buf, 256);
	elog(INFO, "%s", buf);
#endif
}

static void
bit8Key_limits_from_extent(const uint32 *bl_coords, const uint32 *ur_coords, bitKey_t *minval, bitKey_t *maxval)
{
	bit8Key_fromCoords(minval, bl_coords, 8);
	bit8Key_fromCoords(maxval, ur_coords, 8);
}

static bool
bit8Key_isSolid(const uint32 *bl_coords, const uint32 *ur_coords, const bitKey_t *minval, const bitKey_t *maxval)
{
	uint32_t lcoords[ZKEY_MAX_COORDS];
	uint32_t hcoords[ZKEY_MAX_COORDS];
	int dcoords[ZKEY_MAX_COORDS], i, ok = 1, diff = 0, odiff = 0;
	uint64_t vol = 1;

	bit8Key_toCoords(minval, lcoords, ZKEY_MAX_COORDS);
	bit8Key_toCoords(maxval, hcoords, ZKEY_MAX_COORDS);

	for (i = 0; i < 8; i++)
	{
		dcoords[i] = hcoords[i] - lcoords[i];
		vol *= (unsigned)(dcoords[i]);
		if (dcoords[i]++)
		{
			diff = 1 << Log2(dcoords[i]);
			if (diff != dcoords[i])
			{
				ok = 0;
				break;
			}
			if (odiff && odiff != diff)
			{
				ok = 0;
				break;
			}
			odiff = diff;
		}
	}
	if (0 == vol)
	{
		ok = 0;
	}
#if 0
	/* gnuplot line compatible output*/
	elog(INFO, "%d %d", lcoords[0], lcoords[2]);
	elog(INFO, "%d %d", lcoords[0], hcoords[2]);
	elog(INFO, "%d %d", hcoords[0], hcoords[2]);
	elog(INFO, "%d %d", hcoords[0], lcoords[2]);
	elog(INFO, "%d %d %d %d %d %llu %c", lcoords[0], lcoords[2], dcoords[0], dcoords[1], dcoords[2], vol, ok ? '*' : ' ');
	elog(INFO, "");
#endif
	return ok != 0;
}

static bool
bit8Key_hasSmth(const uint32 *bl_coords, const uint32 *ur_coords, const bitKey_t *minval, const bitKey_t *maxval)
{
	return true;
}

static zkey_vtab_t key8_vtab_ = {
	bit8Key_cmp,
	bit8Key_clearKey,
	bit8Key_fromLong,
	bit8Key_toLong,
	bit8Key_fromCoords,
	bit8Key_toCoords,
	bit8Key_toStr,
	bit8Key_split,
	bit8Key_limits_from_extent,
	bit8Key_isSolid,
	bit8Key_hasSmth,
};

static void  bitKey_CTOR8(bitKey_t *pk)
{
	Assert(pk);
	memset(pk->vals_, 0, sizeof(pk->vals_));
	pk->vtab_ = &key8_vtab_;
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

static void
hilb2Key_split(const bitKey_t *low, const bitKey_t *high, bitKey_t *lower_high, bitKey_t *upper_low)
{
	elog(ERROR, "hilb2Key_split not realized");
}

static void
hilb2Key_limits_from_extent(const uint32 *bl_coords, const uint32 *ur_coords, bitKey_t *minval, bitKey_t *maxval)
{
	elog(ERROR, "hilb2Key_limits_from_extent not realized");
}

static bool  
hilb2Key_isSolid (const uint32 *bl_coords, const uint32 *ur_coords, const bitKey_t *minval, const bitKey_t *maxval)
{
	return true;
}

static zkey_vtab_t hilb2_vtab_ = {
	bit2Key_cmp,
	bit2Key_clearKey,
	bit2Key_fromLong,
	bit2Key_toLong,
	hilb2Key_fromCoords,
	hilb2Key_toCoords,
	bit2Key_toStr,
	hilb2Key_split,
	hilb2Key_limits_from_extent,
	hilb2Key_isSolid,
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
		(uint32)(pk->vals_[0] >> 32),
		(uint32)pk->vals_[1]};
	Assert(NULL != pk && NULL != coords && n >= 3);
	hilbert_i2c(3, 30, res, coords);
}

static int calcCurBit(uint32 l, uint32 r)
{
	int curBitNum = 31;

	for (; curBitNum; curBitNum--)
	{
		if ((l >> curBitNum) != (r >> curBitNum))
			return curBitNum;
	}
	return 0;
}

static int calcCurBit64(uint64 l, uint64 r)
{
	int curBitNum = 63;

	for (; curBitNum; curBitNum--)
	{
		if ((l >> curBitNum) != (r >> curBitNum))
			return curBitNum;
	}
	return 0;
}


#define MIN(a,b) (a>b?b:a)
#define MAX(a,b) (a>b?a:b)
#define ABS(a) (a>0?a:-(a))

static int hilb3_get_extent(const bitKey_t *minval, const bitKey_t *maxval, uint32 *bl_coords, uint32 *ur_coords)
{
	uint32 tmp_coords[3];
	uint32 tmp_coords2[3];
	uint32 tmp_coords3[3];

	uint32 vals1[3] = {
		(uint32)minval->vals_[0],
		(uint32)(minval->vals_[0] >> 32),
		(uint32)minval->vals_[1] };

	uint32 vals2[3] = {
		(uint32)maxval->vals_[0],
		(uint32)(maxval->vals_[0] >> 32),
		(uint32)maxval->vals_[1] };

	uint32 vals3[4] = {
		(uint32)maxval->vals_[0],
		(uint32)(maxval->vals_[0] >> 32),
		(uint32)maxval->vals_[1], 0 };

	bitKey_t mid = *minval;
	uint64 cr = 0;
	int i, cb = 0;
	/* let's find the lowest differing bit */
	if (minval->vals_[1] != maxval->vals_[1])
	{
		cb = calcCurBit64(minval->vals_[1], maxval->vals_[1]) + 64;
	}
	else
	{
		cb = calcCurBit64(minval->vals_[0], maxval->vals_[0]);
	}

	/* calc half-sum */
	for (i = 0; i < 3; i++)
	{
		cr = (uint64_t)(vals1[i]) + (uint64_t)(vals2[i]) + cr;
		vals3[i] = cr & 0xffffffff;
		cr >>= 32;
	}
	for (i = 0; i < 3; i++)
	{
		vals3[i] >>= 1;
		if (1 & vals3[i + 1])
			vals3[i] |= 0x80000000;
	}
	mid.vals_[0] = vals3[0] + (((uint64)vals3[1]) << 32);
	mid.vals_[1] = vals3[2];

	/* calc coords */
	hilb3Key_toCoords(minval, tmp_coords, 3);
	switch ((cb + 1) % 3) {
		case 0:	/* cube */
		{
			uint32_t mask = (1 << ((cb + 1) / 3)) - 1;
			for (i = 0; i < 3; i++)
			{
				bl_coords[i] = tmp_coords[i] & ~mask;
				ur_coords[i] = tmp_coords[i] | mask;
			}
			return 0;
		}
		case 1:	/* quarter */
		case 2:	/* half */
		{
			uint32_t mask = (1 << ((cb + 1) / 3)) - 1;
			hilb3Key_toCoords(maxval, tmp_coords2, 3);
			hilb3Key_toCoords(&mid, tmp_coords3, 3);
			for (i = 0; i < 3; i++)
			{
				bl_coords[i] = MIN(tmp_coords[i], tmp_coords2[i]);
				bl_coords[i] = MIN(bl_coords[i], tmp_coords3[i]);
				ur_coords[i] = MAX(tmp_coords[i], tmp_coords2[i]);
				ur_coords[i] = MAX(ur_coords[i], tmp_coords3[i]);
			}
			for (i = 0; i < 3; i++)
			{
				bl_coords[i] &= ~mask;
				ur_coords[i] |= mask;
				if (bl_coords[i] > ur_coords[i])
				{
					uint32 tmp = ur_coords[i];
					ur_coords[i] = bl_coords[i];
					bl_coords[i] = tmp;
				}
			}
			return 1;
		}
	};
	return 0;
}

static void
hilb3Key_split(const bitKey_t *low, const bitKey_t *high, bitKey_t *lower_high, bitKey_t *upper_low)
{
	int i, cb;

	if (low->vals_[1] != high->vals_[1])
	{
		cb = calcCurBit64(low->vals_[1], high->vals_[1]) + 64;
	}
	else
	{
		cb = calcCurBit64(low->vals_[0], high->vals_[0]);
	}

	*lower_high = *low;
	*upper_low = *high;

	i = 0;
	if (cb > 64)
	{
		cb -= 64;
		upper_low->vals_[0] = 0ULL;
		upper_low->vals_[0] |= (1ULL << cb);
		lower_high->vals_[0] = ~0ULL;
		i = 1;
	}
	{
		uint64 tmp = (1ULL << cb) - 1;
		upper_low->vals_[i] &= ~tmp;
		upper_low->vals_[i] |= (1ULL << cb);
		lower_high->vals_[i] |= tmp;
	}

#if 0
	{

		static int cnt = 0;
		uint32 tmp_coords[3];
		uint32 tmp_coords2[3];
		hilb3Key_toCoords(/*lower_*/lower_high, tmp_coords2, 3);
		hilb3Key_toCoords(/*upper_*/low, tmp_coords, 3);
		elog(INFO, "%x %x %x <== %x %x %x (%d %d %d <= %d %d %d)", 
			tmp_coords[0], tmp_coords[1], tmp_coords[2], tmp_coords2[0], tmp_coords2[1], tmp_coords2[2],
			tmp_coords[0], tmp_coords[1], tmp_coords[2], tmp_coords2[0], tmp_coords2[1], tmp_coords2[2]);

		hilb3Key_toCoords(high, tmp_coords2, 3);
		hilb3Key_toCoords(upper_low, tmp_coords, 3);
		elog(INFO, "%x %x %x ==> %x %x %x (%d %d %d => %d %d %d)", 
			tmp_coords[0], tmp_coords[1], tmp_coords[2], tmp_coords2[0], tmp_coords2[1], tmp_coords2[2],
			tmp_coords[0], tmp_coords[1], tmp_coords[2], tmp_coords2[0], tmp_coords2[1], tmp_coords2[2]);

		hilb3_get_extent(low, lower_high, tmp_coords, tmp_coords2);
		elog(INFO, "%x %x %x <= %x %x %x (%d %d %d <= %d %d %d)", 
			tmp_coords[0], tmp_coords[1], tmp_coords[2], tmp_coords2[0], tmp_coords2[1], tmp_coords2[2],
			tmp_coords[0], tmp_coords[1], tmp_coords[2], tmp_coords2[0], tmp_coords2[1], tmp_coords2[2]);
		
		hilb3_get_extent(upper_low, high, tmp_coords, tmp_coords2);
		elog(INFO, "%x %x %x => %x %x %x (%d %d %d => %d %d %d)", 
			tmp_coords[0], tmp_coords[1], tmp_coords[2], tmp_coords2[0], tmp_coords2[1], tmp_coords2[2],
			tmp_coords[0], tmp_coords[1], tmp_coords[2], tmp_coords2[0], tmp_coords2[1], tmp_coords2[2]);
		elog(INFO, "");

		if (++cnt > 10)
		{
			/*elog(ERROR, "Sic!");*/
		}
	}
#endif
}

static void
hilb3Key_limits_from_extent(const uint32 *bl_coords, const uint32 *ur_coords, bitKey_t *minval, bitKey_t *maxval)
{
	uint32 coords_low[3] = { bl_coords[0], bl_coords[1], bl_coords[2] };
	uint32 coords_high[3] = { ur_coords[0], ur_coords[1], ur_coords[2] };
	uint32 bits[3];
	int i, max_bits = 0;

	for (i = 0; i < 3; i++)
	{
		bits[i] = calcCurBit(bl_coords[i], ur_coords[i]) + 1;
		if (bits[i] > max_bits)
			max_bits = bits[i];
	}

	hilb3Key_fromCoords(minval, coords_low, 3);
	hilb3Key_fromCoords(maxval, coords_high, 3);

	max_bits *= 3;
	i = 0;
	if (max_bits > 64)
	{
		max_bits -= 64;
		minval->vals_[0] = 0ULL;
		maxval->vals_[0] = ~0ULL;
		i = 1;
	}
	{
		uint64 tmp = (1ULL << max_bits) - 1;
		minval->vals_[i] &= ~tmp;
		maxval->vals_[i] |= tmp;
	}
#if 0
	{
		uint32 tmp_coords[3];
		uint32 tmp_coords2[3];
		hilb3Key_toCoords(minval, tmp_coords, 3);
		hilb3Key_toCoords(maxval, tmp_coords2, 3);
		elog(INFO, "{%x %x %x => %x %x %x}", tmp_coords[0], tmp_coords[1], tmp_coords[2], tmp_coords2[0], tmp_coords2[1], tmp_coords2[2]);
	}
#endif
}

static void  
hilb3Key_toStr(const bitKey_t *pk, char *buf, int buflen)
{
	uint32 coords[3];
	hilb3Key_toCoords (pk, coords, 3);
	Assert(pk && buf && buflen > 128);
	sprintf(buf, "[%x %x %x]: %x %x %x", 
		(int)(pk->vals_[1] & 0xffffffff),
		(int)((pk->vals_[0] >> 32) & 0xffffffff),
		(int)(pk->vals_[0] & 0xffffffff),
		(int)coords[0],
		(int)coords[1],
		(int)coords[2]);
}

#define MIN(a,b) (a>b?b:a)
#define MAX(a,b) (a>b?a:b)

static bool  
hilb3Key_hasSmth (const uint32 *bl_coords, const uint32 *ur_coords, const bitKey_t *minval, const bitKey_t *maxval)
{
	uint32_t lcoords[ZKEY_MAX_COORDS];
	uint32_t hcoords[ZKEY_MAX_COORDS];
	int i;
	bool ret = true;

	hilb3_get_extent(minval, maxval, lcoords, hcoords);

	for (i = 0; i < 3; i++)
	{
		if (lcoords[i] > ur_coords[i] || hcoords[i] < bl_coords[i])
		{
			ret = false;
			break;
		}
	}
	/*elog(INFO, "HasSmth %d %d %d %d %d %d %c",lcoords[0], lcoords[1], lcoords[2], hcoords[0], hcoords[1], hcoords[2], ret?'*':' ');*/
	return ret;
}

static bool  
hilb3Key_isSolid (const uint32 *bl_coords, const uint32 *ur_coords, const bitKey_t *minval, const bitKey_t *maxval)
{
	uint32_t lcoords[ZKEY_MAX_COORDS];
	uint32_t hcoords[ZKEY_MAX_COORDS];
	int i;
	bool ret = true;

	hilb3_get_extent(minval, maxval, lcoords, hcoords);

	for (i = 0; i < 3; i++)
	{
		if (lcoords[i] < bl_coords[i] || lcoords[i] > ur_coords[i])
		{
			ret = false;
			break;
		}
		if (hcoords[i] < bl_coords[i] || hcoords[i] > ur_coords[i])
		{
			ret = false;
			break;
		}
	}

#if 0
	{
	bool has_smth = hilb3Key_hasSmth (bl_coords, ur_coords, minval, maxval);
	/* gnuplot line compatible output*/
	uint32 minx = MIN(lcoords[0], hcoords[0]);
	uint32 maxx = MAX(lcoords[0], hcoords[0]);
	uint32 miny = MIN(lcoords[2], hcoords[2]);
	uint32 maxy = MAX(lcoords[2], hcoords[2]);
	elog(INFO, "%d %d",minx, miny);
	elog(INFO, "%d %d",minx, maxy);
	elog(INFO, "%d %d",maxx, maxy);
	elog(INFO, "%d %d",maxx, miny);
	elog(INFO, "%d %d %c %c",minx, miny, ret?'*':' ', has_smth? '+': '-');
	elog(INFO, "");
	}
#endif
	return ret;
}


static zkey_vtab_t hilb3_vtab_ = {
	bit3Key_cmp,
	bit3Key_clearKey,
	bit3Key_fromLong,
	bit3Key_toLong,
	hilb3Key_fromCoords,
	hilb3Key_toCoords,
	hilb3Key_toStr,
	hilb3Key_split,
	hilb3Key_limits_from_extent,
	hilb3Key_isSolid,
	hilb3Key_hasSmth,
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
		case btZ8D:
			bitKey_CTOR8(pk);
			break;
		default:;
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
		case btZ8D:		return 8;
		default:;
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

void  bitKey_clearKey (bitKey_t *pk)
{
	Assert(pk);
	pk->vtab_->f_clearKey(pk);
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

void  bitKey_split(const bitKey_t *low, const bitKey_t *high, bitKey_t *lower_high, bitKey_t *upper_low)
{
	Assert(low && high && lower_high && upper_low);
	Assert(low->vtab_ == high->vtab_ && lower_high->vtab_ == upper_low->vtab_ && low->vtab_ == upper_low->vtab_);
	low->vtab_->f_split(low, high, lower_high, upper_low);
}

void  bitKey_limits_from_extent(const uint32 *bl_coords, const uint32 *ur_coords, bitKey_t *minval, bitKey_t *maxval)
{
	Assert(bl_coords && ur_coords && minval && maxval);
	Assert(minval->vtab_ == maxval->vtab_);
	minval->vtab_->f_limits_from_extent(bl_coords, ur_coords, minval, maxval);
}

bool  bitKey_isSolid (const uint32 *bl_coords, const uint32 *ur_coords, const bitKey_t *minval, const bitKey_t *maxval)
{
	Assert(bl_coords && ur_coords && minval && maxval);
	Assert(minval->vtab_ == maxval->vtab_);
	return minval->vtab_->f_isSolid(bl_coords, ur_coords, minval, maxval);
}

bool  bitKey_hasSmth (const uint32 *bl_coords, const uint32 *ur_coords, const bitKey_t *minval, const bitKey_t *maxval)
{
	Assert(bl_coords && ur_coords && minval && maxval);
	Assert(minval->vtab_ == maxval->vtab_);
	return minval->vtab_->f_hasSmth(bl_coords, ur_coords, minval, maxval);
}
