#ifndef _BABELTRACE_COMPAT_UNISTD_H
#define _BABELTRACE_COMPAT_UNISTD_H

/*
 * babeltrace/compat/unistd.h
 *
 * (C) Copyright 2016 - Michael Jeanson <mjeanson@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <unistd.h>

#ifdef __MINGW32__
#include <windows.h>
#include <errno.h>

#define _SC_PAGESIZE 30

static inline
long bt_sysconf(int name)
{
	SYSTEM_INFO si;

	switch(name) {
	case _SC_PAGESIZE:
		GetNativeSystemInfo(&si);
		return si.dwPageSize;
	default:
		errno = EINVAL;
		return -1;
	}
}

#else

static inline
long bt_sysconf(int name)
{
	return sysconf(name);
}

#endif
#endif /* _BABELTRACE_COMPAT_UNISTD_H */
