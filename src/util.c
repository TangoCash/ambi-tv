/* ambi-tv: a flexible ambilight clone for embedded linux
 *  Copyright (C) 2013 Georg Kaindl
 *
 *  This file is part of ambi-tv.
 *
 *  ambi-tv is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  ambi-tv is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with ambi-tv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include "util.h"
#include "log.h"

#define LOGNAME            "util: "

#define LIST_GROW_STEP     4

static const char html_header[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";

int ambitv_util_append_ptr_to_list(void*** list_ptr, int idx, long* len_ptr, void* ptr)
{
	if (NULL == *list_ptr)
	{
		*list_ptr = (void**) malloc(sizeof(void*) * LIST_GROW_STEP);
		*len_ptr = LIST_GROW_STEP;
	}
	else if (idx >= *len_ptr)
	{
		*len_ptr += LIST_GROW_STEP;
		*list_ptr = (void**) realloc(*list_ptr, sizeof(void*) * (*len_ptr));
	}

	(*list_ptr)[idx] = ptr;

	return ++idx;
}

int ambitv_parse_led_string(const char* str, long** out_ptr, long* out_len)
{
	int a, b, c, idx = 0, skip = 0, *s = &a;
	long ilen = 0;
	long* list = NULL;

	a = b = 0;
	while (1)
	{
		c = *str++;

		switch (c)
		{
		case 'X':
		case 'x':
		{
			if (&b == s)
			{
				ambitv_log(ambitv_log_error, LOGNAME "unexpected '%c'.\n", c);
				goto errReturn;
			}

			skip = 1;
			break;
		}

		case '-':
		{
			if (&a == s && !skip)
			{
				s = &b;
			}
			else
			{
				ambitv_log(ambitv_log_error, LOGNAME "unexpected '%c'.\n", c);
				goto errReturn;
			}

			break;
		}

		case '\0':
		case '\n':
		case ',':
		{
			if (skip)
			{
				while (a--)
					idx = ambitv_util_append_ptr_to_list((void***) &list, idx, &ilen, (void*) -1);
			}
			else
			{
				int l = a, r = a + 1, ss = 1;

				if (&b == s)
				{
					ss = (l > b) ? -1 : 1;
					r = b + ss;
				}

				do
				{
					idx = ambitv_util_append_ptr_to_list((void***) &list, idx, &ilen, (void*) l);

					l += ss;
				} while (l != r);
			}

			skip = a = b = 0;
			s = &a;

			break;
		}

		default:
		{
			if (skip || c < '0' || c > '9')
			{
				ambitv_log(ambitv_log_error, LOGNAME "unexpected character '%c'.\n", c);
				goto errReturn;
			}

			*s = *s * 10 + (c - '0');
			break;
		}
		}

		if (c == '\0' || c == '\n')
			break;
	}

	*out_ptr = list;
	*out_len = idx;

	return 0;

	errReturn: return -1;
}

char *stristr(const char *String, const char *Pattern)
{
	char *pptr, *sptr, *start;

	for (start = (char *) String; *start; start++)
	{
		/* find start of pattern in string */
		for (; (*start && (toupper(*start) != toupper(*Pattern))); start++)
			;
		if (!*start)
			return 0;

		pptr = (char *) Pattern;
		sptr = (char *) start;

		while (toupper(*sptr) == toupper(*pptr))
		{
			sptr++;
			pptr++;

			/* if end of pattern then pattern was found */

			if (!*pptr)
				return (start);
		}
	}
	return 0;
}

void netif_send(int socket, char *data, int length, int mode, bool bb)
{
	if (socket >= 0)
	{
		if (mode & NETIF_MODE_FIRST)
		{
			char tbuf[128];

			write(socket, html_header, sizeof(html_header));
			sprintf(tbuf, "Content-Length: %ld\r\n%s", (unsigned long) ((length) ? length : strlen(data)),
					(bb) ? "" : "\r\n");
			write(socket, tbuf, strlen(tbuf));
		}
		if (mode & NETIF_MODE_MID)
		{
			write(socket, data, (length) ? length : strlen(data));
		}
	}
}

