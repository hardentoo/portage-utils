/*
 * utility funcs
 *
 * Copyright 2005-2018 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#include <stdio.h>

static size_t
safe_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t ret = 0, this_ret;

	do {
		this_ret = fwrite(ptr, size, nmemb, stream);
		if (this_ret == nmemb)
			return this_ret; /* most likely behavior */
		if (this_ret == 0) {
			if (feof(stream))
				break;
			if (ferror(stream)) {
				if (errno == EAGAIN || errno == EINTR)
					continue;
				errp("fwrite(%p, %zu, %zu) failed (wrote %zu elements)",
					ptr, size, nmemb, ret);
			}
		}
		nmemb -= this_ret;
		ret += this_ret;
		ptr += (this_ret * size);
	} while (nmemb);

	return ret;
}
#define fwrite safe_fwrite
