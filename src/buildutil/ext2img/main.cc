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

#include <unistd.h>
#include <stdlib.h>

#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <vfs/VFS.h>
#include <vfs/File.h>
#include <vfs/Directory.h>
#include <vfs/Symlink.h>
#include <ext2/Ext2Filesystem.h>
#include <machine/Disk.h>
#include <utilities/String.h>

#include "DiskImage.h"

#define FS_ALIAS "fs"
#define TO_FS_PATH(x) String(FS_ALIAS "»") += x.c_str()

static bool ignoreErrors = false;
static size_t blocksPerRead = 64;

enum CommandType
{
    InvalidCommand,
    CreateDirectory,
    CreateSymlink,
    WriteFile,
};

struct Command
{
    Command() : what(InvalidCommand), params() {};

    CommandType what;
    std::vector<std::string> params;
};

extern bool msdosProbeDisk(Disk *pDisk);
extern bool appleProbeDisk(Disk *pDisk);

void syscallError(int e)
{
    std::cerr << "ERROR: #" << e << std::endl;
}

uint32_t getUnixTimestamp()
{
    return time(0);
}

bool writeFile(std::string source, std::string dest)
{
    std::ifstream ifs(source, std::ios::binary);
    if (ifs.bad() || ifs.fail())
    {
        std::cerr << "Could not open source file '" << source << "'." << std::endl;
        return false;
    }

    bool result = VFS::instance().createFile(TO_FS_PATH(dest), 0644);
    if (!result)
    {
        std::cerr << "Could not create destination file '" << dest << "'." << std::endl;
        return false;
    }

    File *pFile = VFS::instance().find(TO_FS_PATH(dest));
    if (!pFile)
    {
        std::cerr << "Couldn't open created destination file: '" << dest << "'." << std::endl;
        return false;
    }

    size_t blockSize = pFile->getBlockSize() * blocksPerRead;

    char *buffer = new char[blockSize];

    uint64_t offset = 0;
    while (!ifs.eof())
    {
        ifs.read(buffer, blockSize);
        uint64_t readCount = ifs.gcount();
        uint64_t count = 0;
        if (readCount)
        {
            count = pFile->write(offset, readCount, reinterpret_cast<uintptr_t>(buffer));
            if (!count || (count < readCount))
            {
                std::cerr << "Empty or short write to file '" << dest << "'." << std::endl;
                if (!ignoreErrors)
                    return false;
            }

            offset += readCount;
        }
    }

    return true;
}

bool createSymlink(std::string name, std::string target)
{
    bool result = VFS::instance().createSymlink(TO_FS_PATH(name), String(target.c_str()));
    if (!result)
    {
        std::cerr << "Could not create symlink '" << name << "' -> '" << target << "'." << std::endl;
        return false;
    }

    return true;
}

bool createDirectory(std::string dest)
{
    bool result = VFS::instance().createDirectory(TO_FS_PATH(dest));
    if (!result)
    {
        std::cerr << "Could not create directory '" << dest << "'." << std::endl;
        return false;
    }

    return true;
}

int handleImage(const char *image, std::vector<Command> &cmdlist, size_t part=0)
{
    // Prepare to probe ext2 filesystems via the VFS.
    VFS::instance().addProbeCallback(&Ext2Filesystem::probe);

    DiskImage mainImage(image);
    if (!mainImage.initialise())
    {
        std::cerr << "Couldn't load disk image!" << std::endl;
        return 1;
    }

    bool isFullFilesystem = false;
    if (!msdosProbeDisk(&mainImage))
    {
        std::cerr << "No MSDOS partition table found, trying an Apple partition table." << std::endl;
        if (!appleProbeDisk(&mainImage))
        {
            std::cerr << "No partition table found, assuming this is an ext2 filesystem." << std::endl;
            isFullFilesystem = true;
        }
    }

    size_t desiredPartition = part;

    Disk *pDisk = 0;

    if (isFullFilesystem)
        pDisk = &mainImage;
    else
    {
        // Find the nth partition.
        if (desiredPartition > mainImage.getNumChildren())
        {
            std::cerr << "Desired partition does not exist in this image." << std::endl;
            return 1;
        }

        pDisk = static_cast<Disk *>(mainImage.getChild(desiredPartition));
    }

    // Make sure we actually have a filesystem here.
    String alias("fs");
    if (!VFS::instance().mount(pDisk, alias))
    {
        std::cerr << "This partition does not appear to be an ext2 filesystem." << std::endl;
        return 1;
    }

    // Handle the command list.
    size_t nth = 0;
    for (auto it = cmdlist.begin(); it != cmdlist.end(); ++it, ++nth)
    {
        switch (it->what)
        {
            case WriteFile:
                if ((!writeFile(it->params[0], it->params[1])) && !ignoreErrors)
                {
                    return 1;
                }
                break;
            case CreateSymlink:
                if ((!createSymlink(it->params[0], it->params[1])) && !ignoreErrors)
                {
                    return 1;
                }
                break;
            case CreateDirectory:
                if ((!createDirectory(it->params[0])) && !ignoreErrors)
                {
                    return 1;
                }
                break;
            default:
                std::cerr << "Unknown command in command list." << std::endl;
                break;
        }

        if ((nth % 10) == 0)
        {
            double progress = nth / (double) cmdlist.size();
            std::cout << "Progress: " << std::setprecision(4) << (progress * 100.0) << "%      \r" << std::flush;
        }
    }

    std::cout << "\rProgress: 100.0%" << std::endl;

    std::cout << "Completed command list for image " << image << "." << std::endl;

    return 0;
}

bool parseCommandFile(const char *cmdFile, std::vector<Command> &output)
{
    std::ifstream f(cmdFile);
    if (f.bad() || f.fail())
    {
        std::cerr << "Command file '" << cmdFile << "' could not be read." << std::endl;
        return false;
    }

    std::string line;
    size_t lineno = 0;
    while (std::getline(f, line))
    {
        ++lineno;

        if (line.empty())
        {
            continue;
        }

        if (line[0] == '#')
        {
            // Comment line.
            continue;
        }

        Command c;
        std::istringstream s(line);

        std::string cmd;
        s >> cmd;

        std::string next;
        while (s >> next)
        {
            if (next.empty())
            {
                continue;
            }

            c.params.push_back(next);
        }

        size_t requiredParamCount = 0;

        bool ok = true;

        if (cmd == "write")
        {
            c.what = WriteFile;
            requiredParamCount = 2;
        }
        else if (cmd == "symlink")
        {
            c.what = CreateSymlink;
            requiredParamCount = 2;
        }
        else if (cmd == "mkdir")
        {
            c.what = CreateDirectory;
            requiredParamCount = 1;
        }
        else
        {
            std::cerr << "Unknown command '" << cmd << "' at line " << lineno << ": '" << line << "'" << std::endl;
            ok = false;
        }

        if (c.params.size() < requiredParamCount)
        {
            std::cerr << "Not enough parameters for '" << cmd << "' at line " << lineno << ": '" << line << "'" << std::endl;
            ok = false;
        }

        if (!ok)
        {
            if (!ignoreErrors)
                return false;

            continue;
        }

        output.push_back(c);
    }

    return true;
}

int main(int argc, char *argv[])
{
    const char *cmdFile = 0;
    const char *diskImage = 0;
    size_t partitionNumber = 0;

    // Load options.
    int c;
    while ((c = getopt(argc, argv, "if:c:p::b:")) != -1)
    {
        switch (c)
        {
            case 'c':
                // cmdfile
                cmdFile = optarg;
                break;
            case 'f':
                // disk image
                diskImage = optarg;
                break;
            case 'i':
                ignoreErrors = true;
                break;
            case 'p':
                // partition number
                partitionNumber = atoi(optarg);
                break;
            case 'b':
                // # of blocks per read.
                blocksPerRead = atoi(optarg);
                break;
            case '?':
                if (optopt == 'c' || optopt == 'p')
                {
                    std::cerr << "Option -" << optopt << " requires an argument." << std::endl;
                }
                else
                {
                    std::cerr << "Option -" << optopt << " is unknown." << std::endl;
                }
                break;
            default:
                return 1;
        }
    }

    if (cmdFile == 0)
    {
        std::cerr << "A command file must be specified." << std::endl;
        return 1;
    }

    if (diskImage == 0)
    {
        std::cerr << "A disk image must be specified." << std::endl;
        return 1;
    }

    // Parse!
    std::vector<Command> cmdlist;
    if (!parseCommandFile(cmdFile, cmdlist))
    {
        return 1;
    }

    // Complete tasks.
    int rc = handleImage(diskImage, cmdlist, partitionNumber);
    std::cout << std::flush;
    return rc;
}