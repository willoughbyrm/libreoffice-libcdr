/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* libcdr
 * Version: MPL 1.1 / GPLv2+ / LGPLv2+
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License or as specified alternatively below. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * Major Contributor(s):
 * Copyright (C) 2012 Fridrich Strba <fridrich.strba@bluewin.ch>
 * Copyright (C) 2011 Eilidh McAdam <tibbylickle@gmail.com>
 *
 *
 * All Rights Reserved.
 *
 * For minor contributions see the git repository.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPLv2+"), or
 * the GNU Lesser General Public License Version 2 or later (the "LGPLv2+"),
 * in which case the provisions of the GPLv2+ or the LGPLv2+ are applicable
 * instead of those above.
 */


#include <string.h>
#include <zlib.h>
#include <map>
#include "CDRZipStream.h"
#include "CDRInternalStream.h"
#include "libcdr_utils.h"

namespace
{

struct LocalFileHeader
{
  unsigned short general_flag;
  unsigned short compression;
  unsigned crc32;
  unsigned compressed_size;
  unsigned uncompressed_size;
  std::string filename;
  LocalFileHeader()
    : general_flag(0), compression(0), crc32(0), compressed_size(0), uncompressed_size(0), filename() {}
  ~LocalFileHeader() {}
};

struct CentralDirectoryEntry
{
  unsigned short general_flag;
  unsigned short compression;
  unsigned crc32;
  unsigned compressed_size;
  unsigned uncompressed_size;
  unsigned offset;
  std::string filename;
  CentralDirectoryEntry()
    : general_flag(0), compression(0), crc32(0), compressed_size(0), uncompressed_size(0), offset(0), filename() {}
  ~CentralDirectoryEntry() {}
};

struct CentralDirectoryEnd
{
  unsigned cdir_size;
  unsigned cdir_offset;
  CentralDirectoryEnd()
    : cdir_size(0), cdir_offset(0) {}
  ~CentralDirectoryEnd() {}
};

} // anonymous namespace

namespace libcdr
{

struct CDRZipStreamImpl
{
  WPXInputStream *m_input;
  unsigned m_cdir_offset;
  std::map<std::string, CentralDirectoryEntry> m_cdir;
  bool m_initialized;
  CDRZipStreamImpl(WPXInputStream *input)
    : m_input(input), m_cdir_offset(0), m_cdir(), m_initialized(false) {}
  ~CDRZipStreamImpl() {}

  bool isZipStream();
  WPXInputStream *getSubstream(const char *name);
private:
  CDRZipStreamImpl(const CDRZipStreamImpl &);
  CDRZipStreamImpl &operator=(const CDRZipStreamImpl &);

  bool findCentralDirectoryEnd();
  bool readCentralDirectoryEnd(CentralDirectoryEnd &end);
  bool readCentralDirectory(const CentralDirectoryEnd &end);
  bool readLocalFileHeader(LocalFileHeader &header);
  bool areHeadersConsistent(const LocalFileHeader &header, const CentralDirectoryEntry &entry);
};

} // namespace libcdr


using namespace libcdr;


libcdr::CDRZipStream::CDRZipStream(WPXInputStream *input) :
  WPXInputStream(),
  m_pImpl(new CDRZipStreamImpl(input))
{
}

libcdr::CDRZipStream::~CDRZipStream()
{
  if (m_pImpl)
    delete m_pImpl;
}

const unsigned char *libcdr::CDRZipStream::read(unsigned long numBytes, unsigned long &numBytesRead)
{
  return m_pImpl->m_input->read(numBytes, numBytesRead);
}

int libcdr::CDRZipStream::seek(long offset, WPX_SEEK_TYPE seekType)
{
  return m_pImpl->m_input->seek(offset, seekType);
}

long libcdr::CDRZipStream::tell()
{
  return m_pImpl->m_input->tell();
}

bool libcdr::CDRZipStream::atEOS()
{
  return m_pImpl->m_input->atEOS();
}

bool libcdr::CDRZipStream::isOLEStream()
{
  return m_pImpl->isZipStream();
}

WPXInputStream *libcdr::CDRZipStream::getDocumentOLEStream(const char *name)
{
  if (!m_pImpl->isZipStream())
    return 0;
  return m_pImpl->getSubstream(name);
}

#define CDIR_ENTRY_SIG 0x02014b50
#define LOC_FILE_HEADER_SIG 0x04034b50
#define CDIR_END_SIG 0x06054b50

bool libcdr::CDRZipStreamImpl::findCentralDirectoryEnd()
{
  m_input->seek(m_cdir_offset, WPX_SEEK_SET);
  try
  {
    while (!m_input->atEOS())
    {
      unsigned signature = readU32(m_input);
      if (signature == CDIR_END_SIG)
      {
        m_input->seek(-4, WPX_SEEK_CUR);
        m_cdir_offset = m_input->tell();
        return true;
      }
      else
        m_input->seek(-3, WPX_SEEK_CUR);
    }
  }
  catch (...)
  {
    return false;
  }
  return false;
}

bool libcdr::CDRZipStreamImpl::isZipStream()
{
  if (m_cdir_offset)
  {
    if(m_cdir.empty())
      return false;
    return true;
  }
  if (m_initialized)
    return false;
  m_initialized = true;
  if (!findCentralDirectoryEnd())
    return false;
  CentralDirectoryEnd end;
  if (!readCentralDirectoryEnd(end))
    return false;
  if (!readCentralDirectory(end))
    return false;
  CentralDirectoryEntry entry = m_cdir.begin()->second;
  m_input->seek(entry.offset, WPX_SEEK_SET);
  LocalFileHeader header;
  if (!readLocalFileHeader(header))
    return false;
  if (!areHeadersConsistent(header, entry))
    return false;
  return true;
}

bool libcdr::CDRZipStreamImpl::readCentralDirectory(const CentralDirectoryEnd &end)
{
  try
  {
    m_input->seek(end.cdir_offset, WPX_SEEK_SET);
    while (!m_input->atEOS())
    {
      unsigned signature = readU32(m_input);
      if (signature != CDIR_ENTRY_SIG)
      {
        if (m_cdir.empty())
          return false;
        else
        {
          for (std::map<std::string, CentralDirectoryEntry>::const_iterator iter = m_cdir.begin(); iter != m_cdir.end(); ++iter)
            printf("Fridrich 2 %s\n", iter->first.c_str());

          return true;
        }
      }

      CentralDirectoryEntry entry;
      m_input->seek(4, WPX_SEEK_CUR);
      entry.general_flag = readU16(m_input);
      entry.compression = readU16(m_input);
      m_input->seek(4, WPX_SEEK_CUR);
      entry.crc32 = readU32(m_input);
      entry.compressed_size = readU32(m_input);
      entry.uncompressed_size = readU32(m_input);
      unsigned short filename_size = readU16(m_input);
      unsigned short extra_field_size = readU16(m_input);
      unsigned short file_comment_size = readU16(m_input);
      m_input->seek(8, WPX_SEEK_CUR);
      entry.offset = readU32(m_input);
      entry.filename.clear();
      entry.filename.reserve(filename_size);
      unsigned long bytesRead = 0;
      const unsigned char *buffer = m_input->read(filename_size, bytesRead);
      entry.filename.assign((const char *)buffer, bytesRead);
      printf("Fridrich %s\n", entry.filename.c_str());
      m_input->seek(extra_field_size+file_comment_size, WPX_SEEK_CUR);

      m_cdir[entry.filename] = entry;
    }
  }
  catch (...)
  {
    return false;
  }
  return true;
}

WPXInputStream *libcdr::CDRZipStreamImpl::getSubstream(const char *name)
{
  if (m_cdir.empty())
    return 0;
  std::map<std::string, CentralDirectoryEntry>::const_iterator iter = m_cdir.lower_bound(name);
  if (iter == m_cdir.end())
    return 0;
  if (m_cdir.key_comp()(name, iter->first))
  {
    size_t name_length = strlen(name);
    if (iter->first.compare(0, name_length, name))
      return 0;
  }
  CentralDirectoryEntry entry = iter->second;
  m_input->seek(entry.offset, WPX_SEEK_SET);
  LocalFileHeader header;
  if (!readLocalFileHeader(header))
    return 0;
  if (!areHeadersConsistent(header, entry))
    return 0;
  if (!entry.compression)
    return new CDRInternalStream(m_input, entry.compressed_size);
  else
  {
    int ret;
    z_stream strm;

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit2(&strm,-MAX_WBITS);
    if (ret != Z_OK)
      return 0;

    unsigned long numBytesRead = 0;
    const unsigned char *compressedData = m_input->read(entry.compressed_size, numBytesRead);
    if (numBytesRead != entry.compressed_size)
      return 0;

    strm.avail_in = numBytesRead;
    strm.next_in = (Bytef *)compressedData;

    std::vector<unsigned char>data(entry.uncompressed_size);

    strm.avail_out = entry.uncompressed_size;
    strm.next_out = reinterpret_cast<Bytef *>(&data[0]);
    ret = inflate(&strm, Z_FINISH);
    switch (ret)
    {
    case Z_NEED_DICT:
    case Z_DATA_ERROR:
    case Z_MEM_ERROR:
      (void)inflateEnd(&strm);
      data.clear();
      return 0;
    }
    (void)inflateEnd(&strm);
    return new CDRInternalStream(data);
  }
}

bool libcdr::CDRZipStreamImpl::readCentralDirectoryEnd(CentralDirectoryEnd &end)
{
  try
  {
    unsigned signature = readU32(m_input);
    if (signature != CDIR_END_SIG)
      return false;

    m_input->seek(8, WPX_SEEK_CUR);
    end.cdir_size = readU32(m_input);
    end.cdir_offset = readU32(m_input);
    unsigned short comment_size = readU16(m_input);
    m_input->seek(comment_size, WPX_SEEK_CUR);
  }
  catch (...)
  {
    return false;
  }
  return true;
}

bool libcdr::CDRZipStreamImpl::readLocalFileHeader(LocalFileHeader &header)
{
  try
  {
    unsigned signature = readU32(m_input);
    if (signature != LOC_FILE_HEADER_SIG)
      return false;

    m_input->seek(2, WPX_SEEK_CUR);
    header.general_flag = readU16(m_input);
    header.compression = readU16(m_input);
    m_input->seek(4, WPX_SEEK_CUR);
    header.crc32 = readU32(m_input);
    header.compressed_size = readU32(m_input);
    header.uncompressed_size = readU32(m_input);
    unsigned short filename_size = readU16(m_input);
    unsigned short extra_field_size = readU16(m_input);
    header.filename.clear();
    header.filename.reserve(filename_size);
    unsigned long bytesRead = 0;
    const unsigned char *buffer = m_input->read(filename_size, bytesRead);
    header.filename.assign((const char *)buffer, bytesRead);
    m_input->seek(extra_field_size, WPX_SEEK_CUR);
  }
  catch (...)
  {
    return false;
  }
  return true;
}

bool libcdr::CDRZipStreamImpl::areHeadersConsistent(const LocalFileHeader &header, const CentralDirectoryEntry &entry)
{
  if (header.general_flag != entry.general_flag)
    return false;
  if (header.compression != entry.compression)
    return false;
  if (!(header.general_flag & 0x08))
  {
    if (header.crc32 != entry.crc32)
      return false;
    if (header.compressed_size != entry.compressed_size)
      return false;
    if (header.uncompressed_size != entry.uncompressed_size)
      return false;
  }
  return true;
}


/* vim:set shiftwidth=2 softtabstop=2 expandtab: */
