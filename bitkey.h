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
 * contrib/zcurve/bitkey.h
 *
 *
 * bitkey.h -- spatial key operations
 *		
 *
 * Author:	Boris Muratshin, mailto:bmuratshin@gmail.com
 * 
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


typedef enum bitkey_type {
    btUnknown = 0,
    btZ2D = 1,
    btZ3D = 2,
    btHilb2D = 3,
    btHilb3D = 4,
} bitkey_type;

	/* key stuff ------------------------------------------------------------------------------- */
#define ZKEY_BUFLEN_BY_WORDS64 3
#define ZKEY_MAX_COORDS 6
	typedef struct bitKey_s {
		zkey_vtab_t 	*vtab_;
		uint64 		vals_[ZKEY_BUFLEN_BY_WORDS64];
	} bitKey_t;


	extern void  	bitKey_CTOR(bitKey_t *pk, bitkey_type ktype);
	extern unsigned bitKey_getNCoords(bitkey_type ktype);
	extern int   	bitKey_cmp(const bitKey_t *, const bitKey_t *);
	extern bool  	bitKey_between(const bitKey_t *val, const bitKey_t *minval, const bitKey_t *maxval);
	extern int   	bitKey_getBit(const bitKey_t *pk, int idx);
	extern void  	bitKey_clearKey(bitKey_t *pk);
	extern void  	bitKey_setLowBits(bitKey_t *pk, int idx);
	extern void  	bitKey_clearLowBits(bitKey_t *pk, int idx);
	extern void  	bitKey_fromLong(bitKey_t *pk, Datum dt);
	extern Datum 	bitKey_toLong(const bitKey_t *pk);
	extern void  	bitKey_fromCoords(bitKey_t *pk, const uint32 *coords, int n);
	extern void  	bitKey_toCoords(const bitKey_t *pk, uint32 *coords, int n);
	extern void  	bitKey_toStr(const bitKey_t *pk, char *buf, int buflen);


#endif /* __ZCURVE_BITKEY_H */