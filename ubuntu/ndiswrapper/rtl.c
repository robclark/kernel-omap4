/*
 *  Copyright (C) 2003-2005 Pontus Fuchs, Giridhar Pemmasani
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 */

#include "ntoskernel.h"
#include "rtl_exports.h"

wstdcall SIZE_T WIN_FUNC(RtlCompareMemory,3)
	(const void *a, const void *b, SIZE_T len)
{
	size_t i;
	char *x, *y;

	x = (char *)a;
	y = (char *)b;
	/* MSDN says this should return number of bytes that compare as
	 * equal. This can be interpretted as either all bytes that are
	 * equal in 'len' bytes or that only until the bytes compare as
	 * not equal. Initially we had it the former way, but Realtek driver
	 * doesn't like it that way - it takes many attempts to associate
	 * with WPA. ReactOS returns the number of bytes that are equal
	 * upto when they compare as not equal.
	 * According to lords at #reactos, that is the way it should be
	 * and that msdn is wrong about it!
	 */
	for (i = 0; i < len && x[i] == y[i]; i++)
		;
	return i;
}

wstdcall void WIN_FUNC(RtlCopyMemory,3)
	(void *dst, const void *src, SIZE_T length)
{
	memcpy(dst, src, length);
}

wstdcall void WIN_FUNC(RtlZeroMemory,2)
	(void *dst, SIZE_T length)
{
	memset(dst, 0, length);
}

wstdcall void WIN_FUNC(RtlSecureZeroMemory,2)
	(void *dst, SIZE_T length)
{
	memset(dst, 0, length);
}

wstdcall void WIN_FUNC(RtlFillMemory,3)
	(void *dest, SIZE_T length, UCHAR fill)
{
	memset(dest, fill, length);
}

wstdcall void WIN_FUNC(RtlMoveMemory,3)
	(void *dest, const void *src, SIZE_T length)
{
	memmove(dest, src, length);
}

wstdcall LONG WIN_FUNC(RtlCompareString,3)
	(const struct ansi_string *s1, const struct ansi_string *s2,
	 BOOLEAN case_insensitive)
{
	unsigned int len;
	LONG ret = 0;
	const char *p1, *p2;

	ENTER2("");
	len = min(s1->length, s2->length);
	p1 = s1->buf;
	p2 = s2->buf;
	if (case_insensitive)
		while (!ret && len--)
			ret = toupper(*p1++) - toupper(*p2++);
	else
		while (!ret && len--)
			ret = *p1++ - *p2++;
	if (!ret)
		ret = s1->length - s2->length;
	EXIT2(return ret);
}

wstdcall LONG WIN_FUNC(RtlCompareUnicodeString,3)
	(const struct unicode_string *s1, const struct unicode_string *s2,
	 BOOLEAN case_insensitive)
{
	unsigned int len;
	LONG ret = 0;
	const wchar_t *p1, *p2;

	ENTER2("");

	len = min(s1->length, s2->length) / sizeof(wchar_t);
	p1 = s1->buf;
	p2 = s2->buf;
	if (case_insensitive)
		while (!ret && len--)
			ret = toupper((u8)*p1++) - toupper((u8)*p2++);
	else
		while (!ret && len--)
			ret = (u8)*p1++ - (u8)*p2++;
	if (!ret)
		ret = s1->length - s2->length;
	TRACE2("len: %d, ret: %d", len, ret);
	EXIT2(return ret);
}

wstdcall BOOLEAN WIN_FUNC(RtlEqualString,3)
	(const struct ansi_string *s1, const struct ansi_string *s2,
	 BOOLEAN case_insensitive)
{
	ENTER1("");
	if (s1->length != s2->length)
		return FALSE;
	return !RtlCompareString(s1, s2, case_insensitive);
}

wstdcall BOOLEAN WIN_FUNC(RtlEqualUnicodeString,3)
	(const struct unicode_string *s1, const struct unicode_string *s2,
	 BOOLEAN case_insensitive)
{
	if (s1->length != s2->length)
		return FALSE;
	return !RtlCompareUnicodeString(s1, s2, case_insensitive);
}

wstdcall void WIN_FUNC(RtlCopyUnicodeString,2)
	(struct unicode_string *dst, struct unicode_string *src)
{
	ENTER1("%p, %p", dst, src);
	if (src && src->buf && dst->buf) {
		dst->length = min(src->length, dst->max_length);
		memcpy(dst->buf, src->buf, dst->length);
		if (dst->length < dst->max_length)
			dst->buf[dst->length / sizeof(dst->buf[0])] = 0;
	} else
		dst->length = 0;
	EXIT1(return);
}

wstdcall void WIN_FUNC(RtlCopyString,2)
	(struct ansi_string *dst, struct ansi_string *src)
{
	ENTER1("%p, %p", dst, src);
	if (src && src->buf && dst->buf) {
		dst->length = min(src->length, dst->max_length);
		memcpy(dst->buf, src->buf, dst->length);
		if (dst->length < dst->max_length)
			dst->buf[dst->length] = 0;
	} else
		dst->length = 0;
	EXIT1(return);
}

wstdcall NTSTATUS WIN_FUNC(RtlAppendUnicodeToString,2)
	(struct unicode_string *dst, wchar_t *src)
{
	if (src) {
		int len;
		for (len = 0; src[len]; len++)
			;
		if (dst->length + (len * sizeof(dst->buf[0])) > dst->max_length)
			return STATUS_BUFFER_TOO_SMALL;
		memcpy(&dst->buf[dst->length], src, len * sizeof(dst->buf[0]));
		dst->length += len * sizeof(dst->buf[0]);
		if (dst->max_length > dst->length)
			dst->buf[dst->length / sizeof(dst->buf[0])] = 0;
	}
	return STATUS_SUCCESS;
}

wstdcall NTSTATUS WIN_FUNC(RtlAppendUnicodeStringToString,2)
	(struct unicode_string *dst, struct unicode_string *src)
{
	if (dst->max_length < src->length + dst->length)
		return STATUS_BUFFER_TOO_SMALL;
	if (src->length) {
		memcpy(&dst->buf[dst->length], src->buf, src->length);
		dst->length += src->length;
		if (dst->max_length > dst->length)
			dst->buf[dst->length / sizeof(dst->buf[0])] = 0;
	}
	EXIT2(return STATUS_SUCCESS);
}

wstdcall ULONG WIN_FUNC(RtlxAnsiStringToUnicodeSize,1)
	(const struct ansi_string *string)
{
	int i;

	for (i = 0; i < string->max_length && string->buf[i]; i++)
		;
	return i * sizeof(wchar_t);
}

wstdcall ULONG WIN_FUNC(RtlxUnicodeStringToAnsiSize,1)
	(const struct unicode_string *string)
{
	int i;

	for (i = 0; i < string->max_length && string->buf[i]; i++)
		;
	return i;
}

wstdcall NTSTATUS WIN_FUNC(RtlAnsiStringToUnicodeString,3)
	(struct unicode_string *dst, const struct ansi_string *src,
	 BOOLEAN alloc)
{
	int i, n;

	n = RtlxAnsiStringToUnicodeSize(src);
	TRACE2("%d, %d, %d, %d, %p", n, dst->max_length, src->length,
	       src->max_length, src->buf);
	if (alloc == TRUE) {
#if 0
		if (n == 0) {
			dst->length = dst->max_length = 0;
			dst->buf = NULL;
			EXIT2(return STATUS_SUCCESS);
		}
#endif
		dst->max_length = n + sizeof(dst->buf[0]);
		dst->buf = ExAllocatePoolWithTag(NonPagedPool,
						 dst->max_length, 0);
		if (!dst->buf) {
			dst->max_length = dst->length = 0;
			EXIT2(return STATUS_NO_MEMORY);
		}
	} else if (dst->max_length < n)
		EXIT2(return STATUS_BUFFER_TOO_SMALL);

	dst->length = n;
	n /= sizeof(dst->buf[0]);
	for (i = 0; i < n; i++)
		dst->buf[i] = src->buf[i];
	if (i * sizeof(dst->buf[0]) < dst->max_length)
		dst->buf[i] = 0;
	TRACE2("dst: length: %d, max_length: %d, string: %p",
	       dst->length, dst->max_length, src->buf);
	EXIT2(return STATUS_SUCCESS);
}

wstdcall NTSTATUS WIN_FUNC(RtlUnicodeStringToAnsiString,3)
	(struct ansi_string *dst, const struct unicode_string *src,
	 BOOLEAN alloc)
{
	int i, n;

	n = RtlxUnicodeStringToAnsiSize(src);
	TRACE2("%d, %d, %d, %d, %p", n, dst->max_length, src->length,
	       src->max_length, src->buf);
	if (alloc == TRUE) {
#if 0
		if (n == 0) {
			dst->length = dst->max_length = 0;
			dst->buf = NULL;
			EXIT2(return STATUS_SUCCESS);
		}
#endif
		dst->max_length = n + sizeof(dst->buf[0]);
		dst->buf = ExAllocatePoolWithTag(NonPagedPool,
						 dst->max_length, 0);
		if (!dst->buf) {
			dst->max_length = dst->length = 0;
			EXIT1(return STATUS_NO_MEMORY);
		}
	} else if (dst->max_length < n)
		EXIT2(return STATUS_BUFFER_TOO_SMALL);

	dst->length = n;
	for (i = 0; i < n; i++)
		dst->buf[i] = src->buf[i];
	if (i < dst->max_length)
		dst->buf[i] = 0;
	TRACE2("string: %p, len: %d(%d)", dst->buf, dst->length,
	       dst->max_length);
	EXIT2(return STATUS_SUCCESS);
}

wstdcall NTSTATUS WIN_FUNC(RtlUnicodeStringToInteger,3)
	(struct unicode_string *ustring, ULONG base, ULONG *value)
{
	int i, sign = 1;
	ULONG res;
	typeof(ustring->buf) string;

	if (ustring->length == 0) {
		*value = 0;
		return STATUS_SUCCESS;
	}

	string = ustring->buf;
	i = 0;
	while (i < (ustring->length / sizeof(*string)) && string[i] == ' ')
		i++;
	if (string[i] == '+')
		i++;
	else if (string[i] == '-') {
		i++;
		sign = -1;
	}
	if (base == 0) {
		base = 10;
		if (i <= ((ustring->length / sizeof(*string)) - 2) &&
		    string[i] == '0') {
			i++;
			if (string[i] == 'b') {
				base = 2;
				i++;
			} else if (string[i] == 'o') {
				base = 8;
				i++;
			} else if (string[i] == 'x') {
				base = 16;
				i++;
			}
		}
	}
	if (!(base == 2 || base == 8 || base == 10 || base == 16))
		EXIT2(return STATUS_INVALID_PARAMETER);

	for (res = 0; i < (ustring->length / sizeof(*string)); i++) {
		int v;
		if (isdigit((char)string[i]))
			v = string[i] - '0';
		else if (isxdigit((char)string[i]))
			v = tolower((char)string[i]) - 'a' + 10;
		else
			v = base;
		if (v >= base)
			EXIT2(return STATUS_INVALID_PARAMETER);
		res = res * base + v;
	}
	*value = sign * res;
	EXIT3(return STATUS_SUCCESS);
}

wstdcall NTSTATUS WIN_FUNC(RtlCharToInteger,3)
	(const char *string, ULONG base, ULONG *value)
{
	int sign = 1;
	ULONG res;

	if (!string || !value)
		EXIT2(return STATUS_INVALID_PARAMETER);
	while (*string == ' ')
		string++;
	if (*string == '+')
		string++;
	else if (*string == '-') {
		string++;
		sign = -1;
	}
	if (base == 0) {
		base = 10;
		if (*string == '0') {
			string++;
			if (*string == 'b') {
				base = 2;
				string++;
			} else if (*string == 'o') {
				base = 8;
				string++;
			} else if (*string == 'x') {
				base = 16;
				string++;
			}
		}
	}
	if (!(base == 2 || base == 8 || base == 10 || base == 16))
		EXIT2(return STATUS_INVALID_PARAMETER);

	for (res = 0; *string; string++) {
		int v;
		if (isdigit(*string))
			v = *string - '0';
		else if (isxdigit(*string))
			v = tolower(*string) - 'a' + 10;
		else
			v = base;
		if (v >= base)
			EXIT2(return STATUS_INVALID_PARAMETER);
		res = res * base + v;
	}
	*value = sign * res;
	EXIT3(return STATUS_SUCCESS);
}

wstdcall NTSTATUS WIN_FUNC(RtlIntegerToUnicodeString,3)
	(ULONG value, ULONG base, struct unicode_string *ustring)
{
	typeof(ustring->buf) buf = ustring->buf;
	int i;

	if (base == 0)
		base = 10;
	if (!(base == 2 || base == 8 || base == 10 || base == 16))
		return STATUS_INVALID_PARAMETER;
	for (i = 0; value && i < ustring->max_length / sizeof(*buf); i++) {
		int r;
		r = value % base;
		value /= base;
		if (r < 10)
			buf[i] = r + '0';
		else
			buf[i] = r + 'a' - 10;
	}
	if (value)
		return STATUS_BUFFER_OVERFLOW;
	ustring->length = i * sizeof(*buf);
	return STATUS_SUCCESS;
}

wstdcall LARGE_INTEGER WIN_FUNC(RtlConvertUlongToLargeInteger,1)
	(ULONG ul)
{
	LARGE_INTEGER li = ul;
	return li;
}

wfastcall USHORT WIN_FUNC(RtlUshortByteSwap,1)
	(USHORT src)
{
	return __swab16(src);
}

wfastcall ULONG WIN_FUNC(RtlUlongByteSwap,1)
	(ULONG src)
{
	/* ULONG is 32 bits for both 32-bit and 64-bit architectures */
	return __swab32(src);
}

wstdcall NTSTATUS WIN_FUNC(RtlUpcaseUnicodeString,3)
	(struct unicode_string *dst, struct unicode_string *src, BOOLEAN alloc)
{
	USHORT i, n;

	if (alloc) {
		dst->buf = ExAllocatePoolWithTag(NonPagedPool, src->length, 0);
		if (dst->buf)
			dst->max_length = src->length;
		else
			EXIT2(return STATUS_NO_MEMORY);
	} else {
		if (dst->max_length < src->length)
			EXIT2(return STATUS_BUFFER_OVERFLOW);
	}

	n = src->length / sizeof(src->buf[0]);
	for (i = 0; i < n; i++)
		dst->buf[i] = toupper(src->buf[i]);

	dst->length = src->length;
	EXIT3(return STATUS_SUCCESS);
}

wstdcall void WIN_FUNC(RtlInitUnicodeString,2)
	(struct unicode_string *dst, const wchar_t *src)
{
	ENTER2("%p", dst);
	if (dst == NULL)
		EXIT1(return);
	if (src == NULL) {
		dst->max_length = dst->length = 0;
		dst->buf = NULL;
	} else {
		int i;
		for (i = 0; (char)src[i]; i++)
			;
		dst->buf = (typeof(dst->buf))src;
		dst->length = i * sizeof(dst->buf[0]);
		dst->max_length = (i + 1) * sizeof(dst->buf[0]);
	}
	EXIT1(return);
}

wstdcall void WIN_FUNC(RtlInitAnsiString,2)
	(struct ansi_string *dst, const char *src)
{
	ENTER2("%p", dst);
	if (dst == NULL)
		EXIT2(return);
	if (src == NULL) {
		dst->max_length = dst->length = 0;
		dst->buf = NULL;
	} else {
		int i;
		for (i = 0; src[i]; i++)
			;
		dst->buf = (typeof(dst->buf))src;
		dst->length = i;
		dst->max_length = i + 1;
	}
	TRACE2("%p", dst->buf);
	EXIT2(return);
}

wstdcall void WIN_FUNC(RtlInitString,2)
	(struct ansi_string *dst, const char *src)
{
	ENTER2("%p", dst);
	RtlInitAnsiString(dst, src);
	EXIT2(return);
}

wstdcall void WIN_FUNC(RtlFreeUnicodeString,1)
	(struct unicode_string *string)
{
	ENTER2("%p", string);
	if (string == NULL)
		return;
	if (string->buf)
		ExFreePool(string->buf);
	string->length = string->max_length = 0;
	string->buf = NULL;
	return;
}

wstdcall void WIN_FUNC(RtlFreeAnsiString,1)
	(struct ansi_string *string)
{
	ENTER2("%p", string);
	if (string == NULL)
		return;
	if (string->buf)
		ExFreePool(string->buf);
	string->length = string->max_length = 0;
	string->buf = NULL;
	return;
}

/* guid string is of the form: {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX} */
wstdcall NTSTATUS WIN_FUNC(RtlGUIDFromString,2)
	(struct unicode_string *guid_string, struct guid *guid)
{
	struct ansi_string ansi;
	NTSTATUS ret;
	int i, j, k, l, m;

	ret = RtlUnicodeStringToAnsiString(&ansi, guid_string, TRUE);
	if (ret != STATUS_SUCCESS)
		return ret;
	if (ansi.length != 37 || ansi.buf[0] != '{' ||
	    ansi.buf[36] != '}' || ansi.buf[9] != '-' ||
	    ansi.buf[14] != '-' || ansi.buf[19] != '-' ||
	    ansi.buf[24] != '-') {
		RtlFreeAnsiString(&ansi);
		EXIT2(return STATUS_INVALID_PARAMETER);
	}
	memcpy(&guid->data4, &ansi.buf[29], sizeof(guid->data3));
	/* set end of data3 for scanf */
	ansi.buf[29] = 0;
	if (sscanf(&ansi.buf[1], "%x", &i) == 1 &&
	    sscanf(&ansi.buf[10], "%x", &j) == 1 &&
	    sscanf(&ansi.buf[15], "%x", &k) == 1 &&
	    sscanf(&ansi.buf[20], "%x", &l) == 1 &&
	    sscanf(&ansi.buf[25], "%x", &m) == 1) {
		guid->data1 = (i << 16) | (j < 8) | k;
		guid->data2 = l;
		guid->data3 = m;
		ret = STATUS_SUCCESS;
	} else
		ret = STATUS_INVALID_PARAMETER;
	RtlFreeAnsiString(&ansi);
	return ret;
}

wstdcall NTSTATUS WIN_FUNC(RtlQueryRegistryValues,5)
	(ULONG relative, wchar_t *path, struct rtl_query_registry_table *tbl,
	 void *context, void *env)
{
	struct ansi_string ansi;
	struct unicode_string unicode;
	NTSTATUS status, ret;
	static int i = 0;

	ENTER3("%x, %p", relative, tbl);
//	TODO();

	RtlInitUnicodeString(&unicode, path);
	if (RtlUnicodeStringToAnsiString(&ansi, &unicode, TRUE) ==
	    STATUS_SUCCESS) {
		TRACE2("%s", ansi.buf);
		RtlFreeAnsiString(&ansi);
	}
	ret = STATUS_SUCCESS;
	for (; tbl->name; tbl++) {
		RtlInitUnicodeString(&unicode, tbl->name);
		if (RtlUnicodeStringToAnsiString(&ansi, &unicode, TRUE) ==
		    STATUS_SUCCESS) {
			TRACE2("name: %s", ansi.buf);
			RtlFreeAnsiString(&ansi);
		}
		TRACE2("flags: %08X", tbl->flags);
		if (tbl->flags == RTL_QUERY_REGISTRY_DIRECT) {
			TRACE2("type: %08X", tbl->def_type);
			if (tbl->def_type == REG_DWORD) {
				/* Atheros USB driver needs this, but
				 * don't know where and how to get its
				 * value */
				if (tbl->def_data) {
					TRACE2("def_data: %x",
					       *(int *)tbl->def_data);
					*(DWORD *)tbl->context = 0x5f292a + i++;
//						*(DWORD *)tbl->def_data;
				} else
					*(DWORD *)tbl->context = 0x2345dbe;
			}
		} else {
			void *data;
			ULONG type, length;

			if (!tbl->query_func) {
				ERROR("oops: no query_func");
				ret = STATUS_INVALID_PARAMETER;
				break;
			}
			if (tbl->flags & RTL_QUERY_REGISTRY_NOVALUE) {
				data = NULL;
				type = REG_NONE;
				length = 0;
			} else {
				data = tbl->def_data;
				type = tbl->def_type;
				length = tbl->def_length;;
			}
			TRACE2("calling query_func: %p", tbl->query_func);
			status = LIN2WIN6(tbl->query_func, tbl->name, type,
					  data, length, context, env);
			TRACE2("status: %08X", status);
			if (status) {
				if (status == STATUS_BUFFER_TOO_SMALL)
					ret = STATUS_BUFFER_TOO_SMALL;
				else
					EXIT2(return STATUS_INVALID_PARAMETER);
			}
		}
	}
	EXIT3(return ret);
}

wstdcall NTSTATUS WIN_FUNC(RtlWriteRegistryValue,6)
	(ULONG relative, wchar_t *path, wchar_t *name, ULONG type,
	 void *data, ULONG length)
{
	struct ansi_string ansi;
	struct unicode_string unicode;

	ENTER3("%d", relative);
	TODO();

	RtlInitUnicodeString(&unicode, path);
	if (RtlUnicodeStringToAnsiString(&ansi, &unicode, TRUE) ==
	    STATUS_SUCCESS) {
		TRACE2("%s", ansi.buf);
		RtlFreeAnsiString(&ansi);
	}
	RtlInitUnicodeString(&unicode, name);
	if (RtlUnicodeStringToAnsiString(&ansi, &unicode, TRUE) ==
	    STATUS_SUCCESS) {
		TRACE2("%s", ansi.buf);
		RtlFreeAnsiString(&ansi);
	}
	EXIT5(return STATUS_SUCCESS);
}

wstdcall NTSTATUS WIN_FUNC(RtlDeleteRegistryValue,3)
	(ULONG relative, wchar_t *path, wchar_t *name)
{
	return STATUS_SUCCESS;
}

wstdcall void WIN_FUNC(RtlAssert,4)
	(char *failed_assertion, char *file_name, ULONG line_num, char *message)
{
	ERROR("assertion '%s' failed at %s line %d%s",
	      failed_assertion, file_name, line_num, message ? message : "");
	return;
}

wstdcall void WIN_FUNC(RtlUnwind,0)
	(void)
{
	TODO();
}

wstdcall void WIN_FUNC(RtlRaiseException,1)
	(void *exception_record)
{
	TODO();
}

int rtl_init(void)
{
	return 0;
}

/* called when module is being removed */
void rtl_exit(void)
{
	EXIT4(return);
}
