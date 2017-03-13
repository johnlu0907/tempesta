/**
 *		Tempesta FW
 *
 * HTTP/2 HPack parser test (fragmented version).
 *
 * Copyright (C) 2017 Tempesta Technologies, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "../common.h"
#include "../../pool.h"
#include "../../str.h"
#include "../buffers.h"
#include "../hpack.h"

typedef struct {
	uint32 encoded_len;
	int32 window;
	const char *encoded;
} HPackTestData;

#include "hptestdata.h"

#define ITEMS (sizeof(test) / sizeof(HPackTestData))

#define Print_Results 1
#define Iterations 2048

#define rm_a 1664525
#define rm_b 22695477

#define rm_c1 1013904223
#define rm_c2 1

static ufast Random_x = 0x55555555;
static ufast Random_y = 0xAAAAAAAA;

/* Very simple random number generator, designed for */
/* test purposes only, based on two mod 2^32 LCGs: */

static ufast
Random32(void)
{
	const ufast x = Random_x * rm_a + rm_c1;
	const ufast y = Random_y * rm_b + rm_c2;

	Random_x = x;
	Random_y = y;
	return (x >> 8) ^ (y << 8);
}

/* Unbiased random index in the {0; n} range: */

static ufast
Random32_Index(const ufast n)
{
	if (n) {
		ufast limit = 4294967295U - 4294967295U % n;
		ufast x;

		do {
			x = Random32();
		} while (x >= limit);
		return x % n;
	} else {
		return 0;
	}
}

#if Print_Results

static void
Print_TfwStr(TfwStr * __restrict str)
{
	static char buf[1024];
	ufast length = str->len;

	if (TFW_STR_PLAIN(str)) {
		memcpy(buf, str->ptr, length);
	} else {
		char *__restrict bp = buf;
		TfwStr *__restrict fp = (TfwStr *) str->ptr;
		const ufast count = TFW_STR_CHUNKN(str);
		ufast cnt = count;

		do {
			const ufast m = fp->len;

			memcpy(bp, fp->ptr, m);
			bp += m;
			fp++;
		} while (--cnt);
	}
	buf[length] = 0;
	printf("%s", buf);
}
#endif

int common_cdecl
main(void)
{
	static TfwStr fragments[3];
	static TfwStr root;
	static char buf1[256];
	static char buf2[256];
	static char buf3[256];
	static HTTP2Input in;
	static HTTP2Output out;
	HPack *hp;
	ufast k, i;
	uwide ts;
	double tm;

	ts = clock();
	fragments[0].ptr = buf2;
	fragments[1].ptr = buf1;
	fragments[2].ptr = buf3;
	buffer_new(&out, NULL);
	hp = hpack_new(4096, NULL);
	for (k = 0; k < Iterations; k++) {
		for (i = 0; i < ITEMS; i++) {
			fast window;
			HTTP2Field *fields;
			const char *__restrict encoded = test[i].encoded;
			ufast rc;
			ufast length = test[i].encoded_len;

			if (length == 1) {
				root.ptr = buf1;
				root.len = 1;
				root.flags = 0;
				buf1[0] = encoded[0];
			} else {
				root.ptr = fragments;
				root.len = length;
				if (length == 2) {
					root.flags = 2 << TFW_STR_CN_SHIFT;
					fragments[0].len = 1;
					fragments[1].len = 1;
					buf2[0] = encoded[0];
					buf1[0] = encoded[1];
				} else {
					const ufast split2 =
					    Random32_Index(length - 1) + 1;
					const ufast split1 =
					    Random32_Index(split2) + 1;
					if (split1 == split2) {
						root.flags =
						    2 << TFW_STR_CN_SHIFT;
						fragments[0].len = split1;
						fragments[1].len =
						    length - split1;
						memcpy(buf2, encoded, split1);
						memcpy(buf1, encoded + split1,
						       length - split1);
					} else {
						root.flags =
						    3 << TFW_STR_CN_SHIFT;
						fragments[0].len = split1;
						fragments[1].len =
						    split2 - split1;
						fragments[2].len =
						    length - split2;
						memcpy(buf2, encoded, split1);
						memcpy(buf1, encoded + split1,
						       split2 - split1);
						memcpy(buf3, encoded + split2,
						       length - split2);
					}
				}
			}
			buffer_from_tfwstr(&in, &root);
			window = test[i].window;
			if (window) {
				if (window < 0) {
					hpack_set_window(hp, 0);
					window = -window;
				}
				hpack_set_window(hp, window);
			}
			fields = hpack_decode(hp, &in, length, &out, &rc);
			if (rc) {
				printf("Bug #1: Iteration: %u, rc = %u...\n", i,
				       rc);
				return 1;
			}
			if (fields == NULL) {
				printf
				    ("Bug #2: Iteration: %u, no headers decoded...\n",
				     i);
				return 1;
			}
			do {
#if Print_Results
				Print_TfwStr(&fields->name);
				printf(":");
				Print_TfwStr(&fields->value);
				printf("\n");
#endif
				buffer_str_free(NULL, &fields->name);
				buffer_str_free(NULL, &fields->value);
				fields = fields->next;
			} while (fields);
			hpack_free_list(&out, fields);
		}
	}
	tm = (double)(clock() - ts) / CLOCKS_PER_SEC;
	printf("Time = %g\n", tm);
	return 0;
}