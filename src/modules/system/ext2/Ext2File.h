/*
 * Copyright (c) 2008-2014, Pedigree Developers
 *
 * Please see the CONTRIB file in the root of the source tree for a full
 * list of contributors.
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

#ifndef EXT2_FILE_H
#define EXT2_FILE_H

#include "Ext2Node.h"
#include "modules/system/vfs/File.h"
#include "pedigree/kernel/processor/types.h"
#include "pedigree/kernel/utilities/String.h"

struct Inode;

/** A File is a file, a directory or a symlink. */
class Ext2File : public File, public Ext2Node
{
  private:
    /** Copy constructors are hidden - unused! */
    Ext2File(const Ext2File &file);
    Ext2File &operator=(const Ext2File &);

  public:
    /** Constructor, should be called only by a Filesystem. */
    Ext2File(
        const String &name, uintptr_t inode_num, Inode *inode,
        class Ext2Filesystem *pFs, File *pParent = 0);
    /** Destructor */
    virtual ~Ext2File();

    virtual void preallocate(size_t expectedSize, bool zero=true);

    virtual void extend(size_t newSize);
    virtual void extend(size_t newSize, uint64_t location, uint64_t size);

    virtual void truncate();

    /** Updates inode attributes. */
    void fileAttributeChanged();

    virtual uintptr_t readBlock(uint64_t location);
    virtual void writeBlock(uint64_t location, uintptr_t addr);

    virtual void pinBlock(uint64_t location);
    virtual void unpinBlock(uint64_t location);

    using File::sync;
    virtual void sync(size_t offset, bool async);

    virtual size_t getBlockSize() const;
};

#endif
