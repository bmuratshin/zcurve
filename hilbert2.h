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
 * contrib/zcurve/hilbert2.h
 *
 *
 * hilbert2.h -- Hilbert curve, adopted for an arbitrary lenght, up to 8X30 = 240 bits
 *		
 *
 */

/* C header file for Hilbert curve functions */
#if !defined(_hilbert2_h_)
#define _hilbert2_h_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


	/*****************************************************************
	* TAG( hilbert_i2c )
	*
	* Convert an index into a Hilbert curve to a set of coordinates.
	* Inputs:
	* 	n:	Number of coordinate axes.
	* 	m:	Number of bits per axis.
	* 	r:	The index, contains n*m bits (so n*m must be <= 240).
	* Outputs:
	* 	a:	The list of n coordinates, each with m bits.
	* Assumptions:
	* 	n*m < (sizeof r) * (bits_per_byte), n <= 8, m <= 30.
	* Algorithm:
	* 	From A. R. Butz, "Alternative Algorithm for Hilbert's
	* 		Space-Filling Curve", IEEE Trans. Comp., April, 1971,
	* 		pp 424-426.
	*/
	void hilbert_i2c(int n, int m, const uint32_t r[], uint32_t a[]);

	/*****************************************************************
	* TAG( hilbert_c2i )
	*
	* Convert coordinates of a point on a Hilbert curve to its index.
	* Inputs:
	* 	n:	Number of coordinates.
	* 	m:	Number of bits/coordinate.
	* 	a:	Array of n m-bit coordinates.
	* Outputs:
	* 	r:	Output index value.  n*m bits.
	* Assumptions:
	* 	n*m <= 240, n <= 8, m <= 30.
	* Algorithm:
	* 	Invert the above.
	*/
	void hilbert_c2i(int n, int m, const uint32_t a[], uint32_t r[]);

#ifdef __cplusplus
}
#endif

#endif /* _hilbert2_h_ */
