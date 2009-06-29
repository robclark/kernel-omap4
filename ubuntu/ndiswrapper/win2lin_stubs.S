/*
 *  Copyright (C) 2005 Karl Vogel, Giridhar Pemmasani
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

#include <linux/linkage.h>

#ifdef CONFIG_X86_64

/*
# Windows <---> Linux register usage conversion when calling functions
# V = Volatile
# NV = Non Volatile (needs to be saved)
#
#         Win                     Lin
# ---------------------------------------
# Rax    Return           V       Return          V
# Rbx                     NV                      NV
# Rcx     Arg1            V       Arg4            V
# Rdx     Arg2            V       Arg3            V
# Rsi                     NV      Arg2            V
# Rdi                     NV      Arg1            V
# Rsp                     NV                      NV
# Rbp                     NV                      NV
# R8      Arg3            V       Arg5            V
# R9      Arg4            V       Arg6            V
# R10                     V                       V
# R11                     V                       V
# R12                     NV                      NV
# R13                     NV                      NV
# R14                     NV                      NV
# R15                     NV                      NV
#
# In addition, Linux uses %rax to indicate number of SSE registers used
# when variadic functions are called. Since there is no way to obtain this
# from Windows, for now, we just assume this is 0 (hence %rax is cleared).
#
# Windows pushes arguments 5 and higher onto stack in case of integer
# variables and 4 and higher in case of floating point variabes (passed
# in SSE registers).

In a windows function, the stackframe/registers look like this:

# 0x0048 ....
# 0x0040 arg8
# 0x0038 arg7
# 0x0030 arg6
# 0x0028 arg5
# 0x0020 shadow/spill space for arg4
# 0x0018 shadow/spill space for arg3
# 0x0010 shadow/spill space for arg2
# 0x0008 shadow/spill space for arg1
# 0x0000 ret

# register spill space is same irrespective of number of arguments - even
# if Windows function takes less than 4 arguments, 32 bytes above return
# address is reserved for the function

In Linux it should look like:

# 0x0018 ....
# 0x0010 arg8
# 0x0008 arg7
# 0x0000 ret

*/

#
# setup for Windows to Linux function call
#

	.text

.macro win2lin_prolog
	push	%rsi
	push	%rdi
.endm

.macro win2lin_epilog
	pop	%rdi
	pop	%rsi
.endm

# when Windows function calls Linux function, the function address is in %r10

.macro call_lin_func
	xor	%rax, %rax	# rax indicates number of SSE regs
	call	*%r10
.endm

# before prolog, 0(%rsp) is return address, 8(%rsp) would be arg1
# (but it is in register) and so on, so n'th arg would be at n*8(%rsp)
# for n > 4. But in prolog, we push 2 registers that are non-volaile in
# Windows, but volatile in Linux. So after prolog, args are at (n+2)*8(%rsp)

#define win2lin_win_arg(n) (n+2)*8(%rsp)

#define win2lin_arg1 mov %rcx, %rdi
#define win2lin_arg2 mov %rdx, %rsi
#define win2lin_arg3 mov %r8, %rdx
#define win2lin_arg4 mov %r9, %rcx
#define win2lin_arg5 mov win2lin_win_arg(5), %r8
#define win2lin_arg6 mov win2lin_win_arg(6), %r9

	.type	win2lin0, @function
win2lin0:
	win2lin_prolog
	call_lin_func
	win2lin_epilog
	ret
	.size	win2lin0, .-win2lin0

	.type	win2lin1, @function
win2lin1:
	win2lin_prolog
	win2lin_arg1
	call_lin_func
	win2lin_epilog
	ret
	.size	win2lin1, .-win2lin1

	.type	win2lin2, @function
win2lin2:
	win2lin_prolog
	win2lin_arg1
	win2lin_arg2
	call_lin_func
	win2lin_epilog
	ret
	.size	win2lin2, .-win2lin2

	.type	win2lin3, @function
win2lin3:
	win2lin_prolog
	win2lin_arg1
	win2lin_arg2
	win2lin_arg3
	call_lin_func
	win2lin_epilog
	ret
	.size	win2lin3, .-win2lin3

	.type	win2lin4, @function
win2lin4:
	win2lin_prolog
	win2lin_arg1
	win2lin_arg2
	win2lin_arg3
	win2lin_arg4
	call_lin_func
	win2lin_epilog
	ret
	.size	win2lin4, .-win2lin4

	.type	win2lin5, @function
win2lin5:
	win2lin_prolog
	win2lin_arg1
	win2lin_arg2
	win2lin_arg3
	win2lin_arg4
	win2lin_arg5
	call_lin_func
	win2lin_epilog
	ret
	.size	win2lin5, .-win2lin5

	.type	win2lin6, @function
win2lin6:
	win2lin_prolog
	win2lin_arg1
	win2lin_arg2
	win2lin_arg3
	win2lin_arg4
	win2lin_arg5
	win2lin_arg6
	call_lin_func
	win2lin_epilog
	ret
	.size	win2lin6, .-win2lin6

# Allocate stack frame for Linux arguments before calling function.
# First 6 args are passed through registers, so we need space for 7 and above.
# The arguments should have been copied onto stack already.

.macro call_lin_func_args n
	sub $(\n-6)*8, %rsp
	call_lin_func
	add $(\n-6)*8, %rsp
	.endm

# m is index of Linux arg required, n is total number of args to function
# After stack frame is allocated, Linux arg 7 should be at 0(%rsp),
# arg 8 should be at 1*8(%rsp) and so on. So Linux arg m should be at (m-7)*8
# Stack frame starts at -(n-6)*8(%rsp), so before stack frame is allocated
# Linux arg m should be at (6-n+m-7)*8(%rsp)

#define win2lin_lin_arg(m,n) (m-1-n)*8(%rsp)

	.type	win2lin7, @function
win2lin7:
	win2lin_prolog

	win2lin_arg1
	win2lin_arg2
	win2lin_arg3
	win2lin_arg4
	win2lin_arg5
	win2lin_arg6

	# copy windows argument 7 onto stack for Linux function
	mov	win2lin_win_arg(7), %r11
	mov	%r11, win2lin_lin_arg(7,7)

	call_lin_func_args(7)
	win2lin_epilog
	ret
	.size	win2lin7, .-win2lin7

	.type	win2lin8, @function
win2lin8:
	win2lin_prolog

	win2lin_arg1
	win2lin_arg2
	win2lin_arg3
	win2lin_arg4
	win2lin_arg5
	win2lin_arg6

	# copy windows arguments 7 and 8 onto stack for Linux function
	mov	win2lin_win_arg(7), %r11
	mov	%r11, win2lin_lin_arg(7,8)
	mov	win2lin_win_arg(8), %r11
	mov	%r11, win2lin_lin_arg(8,8)

	call_lin_func_args(8)
	win2lin_epilog
	ret
	.size	win2lin8, .-win2lin8

	.type	win2lin9, @function
win2lin9:
win2lin10:
win2lin11:
win2lin12:
	win2lin_prolog

	# since we destroy rsi and rdi here, first copy windows
	# arguments 7 through 12 onto stack for Linux function
	mov	%rcx, %r11		# save rcx
	lea	win2lin_win_arg(7), %rsi	# source (windows arg 7 and up)
	lea	win2lin_lin_arg(7,12), %rdi	# = destination
	mov	$6, %rcx			# 6 arguments
	rep
	movsq
	mov	%r11, %rcx		# restore rcx

	win2lin_arg1
	win2lin_arg2
	win2lin_arg3
	win2lin_arg4
	win2lin_arg5
	win2lin_arg6

	call_lin_func_args(12)
	win2lin_epilog
	ret
	.size	win2lin9, .-win2lin9

#define win2lin(name, argc)			\
ENTRY(win2lin_ ## name ## _ ## argc)		\
	lea	name(%rip), %r10 ; 		\
	jmp	win2lin ## argc

#include "win2lin_stubs.h"

#endif // CONFIG_X86_64
