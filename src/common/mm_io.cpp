/*
  mkvmerge -- utility for splicing together matroska files
      from component media subtypes

  mm_io_c.cpp

  Written by Moritz Bunkus <moritz@bunkus.org>

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

/*!
    \file
    \version $Id$
    \brief IO callback class implementation
    \author Moritz Bunkus <moritz@bunkus.org>
*/

#include "os.h"

#include <exception>

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(SYS_WINDOWS)
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#endif // SYS_WINDOWS

#include "mm_io.h"
#include "common.h"

using namespace std;

#if defined(SYS_UNIX)
mm_io_c::mm_io_c(const char *path, const open_mode mode) {
  char *cmode;

  switch (mode) {
    case MODE_READ:
      cmode = "rb";
      break;
    case MODE_WRITE:
      cmode = "wb";
      break;
    case MODE_CREATE:
      cmode = "wb+";
      break;
    case MODE_SAFE:
      cmode = "r+b";
      break;
    default:
      throw 0;
  }

  file = (FILE *)fopen(path, cmode);

  if (file == NULL)
    throw exception();

  file_name = safestrdup(path);
}

mm_io_c::mm_io_c() {
  file_name = NULL;
  file = NULL;
}

mm_io_c::~mm_io_c() {
  close();
  safefree(file_name);
}

uint64 mm_io_c::getFilePointer() {
  return ftello((FILE *)file);
}

void mm_io_c::setFilePointer(int64 offset, seek_mode mode) {
  int whence;

  if (mode == seek_beginning)
    whence = SEEK_SET;
  else if (mode == seek_end)
    whence = SEEK_END;
  else
    whence = SEEK_CUR;

  if (fseeko((FILE *)file, offset, whence) != 0)
    throw exception();
}

size_t mm_io_c::write(const void *buffer, size_t size) {
  size_t bwritten;

  bwritten = fwrite(buffer, 1, size, (FILE *)file);
  if (ferror((FILE *)file) != 0)
    mxerror("Cound not write to the output file: %d (%s)\n", errno,
            strerror(errno));

  return bwritten;
}

uint32 mm_io_c::read(void *buffer, size_t size) {
  return fread(buffer, 1, size, (FILE *)file);
}

void mm_io_c::close() {
  if (file != NULL)
    fclose((FILE *)file);
}

bool mm_io_c::eof() {
  return feof((FILE *)file) != 0 ? true : false;
}

#else // SYS_UNIX

mm_io_c::mm_io_c(const char *path, const open_mode mode) {
  DWORD access_mode, share_mode, disposition;

  switch (mode) {
    case MODE_READ:
      access_mode = GENERIC_READ;
      share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE;
      disposition = OPEN_EXISTING;
      break;
    case MODE_WRITE:
      access_mode = GENERIC_WRITE;
      share_mode = 0;
      disposition = OPEN_ALWAYS;
      break;
    case MODE_SAFE:
      access_mode = GENERIC_WRITE | GENERIC_READ;
      share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE;
      disposition = OPEN_ALWAYS;
      break;
    case MODE_CREATE:
      access_mode = GENERIC_WRITE;
      share_mode = 0;
      disposition = CREATE_ALWAYS;
      break;
    default:
      throw exception();
  }

  file = (void *)CreateFile(path, access_mode, share_mode, NULL, disposition,
                            0, NULL);
  _eof = false;
  if ((HANDLE)file == (HANDLE)0xFFFFFFFF)
    throw exception();

  file_name = safestrdup(path);
}

mm_io_c::mm_io_c() {
  file = NULL;
  safefree(file_name);
}

mm_io_c::~mm_io_c() {
  close();
}

void mm_io_c::close() {
  if (file != NULL)
    CloseHandle((HANDLE)file);
}

uint64 mm_io_c::getFilePointer() {
  LONG high = 0;
  DWORD low;
  
  low = SetFilePointer((HANDLE)file, 0, &high, FILE_CURRENT);
  if ((low == INVALID_SET_FILE_POINTER) && (GetLastError() != NO_ERROR))
    return (uint64)-1;

  return (((uint64)high) << 32) | (uint64)low;
}

void mm_io_c::setFilePointer(int64 offset, seek_mode mode) {
  DWORD method;
  LONG high;

  switch (mode) {
    case seek_beginning:
      method = FILE_BEGIN;
      break;
    case seek_current:
      method = FILE_CURRENT;
      break;
    case seek_end:
      method = FILE_END;
      break;
  }

  high = (LONG)(offset >> 32);
  SetFilePointer((HANDLE)file, (LONG)(offset & 0xffffffff), &high, method);
}

uint32 mm_io_c::read(void *buffer, size_t size) {
  DWORD bytes_read;

  if (!ReadFile((HANDLE)file, buffer, size, &bytes_read, NULL)) {
    _eof = true;
    return 0;
  }

  if (size != bytes_read)
    _eof = true;

  return bytes_read;
}

size_t mm_io_c::write(const void *buffer, size_t size) {
  DWORD bytes_written;

  if (!WriteFile((HANDLE)file, buffer, size, &bytes_written, NULL))
    bytes_written = 0;

  if (bytes_written != size) {
    DWORD error;
    char *error_msg;

    error = GetLastError();
    error_msg = NULL;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | 
                  FORMAT_MESSAGE_FROM_SYSTEM | 
                  FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                  error,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR)&error_msg, 0, NULL);
    mxerror("Cound not write to the output file: %d (%s)\n", error,
            error_msg != NULL ? error_msg : "unknown");
  }

  return bytes_written;
}

bool mm_io_c::eof() {
  return _eof;
}

#endif

const char *mm_io_c::get_file_name() {
  return file_name;
}

string mm_io_c::getline() {
  char c;
  string s;

  if (eof())
    throw exception();

  while (read(&c, 1) == 1) {
    if (c == '\r')
      continue;
    if (c == '\n')
      return s;
    s += c;
  }

  return s;
}

bool mm_io_c::getline2(string &s) {
  try {
    s = getline();
  } catch(...) {
    return false;
  }

  return true;
}

bool mm_io_c::setFilePointer2(int64 offset, seek_mode mode) {
  try {
    setFilePointer(offset, mode);
    return true;
  } catch(...) {
    return false;
  }
}

size_t mm_io_c::puts_unl(const char *s) {
  int i;
  size_t bytes_written;

  bytes_written = 0;
  for (i = 0; i < strlen(s); i++)
    if (s[i] != '\r')
      bytes_written += write(&s[i], 1);

  return bytes_written;
}

unsigned char mm_io_c::read_uint8() {
  unsigned char value;

  if (read(&value, 1) != 1)
    throw exception();

  return value;
}

uint16_t mm_io_c::read_uint16() {
  unsigned char buffer[2];

  if (read(buffer, 2) != 2)
    throw exception();

  return get_uint16(buffer);
}

uint32_t mm_io_c::read_uint24() {
  unsigned char buffer[3];

  if (read(buffer, 3) != 3)
    throw exception();

  return get_uint24(buffer);
}

uint32_t mm_io_c::read_uint32() {
  unsigned char buffer[4];

  if (read(buffer, 4) != 4)
    throw exception();

  return get_uint32(buffer);
}

uint64_t mm_io_c::read_uint64() {
  unsigned char buffer[8];

  if (read(buffer, 8) != 8)
    throw exception();

  return get_uint64(buffer);
}

uint16_t mm_io_c::read_uint16_be() {
  unsigned char buffer[2];

  if (read(buffer, 2) != 2)
    throw exception();

  return get_uint16_be(buffer);
}

uint32_t mm_io_c::read_uint24_be() {
  unsigned char buffer[3];

  if (read(buffer, 3) != 3)
    throw exception();

  return get_uint24_be(buffer);
}

uint32_t mm_io_c::read_uint32_be() {
  unsigned char buffer[4];

  if (read(buffer, 4) != 4)
    throw exception();

  return get_uint32_be(buffer);
}

uint64_t mm_io_c::read_uint64_be() {
  unsigned char buffer[8];

  if (read(buffer, 8) != 8)
    throw exception();

  return get_uint64_be(buffer);
}

void mm_io_c::skip(int64 num_bytes) {
  int64_t pos;

  pos = getFilePointer();
  setFilePointer(pos + num_bytes);
  if ((pos + num_bytes) != getFilePointer())
    throw exception();
}

void mm_io_c::save_pos(int64_t new_pos) {
  positions.push(getFilePointer());

  if (new_pos != -1)
    setFilePointer(new_pos);
}

bool mm_io_c::restore_pos() {
  if (positions.size() == 0)
    return false;

  setFilePointer(positions.top());
  positions.pop();

  return true;
}

bool mm_io_c::write_bom(const char *charset) {
  const unsigned char utf8_bom[3] = {0xef, 0xbb, 0xbf};
  const unsigned char utf16le_bom[2] = {0xff, 0xfe};
  const unsigned char utf16be_bom[2] = {0xfe, 0xff};
  const unsigned char utf32le_bom[4] = {0xff, 0xfe, 0x00, 0x00};
  const unsigned char utf32be_bom[4] = {0x00, 0x00, 0xff, 0xfe};
  const unsigned char *bom;
  int bom_len;

  if (charset == NULL)
    return false;

  if (!strcmp(charset, "UTF-8") || !strcmp(charset, "UTF8")) {
    bom_len = 3;
    bom = utf8_bom;
  } else if (!strcmp(charset, "UTF-16") || !strcmp(charset, "UTF-16LE") ||
             !strcmp(charset, "UTF16") || !strcmp(charset, "UTF16LE")) {
    bom_len = 2;
    bom = utf16le_bom;
  } else if (!strcmp(charset, "UTF-16BE") || !strcmp(charset, "UTF16BE")) {
    bom_len = 2;
    bom = utf16be_bom;
  } else if (!strcmp(charset, "UTF-32") || !strcmp(charset, "UTF-32LE") ||
             !strcmp(charset, "UTF32") || !strcmp(charset, "UTF32LE")) {
    bom_len = 4;
    bom = utf32le_bom;
  } else if (!strcmp(charset, "UTF-32BE") || !strcmp(charset, "UTF32BE")) {
    bom_len = 4;
    bom = utf32be_bom;
  } else
    return false;

  return (write(bom, bom_len) == bom_len);
}

int64_t mm_io_c::get_size() {
  int64_t size;

  save_pos();
  setFilePointer(0, seek_end);
  size = getFilePointer();
  restore_pos();

  return size;
}

int mm_io_c::getch() {
  unsigned char c;

  if (read(&c, 1) != 1)
    return -1;

  return c;
}

/*
 * Dummy class for output to /dev/null. Needed for two pass stuff.
 */

mm_null_io_c::mm_null_io_c() {
  pos = 0;
}

uint64 mm_null_io_c::getFilePointer() {
  return pos;
}

void mm_null_io_c::setFilePointer(int64 offset, seek_mode mode) {
  if (mode == seek_beginning)
    pos = offset;
  else if (mode == seek_end)
    pos = 0;
  else
    pos += offset;
}

uint32 mm_null_io_c::read(void *buffer, size_t size) {
  memset(buffer, 0, size);
  pos += size;

  return size;
}

size_t mm_null_io_c::write(const void *buffer, size_t size) {
  pos += size;

  return size;
}

void mm_null_io_c::close() {
}

/*
 * IO callback class working on memory
 */
mm_mem_io_c::mm_mem_io_c(unsigned char *nmem, uint64_t nsize) {
  mem = nmem;
  mem_size = nsize;
  pos = 0;
}

uint64_t mm_mem_io_c::getFilePointer() {
  return pos;
}

void mm_mem_io_c::setFilePointer(int64 offset, seek_mode mode) {
  int64_t npos;

  if ((mem == NULL) || (mem_size == 0))
    throw exception();

  if (mode == seek_beginning)
    npos = offset;
  else if (mode == seek_end)
    npos = mem_size - offset;
  else
    npos = pos + offset;

  if (npos < 0)
    pos = 0;
  else if (npos >= mem_size)
    pos = mem_size;
  else
    pos = npos;
}

uint32 mm_mem_io_c::read(void *buffer, size_t size) {
  int64_t rbytes;

  rbytes = (pos + size) >= mem_size ? mem_size - pos : size;
  memcpy(buffer, &mem[pos], rbytes);
  pos += rbytes;

  return rbytes;
}

size_t mm_mem_io_c::write(const void *buffer, size_t size) {
  int64_t wbytes;

  wbytes = (pos + size) >= mem_size ? mem_size - pos : size;
  memcpy(&mem[pos], buffer, wbytes);
  pos += wbytes;

  return wbytes;
}

void mm_mem_io_c::close() {
  mem = NULL;
  mem_size = 0;
  pos = 0;
}

bool mm_mem_io_c::eof() {
  return pos >= mem_size;
}

/*
 * Class for handling UTF-8/UTF-16/UTF-32 text files.
 */

mm_text_io_c::mm_text_io_c(const char *path): mm_io_c(path, MODE_READ) {
  unsigned char buffer[4];

  if (read(buffer, 4) != 4)
    throw exception();

  if ((buffer[0] == 0xef) && (buffer[1] == 0xbb) && (buffer[2] == 0xbf)) {
    byte_order = BO_UTF8;
    bom_len = 3;
  } else if ((buffer[0] == 0xff) && (buffer[1] == 0xfe) &&
             (buffer[2] == 0x00) && (buffer[3] == 0x00)) {
    byte_order = BO_UTF32_LE;
    bom_len = 4;
  } else if ((buffer[0] == 0x00) && (buffer[1] == 0x00) &&
             (buffer[2] == 0xfe) && (buffer[3] == 0xff)) {
    byte_order = BO_UTF32_BE;
    bom_len = 4;
  } else if ((buffer[0] == 0xff) && (buffer[1] == 0xfe)) {
    byte_order = BO_UTF16_LE;
    bom_len = 2;
  } else if ((buffer[0] == 0xfe) && (buffer[1] == 0xff)) {
    byte_order = BO_UTF16_BE;
    bom_len = 2;
  } else {
    byte_order = BO_NONE;
    bom_len = 0;
  }

  setFilePointer(0, seek_beginning);
}

// 1 byte: 0xxxxxxx,
// 2 bytes: 110xxxxx 10xxxxxx,
// 3 bytes: 1110xxxx 10xxxxxx 10xxxxxx


int mm_text_io_c::read_next_char(char *buffer) {
  unsigned char stream[4];
  unsigned long data;
  int size, i;

  if (byte_order == BO_NONE)
    return read(buffer, 1);

  size = 0;
  if (byte_order == BO_UTF8) {
    if (read(stream, 1) != 1)
      return 0;
    if ((stream[0] & 0x80) == 0)
      size = 1;
    else if ((stream[0] & 0xe0) == 0xc0)
      size = 2;
    else if ((stream[0] & 0xf0) == 0xe0)
      size = 3;
    else if ((stream[0] & 0xf8) == 0xf0)
      size = 4;
    else if ((stream[0] & 0xfc) == 0xf8)
      size = 5;
    else if ((stream[0] & 0xfe) == 0xfc)
      size = 6;
    else
      die("mm_text_io_c::read_next_char(): Invalid UTF-8 char. First byte: "
          "0x%02x", stream[0]);

    if ((size > 1) && (read(&stream[1], size - 1) != (size - 1)))
      return 0;

    memcpy(buffer, stream, size);

    return size;
  } else if ((byte_order == BO_UTF16_LE) || (byte_order == BO_UTF16_BE))
    size = 2;
  else
    size = 4;

  if (read(stream, size) != size)
    return 0;

  data = 0;
  if ((byte_order == BO_UTF16_LE) || (byte_order == BO_UTF32_LE))
    for (i = 0; i < size; i++) {
      data <<= 8;
      data |= stream[size - i - 1];
    }
  else
    for (i = 0; i < size; i++) {
      data <<= 8;
      data |= stream[i];
    }


  if (data < 0x80) {
    buffer[0] = data;
    return 1;
  }

  if (data < 0x800) {
    buffer[0] = 0xc0 | (data >> 6);
    buffer[1] = 0x80 | (data & 0x3f);
    return 2;
  }

  if (data < 0x10000) {
    buffer[0] = 0xe0 | (data >> 12);
    buffer[1] = 0x80 | ((data >> 6) & 0x3f);
    buffer[2] = 0x80 | (data & 0x3f);
    return 3;
  }

  die("mm_text_io_c: UTF32_* is not supported at the moment.");

  return 0;
}

string mm_text_io_c::getline() {
  string s;
  int len;
  char utf8char[8];

  if (eof())
    throw exception();

  while (1) {
    memset(utf8char, 0, 8);

    len = read_next_char(utf8char);
    if (len == 0)
      return s;

    if ((len == 1) && (utf8char[0] == '\r'))
      continue;

    if ((len == 1) && (utf8char[0] == '\n'))
      return s;

    s += utf8char;
  }
}

byte_order_e mm_text_io_c::get_byte_order() {
  return byte_order;
}

void mm_text_io_c::setFilePointer(int64_t offset, seek_mode mode) {
  if ((offset == 0) && (mode == seek_beginning))
    mm_io_c::setFilePointer(bom_len, seek_beginning);
  else
    mm_io_c::setFilePointer(offset, seek_beginning);
}
