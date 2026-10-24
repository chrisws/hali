// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <cctype>
#include <cstdio>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <vector>

#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "sysinfo.h"

//
// Format a mode_t as rwxr-xr-x style string
//
static std::string format_permissions(mode_t mode) {
  const char *chars = "rwxrwxrwx";
  std::string out(9, '-');
  int shift = 8;
  for (int i = 0; i < 9; i++) {
    if (mode & (1 << shift)) {
      out[i] = chars[i];
    }
    shift--;
  }
  // Setuid/setgid/sticky
  if (mode & S_ISUID) out[0] = (mode & S_IXUSR) ? 's' : 'S';
  if (mode & S_ISGID) out[3] = (mode & S_IXGRP) ? 's' : 'S';
  if (mode & S_ISVTX) out[6] = (mode & S_IXOTH) ? 't' : 'T';
  return out;
}

//
// Human-readable file size
//
static std::string human_size(off_t size) {
  char buf[32];
  if (size < 1024) {
    snprintf(buf, sizeof(buf), "%lld B", (long long)size);
  } else if (size < 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f KB", (double)size / 1024.0);
  } else if (size < 1024LL * 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f MB", (double)size / (1024.0 * 1024.0));
  } else {
    snprintf(buf, sizeof(buf), "%.1f GB", (double)size / (1024.0 * 1024.0 * 1024.0));
  }
  return buf;
}

//
// Count lines, words, chars in a file (capped at 10MB to avoid hangs)
//
static void count_text(const std::string &path, long &lines, long &words, long &chars) {
  lines = words = chars = 0;
  std::ifstream f(path, std::ios::binary);
  if (!f) return;

  const long MAX_BYTES = 10 * 1024 * 1024;
  long total = 0;
  bool prev_space = true;
  char c;
  while (f.get(c) && total < MAX_BYTES) {
    total++;
    chars++;
    if (c == '\n') {
      lines++;
      prev_space = true;
    } else if (std::isspace((unsigned char)c)) {
      prev_space = true;
    } else {
      if (prev_space) {
        words++;
      }
      prev_space = false;
    }
  }
  // Count final word if file doesn't end with whitespace
  if (total < MAX_BYTES && !prev_space && total > 0) {
    words++;
  }
}

std::string stat_file(const std::string &path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return "ERROR: cannot stat '" + path + "'";
  }

  std::ostringstream oss;
  oss << "path:       " << path << "\n";

  // Type
  if (S_ISREG(st.st_mode))      oss << "type:       regular file\n";
  else if (S_ISDIR(st.st_mode)) oss << "type:       directory\n";
  else if (S_ISLNK(st.st_mode)) oss << "type:       symlink\n";
  else if (S_ISFIFO(st.st_mode))oss << "type:       fifo\n";
  else if (S_ISSOCK(st.st_mode))oss << "type:       socket\n";
  else                          oss << "type:       other\n";

  oss << "size:       " << human_size(st.st_size) << " (" << st.st_size << " bytes)\n";
  oss << "permissions:" << " " << format_permissions(st.st_mode) << "\n";
  oss << "uid:gid:    " << st.st_uid << ":" << st.st_gid << "\n";

  // Text counts for regular files only
  if (S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= 10 * 1024 * 1024) {
    long lines, words, chars;
    count_text(path, lines, words, chars);
    oss << "lines:      " << lines << "\n";
    oss << "words:      " << words << "\n";
    oss << "chars:      " << chars << "\n";
  }

  return oss.str();
}

std::string sysinfo_text() {
  struct utsname uname_data;
  std::ostringstream oss;

  if (uname(&uname_data) == 0) {
    oss << "os:         " << uname_data.sysname << "\n";
    oss << "release:    " << uname_data.release << "\n";
    oss << "version:    " << uname_data.version << "\n";
    oss << "machine:    " << uname_data.machine << "\n";
    oss << "nodename:   " << uname_data.nodename << "\n";
  }

  uid_t uid = getuid();
  gid_t gid = getgid();
  oss << "uid:gid:    " << uid << ":" << gid << "\n";

  struct passwd *pw = getpwuid(uid);
  if (pw) {
    oss << "user:       " << pw->pw_name << "\n";
  }

  char hostname[256] = {};
  if (gethostname(hostname, sizeof(hostname)) == 0) {
    oss << "hostname:   " << hostname << "\n";
  }

  char cwd[4096] = {};
  if (getcwd(cwd, sizeof(cwd))) {
    oss << "cwd:        " << cwd << "\n";
  }

  return oss.str();
}

std::string hex_dump(const std::string &path, const std::string &offset_str) {
  // Parse optional offset and length
  long offset = 0;
  long length = 256 * 16;  // default: 256 lines * 16 bytes
  {
    std::istringstream iss(offset_str);
    long a, b;
    if (iss >> a) {
      offset = a;
      if (iss >> b) {
        length = b;
      }
    }
    if (offset < 0) offset = 0;
    if (length <= 0) length = 256 * 16;
    if (length > 256 * 16) length = 256 * 16;  // cap at 256 lines
  }

  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return "ERROR: cannot open '" + path + "'";
  }

  f.seekg(offset);
  if (!f) {
    return "ERROR: offset " + std::to_string(offset) + " beyond EOF";
  }

  std::vector<char> buf(length);
  f.read(buf.data(), length);
  long actual = f.gcount();

  std::ostringstream oss;
  for (long i = 0; i < actual; i += 16) {
    // Offset
    char off_buf[16];
    snprintf(off_buf, sizeof(off_buf), "%08lx: ", (unsigned long)(offset + i));
    oss << off_buf;

    // Hex
    for (long j = 0; j < 16; j++) {
      if (i + j < actual) {
        oss << std::hex << std::uppercase;
        char hbuf[3];
        snprintf(hbuf, sizeof(hbuf), "%02X", (unsigned char)buf[i + j]);
        oss << hbuf;
        oss << " ";
      } else {
        oss << "   ";
      }
      if (j == 7) oss << " ";
    }
    oss << " ";

    // ASCII
    for (long j = 0; j < 16 && (i + j) < actual; j++) {
      unsigned char c = (unsigned char)buf[i + j];
      oss << (std::isprint(c) ? c : '.');
    }
    oss << "\n";
  }

  if (actual == 0) {
    oss << "(empty)";
  }

  return oss.str();
}