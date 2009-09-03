/*
 * Copyright (c) 2008 James Molloy, Jörg Pfähler, Matthew Iselin
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef PC102ENUS_H
#define PC102ENUS_H

#define UNICODE_POINT    0x80000000
#define SPECIAL          0x40000000
#define SPARSE_NULL      0x00000000
#define SPARSE_DATA_FLAG 0x8000

#define NONE_I           0x0
#define SHIFT_I          0x1
#define CTRL_I           0x2

#define ALT_I            0x1
#define ALTGR_I          0x2

#define TABLE_IDX(alt, modifiers, escape, scancode) ( ((alt&3)<<10) | ((modifiers&3)<<8) | ((escape&1)<<7) | (scancode&0x7F) )
#define TABLE_MAX TABLE_IDX(2,3,1,128)

typedef struct table_entry
{
    uint32_t flags;
    uint32_t val;
} table_entry_t;

typedef struct sparse_entry
{
    uint16_t left;
    uint16_t right;
} sparse_t;

char sparse_buff[109] =
"\x04\x00\x44\x00\x08\x00\x00\x00\x0c\x00\x00\x00\x10\x00\x28\x00\x14\x00\x20\x00\x18\
\x00\x1c\x00\x00\x80\xb8\x80\x70\x81\x28\x82\x00\x00\x24\x00\xe0\x82\x98\x83\x2c\
\x00\x38\x00\x30\x00\x34\x00\x50\x84\x08\x85\x00\x00\xc0\x85\x3c\x00\x40\x00\x78\
\x86\x30\x87\xe8\x87\x00\x00\x48\x00\x00\x00\x00\x00\x4c\x00\x00\x00\x50\x00\x54\
\x00\x00\x00\x00\x00\x58\x00\x00\x00\xa0\x88\x34\x00\x00\x00\x00\x00\x00\x80\x35\
\x00\x00\x00\x00\x00\x00\x80";

char data_buff[2593] =
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x1b\x00\x00\x00\x00\x00\x00\x80\x31\
\x00\x00\x00\x00\x00\x00\x80\x32\x00\x00\x00\x00\x00\x00\x80\x33\x00\x00\x00\x00\
\x00\x00\x80\x34\x00\x00\x00\x00\x00\x00\x80\x35\x00\x00\x00\x00\x00\x00\x80\x36\
\x00\x00\x00\x00\x00\x00\x80\x37\x00\x00\x00\x00\x00\x00\x80\x38\x00\x00\x00\x00\
\x00\x00\x80\x39\x00\x00\x00\x00\x00\x00\x80\x30\x00\x00\x00\x00\x00\x00\x80\x2d\
\x00\x00\x00\x00\x00\x00\x80\x3d\x00\x00\x00\x00\x00\x00\x80\x08\x00\x00\x00\x00\
\x00\x00\x80\x09\x00\x00\x00\x00\x00\x00\x80\x71\x00\x00\x00\x00\x00\x00\x80\x77\
\x00\x00\x00\x00\x00\x00\x80\x65\x00\x00\x00\x00\x00\x00\x80\x72\x00\x00\x00\x00\
\x00\x00\x80\x74\x00\x00\x00\x00\x00\x00\x80\x79\x00\x00\x00\x00\x00\x00\x80\x75\
\x00\x00\x00\x00\x00\x00\x80\x69\x00\x00\x00\x00\x00\x00\x80\x6f\x00\x00\x00\x00\
\x00\x00\x80\x70\x00\x00\x00\x00\x00\x00\x80\x5b\x00\x00\x00\x00\x00\x00\x80\x5d\
\x00\x00\x00\x00\x00\x00\x80\x0a\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x80\x61\x00\x00\x00\x00\x00\x00\x80\x73\x00\x00\x00\x00\x00\x00\x80\x64\
\x00\x00\x00\x00\x00\x00\x80\x66\x00\x00\x00\x00\x00\x00\x80\x67\x00\x00\x00\x00\
\x00\x00\x80\x68\x00\x00\x00\x00\x00\x00\x80\x6a\x00\x00\x00\x00\x00\x00\x80\x6b\
\x00\x00\x00\x00\x00\x00\x80\x6c\x00\x00\x00\x00\x00\x00\x80\x3b\x00\x00\x00\x00\
\x00\x00\x80\x27\x00\x00\x00\x00\x00\x00\x80\x60\x00\x00\x00\x01\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x80\x5c\x00\x00\x00\x00\x00\x00\x80\x7a\x00\x00\x00\x00\
\x00\x00\x80\x78\x00\x00\x00\x00\x00\x00\x80\x63\x00\x00\x00\x00\x00\x00\x80\x76\
\x00\x00\x00\x00\x00\x00\x80\x62\x00\x00\x00\x00\x00\x00\x80\x6e\x00\x00\x00\x00\
\x00\x00\x80\x6d\x00\x00\x00\x00\x00\x00\x80\x2c\x00\x00\x00\x00\x00\x00\x80\x2e\
\x00\x00\x00\x00\x00\x00\x80\x2f\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x80\x2a\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x20\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x37\x00\x00\x00\x00\x00\x00\x80\x38\
\x00\x00\x00\x00\x00\x00\x80\x39\x00\x00\x00\x00\x00\x00\x80\x2d\x00\x00\x00\x00\
\x00\x00\x80\x34\x00\x00\x00\x00\x00\x00\x80\x35\x00\x00\x00\x00\x00\x00\x80\x36\
\x00\x00\x00\x00\x00\x00\x80\x2b\x00\x00\x00\x00\x00\x00\x80\x31\x00\x00\x00\x00\
\x00\x00\x80\x32\x00\x00\x00\x00\x00\x00\x80\x33\x00\x00\x00\x00\x00\x00\x80\x30\
\x00\x00\x00\x00\x00\x00\x80\x2e\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x80\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x80\x2f\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x40\x68\x6f\x6d\x65\x00\x00\x00\x40\x75\x70\x00\xf2\x00\
\x00\x00\x40\x70\x67\x75\x70\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x40\x6c\
\x65\x66\x74\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x40\x72\x69\x67\x68\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x40\x65\x6e\x64\x00\x00\x00\x00\x40\x64\
\x6f\x77\x6e\x00\x00\x00\x40\x70\x67\x64\x6e\x00\x00\x00\x40\x69\x6e\x73\x00\x00\
\x00\x00\x40\x64\x65\x6c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x21\x00\x00\x00\x00\
\x00\x00\x80\x40\x00\x00\x00\x00\x00\x00\x80\x23\x00\x00\x00\x00\x00\x00\x80\x24\
\x00\x00\x00\x00\x00\x00\x80\x25\x00\x00\x00\x00\x00\x00\x80\x5e\x00\x00\x00\x00\
\x00\x00\x80\x26\x00\x00\x00\x00\x00\x00\x80\x2a\x00\x00\x00\x00\x00\x00\x80\x28\
\x00\x00\x00\x00\x00\x00\x80\x29\x00\x00\x00\x00\x00\x00\x80\x5f\x00\x00\x00\x00\
\x00\x00\x80\x2b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x80\x51\x00\x00\x00\x00\x00\x00\x80\x57\x00\x00\x00\x00\
\x00\x00\x80\x45\x00\x00\x00\x00\x00\x00\x80\x52\x00\x00\x00\x00\x00\x00\x80\x54\
\x00\x00\x00\x00\x00\x00\x80\x59\x00\x00\x00\x00\x00\x00\x80\x55\x00\x00\x00\x00\
\x00\x00\x80\x49\x00\x00\x00\x00\x00\x00\x80\x4f\x00\x00\x00\x00\x00\x00\x80\x50\
\x00\x00\x00\x00\x00\x00\x80\x7b\x00\x00\x00\x00\x00\x00\x80\x7d\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x41\
\x00\x00\x00\x00\x00\x00\x80\x53\x00\x00\x00\x00\x00\x00\x80\x44\x00\x00\x00\x00\
\x00\x00\x80\x46\x00\x00\x00\x00\x00\x00\x80\x47\x00\x00\x00\x00\x00\x00\x80\x48\
\x00\x00\x00\x00\x00\x00\x80\x4a\x00\x00\x00\x00\x00\x00\x80\x4b\x00\x00\x00\x00\
\x00\x00\x80\x4c\x00\x00\x00\x00\x00\x00\x80\x3a\x00\x00\x00\x00\x00\x00\x80\x22\
\x00\x00\x00\x00\x00\x00\x80\x7e\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x80\x7c\x00\x00\x00\x00\x00\x00\x80\x5a\x00\x00\x00\x00\x00\x00\x80\x58\
\x00\x00\x00\x00\x00\x00\x80\x43\x00\x00\x00\x00\x00\x00\x80\x56\x00\x00\x00\x00\
\x00\x00\x80\x42\x00\x00\x00\x00\x00\x00\x80\x4e\x00\x00\x00\x00\x00\x00\x80\x4d\
\x00\x00\x00\x00\x00\x00\x80\x3c\x00\x00\x00\x00\x00\x00\x80\x3e\x00\x00\x00\x00\
\x00\x00\x80\x3f\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x40\x68\x6f\x6d\x65\x00\x00\x00\x40\x75\x70\x00\x75\x00\
\x00\x00\x40\x70\x67\x75\x70\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x40\x6c\
\x65\x66\x74\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x40\x72\x69\x67\x68\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x40\x65\x6e\x64\x00\x00\x00\x00\x40\x64\
\x6f\x77\x6e\x00\x00\x00\x40\x70\x67\x64\x6e\x00\x00\x00\x40\x69\x6e\x73\x00\x00\
\x00\x00\x40\x64\x65\x6c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\xab\
\x00\x00\x00\x00\x00\x00\x80\xbb\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\
\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";

#endif
