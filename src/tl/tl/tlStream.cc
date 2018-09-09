
/*

  KLayout Layout Viewer
  Copyright (C) 2006-2018 Matthias Koefferlein

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/



#include <stddef.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <zlib.h>
#ifdef _WIN32 
#  include <io.h>
#endif

#include "tlStream.h"
#include "tlHttpStream.h"
#include "tlDeflate.h"
#include "tlAssert.h"
#include "tlFileUtils.h"

#include "tlException.h"
#include "tlString.h"
#include "tlUri.h"

namespace tl
{

// ---------------------------------------------------------------------------------
//  Some exception classes

class FileWriteErrorException
  : public tl::Exception
{
public:
  FileWriteErrorException (const std::string &f, int en)
    : tl::Exception (tl::to_string (tr ("Write error on file: %s (errno=%d)")), f, en)
  { }
};

class FileReadErrorException
  : public tl::Exception
{
public:
  FileReadErrorException (const std::string &f, int en)
    : tl::Exception (tl::to_string (tr ("Read error on file: %s (errno=%d)")), f, en)
  { }
};

class ZLibWriteErrorException
  : public tl::Exception
{
public:
  ZLibWriteErrorException (const std::string &f, const char *em)
    : tl::Exception (tl::to_string (tr ("Write error on file in decompression library: %s (message=%s)")), f, em)
  { }
};

class ZLibReadErrorException
  : public tl::Exception
{
public:
  ZLibReadErrorException (const std::string &f, const char *em)
    : tl::Exception (tl::to_string (tr ("Read error on file in decompression library: %s (message=%s)")), f, em)
  { }
};

class FileOpenErrorException
  : public tl::Exception
{
public:
  FileOpenErrorException (const std::string &f, int en)
    : tl::Exception (tl::to_string (tr ("Unable to open file: %s (errno=%d)")), f, en)
  { }
};

class FilePOpenErrorException
  : public tl::Exception
{
public:
  FilePOpenErrorException (const std::string &f, int en)
    : tl::Exception (tl::to_string (tr ("Unable to get input from command through pipe: %s (errno=%d)")), f, en)
  { }
};

class FilePReadErrorException
  : public tl::Exception
{
public:
  FilePReadErrorException (const std::string &f, int en)
    : tl::Exception (tl::to_string (tr ("Read error on pipe from command: %s (errno=%d)")), f, en)
  { }
};

class FilePWriteErrorException
  : public tl::Exception
{
public:
  FilePWriteErrorException (const std::string &f, int en)
    : tl::Exception (tl::to_string (tr ("Write error on pipe from command: %s (errno=%d)")), f, en)
  { }
};

// ---------------------------------------------------------------

class ZLibFilePrivate
{
public:
  ZLibFilePrivate () : zs (NULL) { }
  gzFile zs;
};

// ---------------------------------------------------------------
//  InputStream implementation

InputStream::InputStream (InputStreamBase &delegate)
  : m_pos (0), mp_bptr (0), mp_delegate (&delegate), m_owns_delegate (false), mp_inflate (0)
{ 
  m_bcap = 4096; // initial buffer capacity
  m_blen = 0;
  mp_buffer = new char [m_bcap];
}

InputStream::InputStream (InputStreamBase *delegate)
  : m_pos (0), mp_bptr (0), mp_delegate (delegate), m_owns_delegate (true), mp_inflate (0)
{
  m_bcap = 4096; // initial buffer capacity
  m_blen = 0;
  mp_buffer = new char [m_bcap];
}

InputStream::InputStream (const std::string &abstract_path)
  : m_pos (0), mp_bptr (0), mp_delegate (0), m_owns_delegate (false), mp_inflate (0)
{ 
  m_bcap = 4096; // initial buffer capacity
  m_blen = 0;
  mp_buffer = new char [m_bcap];

  tl::Extractor ex (abstract_path.c_str ());
#if defined(HAVE_CURL) || defined(HAVE_QT)
  if (ex.test ("http:") || ex.test ("https:")) {
    mp_delegate = new InputHttpStream (abstract_path);
  } else
#endif
  if (ex.test ("pipe:")) {
    mp_delegate = new InputPipe (ex.get ());
  } else
  if (ex.test ("file:")) {
    tl::URI uri (abstract_path);
    mp_delegate = new InputZLibFile (uri.path ());
  } else
  {
    mp_delegate = new InputZLibFile (abstract_path);
  }

  m_owns_delegate = true;
}

std::string InputStream::absolute_path (const std::string &abstract_path)
{
  //  TODO: align this implementation with InputStream ctor

  tl::Extractor ex (abstract_path.c_str ());
  if (ex.test ("http:") || ex.test ("https:")) {
    return abstract_path;
  } else if (ex.test ("pipe:")) {
    return abstract_path;
  } else if (ex.test ("file:")) {
    tl::URI uri (abstract_path);
    return tl::absolute_path (uri.path ());
  } else {
    return tl::absolute_file_path (abstract_path);
  }
}

InputStream::~InputStream ()
{
  if (mp_delegate && m_owns_delegate) {
    delete mp_delegate;
    mp_delegate = 0;
  }
  if (mp_inflate) {
    delete mp_inflate;
    mp_inflate = 0;
  } 
  if (mp_buffer) {
    delete[] mp_buffer;
    mp_buffer = 0;
  }
}

const char * 
InputStream::get (size_t n, bool bypass_inflate)
{
  //  if deflating, employ the deflate filter to get the data
  if (mp_inflate && ! bypass_inflate) {
    if (! mp_inflate->at_end ()) {

      const char *r = mp_inflate->get (n);
      tl_assert (r != 0);  //  since deflate did not report at_end()
      return r;

    } else {
      delete mp_inflate;
      mp_inflate = 0;
    }
  } 

  if (m_blen < n) {

    //  to keep move activity low, allocate twice as much as required
    if (m_bcap < n * 2) {

      while (m_bcap < n) {
        m_bcap *= 2;
      }

      char *buffer = new char [m_bcap];
      if (m_blen > 0) {
        memcpy (buffer, mp_bptr, m_blen);
      }
      delete [] mp_buffer;
      mp_buffer = buffer;

    } else if (m_blen > 0) {
      memmove (mp_buffer, mp_bptr, m_blen);
    }

    m_blen += mp_delegate->read (mp_buffer + m_blen, m_bcap - m_blen); 
    mp_bptr = mp_buffer;

  }

  if (m_blen >= n) {
    const char *r = mp_bptr;
    mp_bptr += n;
    m_blen -= n;
    m_pos += n;
    return r;
  } else {
    return 0;
  }
}

void
InputStream::unget (size_t n)
{
  if (mp_inflate) {
    mp_inflate->unget (n);
  } else {
    mp_bptr -= n;
    m_blen += n;
    m_pos -= n;
  }
}

std::string
InputStream::read_all (size_t max_count) 
{
  std::string str;
  while (max_count > 0) {
    size_t n = std::min (max_count, std::max (size_t (1), m_blen));
    const char *b = get (n);
    if (b) {
      str += std::string (b, n);
      max_count -= n;
    } else {
      break;
    }
  } 
  return str;
}

std::string
InputStream::read_all () 
{
  std::string str;
  while (true) {
    size_t n = std::max (size_t (1), m_blen);
    const char *b = get (n);
    if (b) {
      str += std::string (b, n);
    } else {
      break;
    }
  } 
  return str;
}

void InputStream::copy_to(tl::OutputStream &os)
{
  const size_t chunk = 65536;
  char b [chunk];
  size_t read;
  while ((read = mp_delegate->read (b, sizeof (b))) > 0) {
    os.put (b, read);
  }
}

void
InputStream::inflate ()
{
  tl_assert (mp_inflate == 0);
  mp_inflate = new tl::InflateFilter (*this);
}

void
InputStream::close ()
{
  if (mp_delegate) {
    mp_delegate->close ();
  }
}

void 
InputStream::reset ()
{
  //  stop inflate
  if (mp_inflate) {
    delete mp_inflate;
    mp_inflate = 0;
  } 

  //  optimize for a reset in the first m_bcap bytes
  //  -> this reduces the reset calls on mp_delegate which may not support this
  if (m_pos < m_bcap) {

    m_blen += m_pos;
    mp_bptr = mp_buffer;
    m_pos = 0;

  } else {

    mp_delegate->reset ();
    m_pos = 0;

    if (mp_buffer) {
      delete[] mp_buffer;
      mp_buffer = 0;
    }

    mp_bptr = 0;
    m_blen = 0;
    mp_buffer = new char [m_bcap];

  }
}

// ---------------------------------------------------------------
//  TextInputStream implementation

TextInputStream::TextInputStream (InputStream &stream)
  : m_line (1), m_next_line (1), m_at_end (false), m_stream (stream)
{ 
  if (m_stream.get (1) == 0) {
    m_at_end = true;
  } else {
    m_stream.unget (1);
  }
}

const std::string &
TextInputStream::get_line ()
{
  m_line = m_next_line;
  m_line_buffer.clear ();

  while (! at_end ()) {
    char c = get_char ();
    if (c == '\r') {
      //  simply skip CR 
    } else if (c == '\n' || c == 0) {
      break;
    } else {
      m_line_buffer += c;
    }
  }

  return m_line_buffer;
}

char 
TextInputStream::get_char ()
{
  m_line = m_next_line;
  const char *c = m_stream.get (1);
  if (c == 0) {
    m_at_end = true;
    return 0;
  } else {
    if (*c == '\n') {
      ++m_next_line;
    }
    return *c;
  }
}

char 
TextInputStream::peek_char ()
{
  m_line = m_next_line;
  const char *c = m_stream.get (1);
  if (c == 0) {
    m_at_end = true;
    return 0;
  } else {
    char cc = *c;
    m_stream.unget (1);
    return cc;
  }
}

char 
TextInputStream::skip ()
{
  char c = 0;
  while (! at_end () && isspace (c = peek_char ())) {
    get_char ();
  }
  return at_end () ? 0 : c;
}

void
TextInputStream::reset ()
{
  m_stream.reset ();

  m_line = 1;
  m_next_line = 1;

  if (m_stream.get (1) == 0) {
    m_at_end = true;
  } else {
    m_at_end = false;
    m_stream.unget (1);
  }
}

// ---------------------------------------------------------------
//  InputFile implementation

InputFile::InputFile (const std::string &path)
  : m_fd (-1)
{
  m_source = path;
#if defined(_WIN32)
  int fd = _wopen (tl::to_wstring (path).c_str (), _O_BINARY | _O_RDONLY | _O_SEQUENTIAL);
  if (fd < 0) {
    throw FileOpenErrorException (m_source, errno);
  }
  m_fd = fd;
#else
  int fd = open (path.c_str (), O_RDONLY);
  if (fd < 0) {
    throw FileOpenErrorException (m_source, errno);
  }
  m_fd = fd;
#endif
}

InputFile::~InputFile ()
{
  close ();
}

void
InputFile::close ()
{
  if (m_fd >= 0) {
#if defined(_WIN32)
    _close (m_fd);
#else
    ::close (m_fd);
#endif
    m_fd = -1;
  }  
}

size_t 
InputFile::read (char *b, size_t n)
{
  tl_assert (m_fd >= 0);
#if defined(_WIN32)
  ptrdiff_t ret = _read (m_fd, b, (unsigned int) n);
#else
  ptrdiff_t ret = ::read (m_fd, b, (unsigned int) n);
#endif
  if (ret < 0) {
    throw FileReadErrorException (m_source, errno);
  }
  return size_t (ret);
}

void 
InputFile::reset ()
{
  if (m_fd >= 0) {
#if defined(_WIN64)
    _lseeki64 (m_fd, 0, SEEK_SET);
#elif defined(_WIN64)
    _lseek (m_fd, 0, SEEK_SET);
#else
    lseek (m_fd, 0, SEEK_SET);
#endif
  }
}

std::string
InputFile::absolute_path () const
{
  return tl::absolute_file_path (m_source);
}

std::string
InputFile::filename () const
{
  return tl::filename (m_source);
}

// ---------------------------------------------------------------
//  InputZLibFile implementation

InputZLibFile::InputZLibFile (const std::string &path)
  : mp_d (new ZLibFilePrivate ())
{
  m_source = path;
#if defined(_WIN32)
  int fd = _wopen (tl::to_wstring (path).c_str (), _O_BINARY | _O_RDONLY | _O_SEQUENTIAL);
  if (fd < 0) {
    throw FileOpenErrorException (m_source, errno);
  }
  mp_d->zs = gzdopen (fd, "rb");
#else
  mp_d->zs = gzopen (tl::string_to_system (path).c_str (), "rb");
#endif
  if (mp_d->zs == NULL) {
    throw FileOpenErrorException (m_source, errno);
  }
}

InputZLibFile::~InputZLibFile ()
{
  close ();
  delete mp_d;
  mp_d = 0;
}

void
InputZLibFile::close ()
{
  if (mp_d->zs != NULL) {
    gzclose (mp_d->zs);
    mp_d->zs = NULL;
  }  
}

size_t 
InputZLibFile::read (char *b, size_t n)
{
  tl_assert (mp_d->zs != NULL);
  int ret = gzread (mp_d->zs, b, (unsigned int) n);
  if (ret < 0) {
    int gz_err = 0;
    const char *em = gzerror (mp_d->zs, &gz_err);
    if (gz_err == Z_ERRNO) {
      throw FileReadErrorException (m_source, errno);
    } else {
      throw ZLibReadErrorException (m_source, em);
    }
  }

  return ret;
}

void 
InputZLibFile::reset ()
{
  if (mp_d->zs != NULL) {
    gzrewind (mp_d->zs);
  }
}

std::string
InputZLibFile::absolute_path () const
{
  return tl::absolute_file_path (m_source);
}

std::string
InputZLibFile::filename () const
{
  return tl::filename (m_source);
}

// ---------------------------------------------------------------
//  OutputStream implementation

OutputStream::OutputStream (OutputStreamBase &delegate)
  : m_pos (0), mp_delegate (&delegate), m_owns_delegate (false)
{ 
  m_buffer_capacity = 16384;
  m_buffer_pos = 0;
  mp_buffer = new char[m_buffer_capacity];
}

OutputStream::OutputStreamMode 
OutputStream::output_mode_from_filename (const std::string &abstract_path, OutputStream::OutputStreamMode om)
{
  if (om == OM_Auto) {
    if (tl::match_filename_to_format (abstract_path, "(*.gz *.gzip *.GZ *.GZIP)")) {
      om = OM_Zlib;
    } else {
      om = OM_Plain;
    }
  }

  return om;
}

static
OutputStreamBase *create_file_stream (const std::string &path, OutputStream::OutputStreamMode om)
{
  if (om == OutputStream::OM_Zlib) {
    return new OutputZLibFile (path);
  } else {
    return new OutputFile (path);
  }
}

OutputStream::OutputStream (const std::string &abstract_path, OutputStreamMode om)
  : m_pos (0), mp_delegate (0), m_owns_delegate (false)
{
  //  Determine output mode
  om = output_mode_from_filename (abstract_path, om);

  tl::Extractor ex (abstract_path.c_str ());
  if (ex.test ("http:") || ex.test ("https:")) {
    throw tl::Exception (tl::to_string (tr ("Cannot write to http:, https: or pipe: URL's")));
  } else if (ex.test ("pipe:")) {
    mp_delegate = new OutputPipe (ex.get ());
  } else if (ex.test ("file:")) {
    mp_delegate = create_file_stream (ex.get (), om);
  } else {
    mp_delegate = create_file_stream (abstract_path, om);
  }

  m_owns_delegate = true;

  m_buffer_capacity = 16384;
  m_buffer_pos = 0;
  mp_buffer = new char[m_buffer_capacity];
}

OutputStream::~OutputStream ()
{
  flush ();

  if (mp_delegate && m_owns_delegate) {
    delete mp_delegate;
    mp_delegate = 0;
  }
  if (mp_buffer) {
    delete[] mp_buffer;
    mp_buffer = 0;
  }
}

inline void fast_copy (char *t, const char *s, size_t n)
{
  if (n >= sizeof (unsigned long)) {

    unsigned long *tl = reinterpret_cast<unsigned long *> (t);
    const unsigned long *sl = reinterpret_cast<const unsigned long *> (s);

    while (n >= sizeof (unsigned long)) {
      *tl++ = *sl++;
      n -= sizeof (unsigned long);
    }

    t = reinterpret_cast<char *> (tl);
    s = reinterpret_cast<const char *> (sl);

  }

  while (n-- > 0) {
    *t++ = *s++;
  }
}

void
OutputStream::flush ()
{
  if (m_buffer_pos > 0) {
    mp_delegate->write (mp_buffer, m_buffer_pos);
    m_buffer_pos = 0;
  }
}

void
OutputStream::put (const char *b, size_t n)
{
  m_pos += n;

  while (m_buffer_pos + n > m_buffer_capacity) {

    size_t nw = m_buffer_capacity - m_buffer_pos;
    if (nw) {
      n -= nw;
      fast_copy (mp_buffer + m_buffer_pos, b, nw);
      b += nw;
    }

    mp_delegate->write (mp_buffer, m_buffer_capacity);
    m_buffer_pos = 0;

  }

  if (n) {
    fast_copy (mp_buffer + m_buffer_pos, b, n);
    m_buffer_pos += n;
  }
} 

void
OutputStream::seek (size_t pos)
{
  flush ();

  mp_delegate->seek (pos);
  m_pos = pos;
}

// ---------------------------------------------------------------
//  OutputFile implementation

OutputFile::OutputFile (const std::string &path)
  : m_fd (-1)
{
  m_source = path;
#if defined(_WIN32)
  int fd = _wopen (tl::to_wstring (path).c_str (), _O_CREAT | _O_TRUNC | _O_BINARY | _O_WRONLY | _O_SEQUENTIAL, _S_IREAD | _S_IWRITE );
  if (fd < 0) {
    throw FileOpenErrorException (m_source, errno);
  }
  m_fd = fd;
#else
  int fd = open (path.c_str (), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) {
    throw FileOpenErrorException (m_source, errno);
  }
  m_fd = fd;
#endif
}

OutputFile::~OutputFile ()
{
  if (m_fd >= 0) {
#if defined(_WIN32)
    _close (m_fd);
#else
    close (m_fd);
#endif
    m_fd = -1;
  }  
}

void 
OutputFile::seek (size_t s)
{
  tl_assert (m_fd >= 0);
#if defined(_WIN64)
  _lseeki64 (m_fd, s, SEEK_SET);
#elif defined(_WIN32)
  _lseek (m_fd, s, SEEK_SET);
#else
  lseek (m_fd, s, SEEK_SET);
#endif
}

void 
OutputFile::write (const char *b, size_t n)
{
  tl_assert (m_fd >= 0);
#if defined(_WIN32)
  ptrdiff_t ret = _write (m_fd, b, (unsigned int) n);
#else
  ptrdiff_t ret = ::write (m_fd, b, (unsigned int) n);
#endif
  if (ret < 0) {
    throw FileWriteErrorException (m_source, errno);
  }
}

// ---------------------------------------------------------------
//  OutputZLibFile implementation

OutputZLibFile::OutputZLibFile (const std::string &path)
  : mp_d (new ZLibFilePrivate ())
{
  m_source = path;
#if defined(_WIN32)
  FILE *file = _wfopen (tl::to_wstring (path).c_str (), L"wb");
  if (file == NULL) {
    throw FileOpenErrorException (m_source, errno);
  }
  mp_d->zs = gzdopen (_fileno (file), "wb");
#else
  mp_d->zs = gzopen (tl::string_to_system (path).c_str (), "wb");
#endif
  if (mp_d->zs == NULL) {
    throw FileOpenErrorException (m_source, errno);
  }
}

OutputZLibFile::~OutputZLibFile ()
{
  if (mp_d->zs != NULL) {
    gzclose (mp_d->zs);
    mp_d->zs = NULL;
  }  
  delete mp_d;
  mp_d = 0;
}

void 
OutputZLibFile::write (const char *b, size_t n)
{
  tl_assert (mp_d->zs != NULL);
  int ret = gzwrite (mp_d->zs, (char *) b, (unsigned int) n);
  if (ret < 0) {
    int gz_err = 0;
    const char *em = gzerror (mp_d->zs, &gz_err);
    if (gz_err == Z_ERRNO) {
      throw FileWriteErrorException (m_source, errno);
    } else {
      throw ZLibWriteErrorException (m_source, em);
    }
  }
}

#if defined(_WIN32)

// ---------------------------------------------------------------
//  InputPipe delegate implementation

InputPipe::InputPipe (const std::string &path)
  : m_file (NULL)
{
  std::wstring wpath = tl::to_wstring (path);
  m_source = path;
  m_file = _wpopen (wpath.c_str (), L"r");
  if (m_file == NULL) {
    throw FilePOpenErrorException (m_source, errno);
  }
}

InputPipe::~InputPipe ()
{
  close ();
}

void
InputPipe::close ()
{
  if (m_file != NULL) {
    fclose (m_file);
    m_file = NULL;
  }
}

size_t
InputPipe::read (char *b, size_t n)
{
  tl_assert (m_file != NULL);
  size_t ret = fread (b, 1, n, m_file);
  if (ret < n) {
    if (ferror (m_file)) {
      throw FilePReadErrorException (m_source, ferror (m_file));
    }
  }

  return ret;
}

void
InputPipe::reset ()
{
  throw tl::Exception (tl::to_string (tr ("'reset' is not supported on pipeline input files")));
}

// ---------------------------------------------------------------
//  OutputPipe delegate implementation

OutputPipe::OutputPipe (const std::string &path)
  : m_file (NULL)
{
  std::wstring wpath = tl::to_wstring (path);
  m_source = path;
  m_file = _wpopen (wpath.c_str (), L"w");
  if (m_file == NULL) {
    throw FilePOpenErrorException (m_source, errno);
  }
}

OutputPipe::~OutputPipe ()
{
  if (m_file != NULL) {
    fclose (m_file);
    m_file = NULL;
  }
}

void
OutputPipe::write (const char *b, size_t n)
{
  tl_assert (m_file != NULL);
  size_t ret = fwrite (b, 1, n, m_file);
  if (ret < n) {
    if (ferror (m_file)) {
      throw FilePWriteErrorException (m_source, ferror (m_file));
    }
  }
}

#else

// ---------------------------------------------------------------
//  InputPipe delegate implementation

InputPipe::InputPipe (const std::string &path)
  : m_file (NULL)
{
  m_source = path;
  m_file = popen (tl::string_to_system (path).c_str (), "r");
  if (m_file == NULL) {
    throw FilePOpenErrorException (m_source, errno);
  }
}

InputPipe::~InputPipe ()
{
  close ();
}

void
InputPipe::close ()
{
  if (m_file != NULL) {
    fclose (m_file);
    m_file = NULL;
  }
}

size_t 
InputPipe::read (char *b, size_t n)
{
  tl_assert (m_file != NULL);
  size_t ret = fread (b, 1, n, m_file);
  if (ret < n) {
    if (ferror (m_file)) {
      throw FilePReadErrorException (m_source, ferror (m_file));
    }
  }

  return ret;
}

void 
InputPipe::reset ()
{
  throw tl::Exception (tl::to_string (tr ("'reset' is not supported on pipeline input files")));
}

// ---------------------------------------------------------------
//  OutputPipe delegate implementation

OutputPipe::OutputPipe (const std::string &path)
  : m_file (NULL)
{
  m_source = path;
  m_file = popen (tl::string_to_system (path).c_str (), "w");
  if (m_file == NULL) {
    throw FilePOpenErrorException (m_source, errno);
  }
}

OutputPipe::~OutputPipe ()
{
  if (m_file != NULL) {
    fclose (m_file);
    m_file = NULL;
  }  
}

void 
OutputPipe::write (const char *b, size_t n)
{
  tl_assert (m_file != NULL);
  size_t ret = fwrite (b, 1, n, m_file);
  if (ret < n) {
    if (ferror (m_file)) {
      throw FilePWriteErrorException (m_source, ferror (m_file));
    }
  }
}

#endif

// ---------------------------------------------------------------
//  match_filename_to_format implementation

bool 
match_filename_to_format (const std::string &fn, const std::string &fmt)
{
  const char *fp = fmt.c_str ();
  while (*fp && *fp != '(') {
    ++fp;
  }
  while (*fp && *fp != ')') {
    if (*++fp == '*') {
      ++fp;
    }
    const char *fpp = fp;
    while (*fpp && *fpp != ' ' && *fpp != ')') {
      ++fpp;
    }
    if (fn.size () > (unsigned int) (fpp - fp) && strncmp (fn.c_str () + fn.size () - (fpp - fp), fp, fpp - fp) == 0) {
      return true;
    }
    fp = fpp;
    while (*fp == ' ') {
      ++fp;
    }
  }
  return false;
}

}

