#define _FILE_OFFSET_BITS 64

#include "misc.h"

#include <errno.h>
#include <iconv.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "PHP/php-engine-vars.h"


#include "interface.h"
#include "datetime.h"
#include "files.h"
#include "math_functions.h"
#include "string_functions.h"
#include "exception.h"


string f$uniqid (const string &prefix, bool more_entropy) {
  if (!more_entropy) {
    f$usleep (1);
  }

  dl::enter_critical_section();//OK

  struct timeval tv;
  gettimeofday (&tv, NULL);
  int sec = (int)tv.tv_sec;
  int usec = (int)(tv.tv_usec & 0xFFFFF);

  char buf[30];
  static_SB.clean() += prefix;

  if (more_entropy) {
    sprintf (buf, "%08x%05x%.8f", sec, usec, f$lcg_value() * 10);
    static_SB.append (buf, 23);
  } else {
    sprintf (buf, "%08x%05x", sec, usec);
    static_SB.append (buf, 13);
  }

  dl::leave_critical_section();
  return static_SB.str();
}


OrFalse <string> f$iconv (const string &input_encoding, const string &output_encoding, const string &input_str) {
  iconv_t cd;
  if ((cd = iconv_open (output_encoding.c_str(), input_encoding.c_str())) == (iconv_t)-1) {
    php_critical_error ("unsupported iconv from \"%s\" to \"%s\"", input_encoding.c_str(), output_encoding.c_str());
    return false;
  }

  for (int mul = 4; mul <= 16; mul *= 4) {
    string output_str (mul * input_str.size(), false);

    size_t input_len = input_str.size();
    size_t output_len = output_str.size();
    char *input_buf = const_cast <char *> (input_str.c_str());
    char *output_buf = output_str.buffer();

    size_t res = iconv (cd, &input_buf, &input_len, &output_buf, &output_len);
    if (res != (size_t)-1 || errno != E2BIG) {
      output_str.shrink ((dl::size_type)(output_buf - output_str.c_str()));
      iconv_close (cd);
      return output_str;
    }
/*
    if (errno != E2BIG) {
      php_warning ("Error in iconv from \"%s\" to \"%s\" string \"%s\" at character %d at pos %d: %m", input_encoding.c_str(), output_encoding.c_str(), input_str.c_str(), (int)*input_buf, (int)(input_buf - input_str.c_str()));
      break;
    }
*/
    iconv (cd, NULL, NULL, NULL, NULL);
  }

  iconv_close (cd);
  return false;
}


void f$sleep (const int &seconds) {
  if (seconds <= 0 || seconds > 1800) {
    php_warning ("Wrong parameter seconds (%d) specified in function sleep, must be in seconds", seconds);
    return;
  }

  f$usleep (seconds * 1000000);
}

void f$usleep (const int &micro_seconds) {
  int sleep_time = micro_seconds;
  if (sleep_time <= 0) {
    php_warning ("Wrong parameter micro_seconds (%d) specified in function usleep", sleep_time);
    return;
  }

  struct itimerval timer, old_timer;
  memset (&timer, 0, sizeof (timer));
  dl::enter_critical_section();//OK
  setitimer (ITIMER_REAL, &timer, &old_timer);
  dl::leave_critical_section();
  long long time_left = old_timer.it_value.tv_sec * 1000000ll + old_timer.it_value.tv_usec;

//  fprintf (stderr, "time_left = %lld, sleep_time = %d\n", time_left, sleep_time);
  if (time_left == 0) {
    dl::enter_critical_section();//OK
    usleep (sleep_time);
    dl::leave_critical_section();
    return;
  }

  if (time_left > sleep_time) {
    double start_time = microtime (true);

    dl::enter_critical_section();//OK
    usleep (sleep_time);
    dl::leave_critical_section();

    time_left -= (long long)((microtime (true) - start_time) * 1000000);
  } else {
    time_left = 1;
  }

  if (time_left <= 1) {
//    raise (SIGALRM);
//    return;
    time_left = 1;
  }

  timer.it_value.tv_sec = time_left / 1000000;
  timer.it_value.tv_usec = time_left % 1000000;
  dl::enter_critical_section();//OK
  setitimer (ITIMER_REAL, &timer, NULL);
  dl::leave_critical_section();
}


static const char php_sig_gif[3] = {'G', 'I', 'F'};
static const char php_sig_jpg[3] = {(char)0xff, (char)0xd8, (char)0xff};
static const char php_sig_png[8] = {(char)0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
static const char php_sig_jpc[4] = {(char)0xff, 0x4f, (char)0xff, 0x51};
static const char php_sig_jp2[12] = {0x00, 0x00, 0x00, 0x0c, 'j', 'P', ' ', ' ', 0x0d, 0x0a, (char)0x87, 0x0a};

static const char *mime_type_string[11] = {
  "",
  "image/gif",
  "image/jpeg",
  "image/png",
  "application/x-shockwave-flash",
  "image/psd",
  "image/x-ms-bmp",
  "image/tiff",
  "image/tiff",
  "application/octet-stream",
  "image/jp2"
};


static const int M_SOF0  = 0xC0;
static const int M_SOF1  = 0xC1;
static const int M_SOF2  = 0xC2;
static const int M_SOF3  = 0xC3;
static const int M_SOF5  = 0xC5;
static const int M_SOF6  = 0xC6;
static const int M_SOF7  = 0xC7;
static const int M_SOF9  = 0xC9;
static const int M_SOF10 = 0xCA;
static const int M_SOF11 = 0xCB;
static const int M_SOF13 = 0xCD;
static const int M_SOF14 = 0xCE;
static const int M_SOF15 = 0xCF;
static const int M_EOI   = 0xD9;
static const int M_SOS   = 0xDA;
static const int M_COM   = 0xFE;

static const int M_PSEUDO = 0xFFD8;

var f$getimagesize (const string &name) {
  dl::enter_critical_section();//OK
  struct stat stat_buf;
  int read_fd = open (name.c_str(), O_RDONLY);
  if (read_fd < 0) {
    dl::leave_critical_section();
    return false;
  }
  if (fstat (read_fd, &stat_buf) < 0) {
    close (read_fd);
    dl::leave_critical_section();
    return false;
  }

  if (!S_ISREG (stat_buf.st_mode)) {
    close (read_fd);
    dl::leave_critical_section();
    php_warning ("Regular file expected as first argument in function getimagesize, \"%s\" is given", name.c_str());
    return false;
  }

  const size_t min_size = 3 * 256 + 64;
  unsigned char buf[min_size];
  size_t size = stat_buf.st_size, read_size = min_size;
  if (size < min_size) {
    read_size = size;
  }

  if (read_size < 12 || read_safe (read_fd, buf, read_size) < (ssize_t)read_size) {
    close (read_fd);
    dl::leave_critical_section();
    return false;
  }

  int width = 0, height = 0, bits = 0, channels = 0, type = IMAGETYPE_UNKNOWN;
  switch (buf[0]) {
    case 'G': //gif
      if (!strncmp ((const char *)buf, php_sig_gif, sizeof (php_sig_gif))) {
        type     = IMAGETYPE_GIF;
        width    = buf[6] | (buf[7] << 8);
        height   = buf[8] | (buf[9] << 8);
        bits     = buf[10] & 0x80 ? (buf[10] & 0x07) + 1 : 0;
        channels = 3;
      } else {
        close (read_fd);
        dl::leave_critical_section();
        return false;
      }
      break;
    case 0xff: //jpg or jpc
      if (!strncmp ((const char *)buf, php_sig_jpg, sizeof (php_sig_jpg))) {
        type = IMAGETYPE_JPEG;

        unsigned char *image = (unsigned char *)dl::allocate ((dl::size_type)size);
        if (image == NULL) {
          php_warning ("Not enough memory to process file \"%s\" in getimagesize", name.c_str());
          close (read_fd);
          dl::leave_critical_section();
          return false;
        }
        memcpy (image, buf, read_size);
        if (read_safe (read_fd, image + read_size, size - read_size) < (ssize_t)(size - read_size)) {
          dl::deallocate (image, (dl::size_type)size);
          close (read_fd);
          dl::leave_critical_section();
          return false;
        }

        int marker = M_PSEUDO;
        size_t cur_pos = 2;

        while (height == 0 && width == 0 && marker != M_SOS && marker != M_EOI) {
          int a = 0, comment_correction = 1 + (marker == M_COM), new_marker;

          do {
            if (cur_pos == size) {
              new_marker = M_EOI;
              break;
            }
            new_marker = image[cur_pos++];
            if (marker == M_COM && comment_correction > 0) {
              if (new_marker != 0xFF) {
                new_marker = 0xFF;
                comment_correction--;
              } else {
                marker = M_PSEUDO;
              }
            }
            a++;
          } while (new_marker == 0xff);

          if (a < 2 || (marker == M_COM && comment_correction)) {
            new_marker = M_EOI;
          }

          marker = new_marker;

          switch (marker) {
            case M_SOF0:
            case M_SOF1:
            case M_SOF2:
            case M_SOF3:
            case M_SOF5:
            case M_SOF6:
            case M_SOF7:
            case M_SOF9:
            case M_SOF10:
            case M_SOF11:
            case M_SOF13:
            case M_SOF14:
            case M_SOF15:
              if (cur_pos + 8 > size) {
                dl::deallocate (image, (dl::size_type)size);
                close (read_fd);
                dl::leave_critical_section();
                return false;
              }
              bits     = image[cur_pos + 2];
              height   = (image[cur_pos + 3] << 8) + image[cur_pos + 4];
              width    = (image[cur_pos + 5] << 8) + image[cur_pos + 6];
              channels = image[cur_pos + 7];
              cur_pos += 8;

            case M_SOS:
            case M_EOI:
              break;

            default: {
              size_t length = (image[cur_pos] << 8) + image[cur_pos + 1];

              if (length < 2 || cur_pos + length > size) {
                dl::deallocate (image, (dl::size_type)size);
                close (read_fd);
                dl::leave_critical_section();
                return false;
              }
              cur_pos += length;
              break;
            }
          }
        }
        dl::deallocate (image, (dl::size_type)size);
      } else if (!strncmp ((const char *)buf, php_sig_jpc, sizeof (php_sig_jpc)) && (int)read_size >= 42) {
        type = IMAGETYPE_JPEG;

        width    = (buf[8] << 24) + (buf[9] << 16) + (buf[10] << 8) + buf[11];
        height   = (buf[12] << 24) + (buf[13] << 16) + (buf[14] << 8) + buf[15];
        channels = (buf[40] << 8) + buf[41];

        if (channels < 0 || channels > 256 || (int)read_size < 42 + 3 * channels || width <= 0 || height <= 0) {
          close (read_fd);
          dl::leave_critical_section();
          return false;
        }

        bits = 0;
        for (int i = 0; i < channels; i++) {
          int cur_bits = buf[42 + 3 * i];
          if (cur_bits > bits) {
            bits = cur_bits;
          }
        }
        bits++;
      } else {
        close (read_fd);
        dl::leave_critical_section();
        return false;
      }
      break;
    case 0x00: //jp2
      if (read_size >= 54 && !strncmp ((const char *)buf, php_sig_jp2, sizeof (php_sig_jp2))) {
        type = IMAGETYPE_JP2;

        bool found = false;

        int buf_pos = 12;
        size_t file_pos = 12;
        while ((int)read_size >= 42 + buf_pos + 8) {
          const unsigned char *s = buf + buf_pos;
          int box_length = (s[0] << 24) + (s[1] << 16) + (s[2] << 8) + s[3];
          if (box_length == 1 || box_length > 1000000000) {
            break;
          }
          if (s[4] == 'j' && s[5] == 'p' && s[6] == '2' && s[7] == 'c') {
            s += 8;

            width    = (s[8] << 24) + (s[9] << 16) + (s[10] << 8) + s[11];
            height   = (s[12] << 24) + (s[13] << 16) + (s[14] << 8) + s[15];
            channels = (s[40] << 8) + s[41];

            if (channels < 0 || channels > 256 || (int)read_size < 42 + buf_pos + 8 + 3 * channels || width <= 0 || height <= 0) {
              break;
            }

            bits = 0;
            for (int i = 0; i < channels; i++) {
              int cur_bits = s[42 + 3 * i];
              if (cur_bits > bits) {
                bits = cur_bits;
              }
            }
            bits++;

            found = true;
            break;
          }

          if (box_length <= 8) {
            break;
          }
          file_pos += box_length;
          if (file_pos >= size || (off_t)file_pos != (ssize_t)file_pos || (ssize_t)file_pos < 0) {
            break;
          }

          read_size = min_size;
          if (size - file_pos < min_size) {
            read_size = size - file_pos;
          }

          if (read_size < 50 || pread (read_fd, buf, read_size, (off_t)file_pos) < (ssize_t)read_size) {
            break;
          }

          buf_pos = 0;
        }

        if (!found) {
          close (read_fd);
          dl::leave_critical_section();
          return false;
        }
      } else {
        close (read_fd);
        dl::leave_critical_section();
        return false;
      }
      break;
    case 0x89: //png
      if (read_size >= 25 && !strncmp ((const char *)buf, php_sig_png, sizeof (php_sig_png))) {
        type   = IMAGETYPE_PNG;
        width  = (buf[16] << 24) + (buf[17] << 16) + (buf[18] << 8) + buf[19];
        height = (buf[20] << 24) + (buf[21] << 16) + (buf[22] << 8) + buf[23];
        bits   = buf[24];
      } else {
        close (read_fd);
        dl::leave_critical_section();
        return false;
      }
      break;
    default:
      close (read_fd);
      dl::leave_critical_section();
      return false;
  }
  close (read_fd);
  dl::leave_critical_section();

  array <var> result (array_size (4, 3, false));
  result.push_back (width);
  result.push_back (height);
  result.push_back (type);
  int len = sprintf ((char *)buf, "width=\"%d\" height=\"%d\"", width, height);
  result.push_back (string ((const char *)buf, len));
  if (bits != 0) {
    result.set_value (string ("bits", 4), bits);
  }
  if (channels != 0) {
    result.set_value (string ("channels", 8), channels);
  }
  result.set_value (string ("mime", 4), string (mime_type_string[type], (dl::size_type)strlen (mime_type_string[type])));

  return result;
}


int f$posix_getpid (void) {
  dl::enter_critical_section();//OK
  int result = (int)getpid();
  dl::leave_critical_section();
  return result;
}

int f$posix_getuid (void) {
  dl::enter_critical_section();//OK
  int result = (int)getuid();
  dl::leave_critical_section();
  return result;
}

OrFalse <array <var> > f$posix_getpwuid (int uid) {
  dl::enter_critical_section();//OK
  passwd *pwd = getpwuid(uid);
  dl::leave_critical_section();
  if (!pwd) {
    return false;
  }
  array <var> result(array_size (0, 7, false));
  result.set_value(string("name", 4), string(pwd->pw_name, (dl::size_type)strlen(pwd->pw_name)));
  result.set_value(string("passwd", 6), string(pwd->pw_passwd, (dl::size_type)strlen(pwd->pw_passwd)));
  result.set_value(string("uid", 3), (int)pwd->pw_uid);
  result.set_value(string("gid", 3), (int)pwd->pw_gid);
  result.set_value(string("gecos", 5), string(pwd->pw_gecos, (dl::size_type)strlen(pwd->pw_gecos)));
  result.set_value(string("dir", 3), string(pwd->pw_dir, (dl::size_type)strlen(pwd->pw_dir)));
  result.set_value(string("shell", 5), string(pwd->pw_shell, (dl::size_type)strlen(pwd->pw_shell)));
  return result;
}


#define AS_CONST_STRING(s) (reinterpret_cast <const string *> (&s))
#define AS_CONST_ARRAY(a) (reinterpret_cast <const array <var> *> (&a))

static inline void do_serialize (bool b) {
  static_SB.reserve (4);
  static_SB.append_char ('b');
  static_SB.append_char (':');
  static_SB.append_char (b + '0');
  static_SB.append_char (';');
}

static inline void do_serialize (int i) {
  static_SB.reserve (15);
  static_SB.append_char ('i');
  static_SB.append_char (':');
  static_SB += i;
  static_SB.append_char (';');
}

static inline void do_serialize (double f) {
  static_SB.append ("d:", 2);
  static_SB += f;
  static_SB += ';';
}

static inline void do_serialize (const string &s) {
  int len = (int)s.size();
  static_SB.reserve (20 + len);
  static_SB.append_char ('s');
  static_SB.append_char (':');
  static_SB += len;
  static_SB.append_char (':');
  static_SB.append_char ('"');
  static_SB.append_unsafe (s.c_str(), len);
  static_SB.append_char ('"');
  static_SB.append_char (';');
}

void do_serialize (const var &v) {
  switch (v.type) {
    case var::NULL_TYPE:
      static_SB.reserve (2);
      static_SB.append_char ('N');
      static_SB.append_char (';');
      return;
    case var::BOOLEAN_TYPE:
      return do_serialize (v.b);
    case var::INTEGER_TYPE:
      return do_serialize (v.i);
    case var::FLOAT_TYPE:
      return do_serialize (v.f);
    case var::STRING_TYPE:
      return do_serialize (*AS_CONST_STRING(v.s));
    case var::ARRAY_TYPE: {
      const array <var> &a = *AS_CONST_ARRAY(v.a);
      static_SB.append ("a:", 2);
      static_SB += a.count();
      static_SB.append (":{", 2);
      for (array <var>::const_iterator p = a.begin(); p != a.end(); ++p) {
        const array <var>::key_type &key = p.get_key();
        if (array <var>::is_int_key (key)) {
          do_serialize (key.to_int());
        } else {
          do_serialize (key.to_string());
        }
        do_serialize (p.get_value());
      }
      static_SB += '}';
      return;
    }
    case var::OBJECT_TYPE: {
      //TODO
      return;
    }
    default:
      php_assert (0);
      exit (1);
  }
}

string f$serialize (const var &v) {
  static_SB.clean();

  do_serialize (v);

  return static_SB.str();
}

static int do_unserialize (const char *s, int s_len, var &v) {
  if (!v.is_null()) {
    v.destroy();
  }
  switch (s[0]) {
    case 'N':
      if (s[1] == ';') {
        return 2;
      }
      break;
    case 'b':
      if (s[1] == ':' && ((unsigned int)(s[2] - '0') < 2u) && s[3] == ';') {
        new (&v) var ((bool)(s[2] - '0'));
        return 4;
      }
      break;
    case 'd':
      if (s[1] == ':') {
        s += 2;
        char *end_ptr;
        double floatval = strtod (s, &end_ptr);
        if (*end_ptr == ';' && end_ptr > s) {
          new (&v) var (floatval);
          return (int)(end_ptr - s + 3);
        }
      }
      break;
    case 'i':
      if (s[1] == ':') {
        s += 2;
        int j = 0;
        while (s[j]) {
          if (s[j] == ';') {
            int intval;
            if (php_try_to_int (s, j, &intval)) {
              s += j + 1;
              new (&v) var (intval);
              return j + 3;
            }

            int k = 0;
            if (s[k] == '-' || s[k] == '+') {
              k++;
            }
            while ('0' <= s[k] && s[k] <= '9') {
              k++;
            }
            if (k == j) {
              new (&v) var (s, j);
              return j + 3;
            }
          }
          j++;
        }
      }
      break;
    case 's':
      if (s[1] == ':') {
        s += 2;
        int j = 0, len = 0;
        while ('0' <= s[j] && s[j] <= '9') {
          len = len * 10 + s[j++] - '0';
        }
        if (j > 0 && s[j] == ':' && s[j + 1] == '"' && (dl::size_type)len < string::max_size && j + 2 + len < s_len) {
          s += j + 2;

          if (s[len] == '"' && s[len + 1] == ';') {
            new (&v) var (s, len);
            return len + 6 + j;
          }
        }
      }
      break;
    case 'a':
      if (s[1] == ':') {
        const char *s_begin = s;
        s += 2;
        int j = 0, len = 0;
        while ('0' <= s[j] && s[j] <= '9') {
          len = len * 10 + s[j++] - '0';
        }
        if (j > 0 && len >= 0 && s[j] == ':' && s[j + 1] == '{') {
          s += j + 2;
          s_len -= j + 4;

          array_size size (0, len, false);
          if (s[0] == 'i') {//try to cheat
            size = array_size (len, 0, s[1] == ':' && s[2] == '0' && s[3] == ';');
          }
          array <var> res (size);

          while (len-- > 0) {
            var key;
            int length = do_unserialize (s, s_len, key);
            if (!(length && (key.is_int() || key.is_string()))) {
              return 0;
            }
            s += length;
            s_len -= length;

            length = do_unserialize (s, s_len, res[key]);
            if (!length) {
              return 0;
            }
            s += length;
            s_len -= length;
          }

          if (s[0] == '}') {
            new (&v) var (res);
            return (int)(s - s_begin + 1);
          }
        }
      }
      break;
  }
  return 0;
}

var f$unserialize (const string &v) {
  var result;

  if (do_unserialize (v.c_str(), v.size(), result) == (int)v.size()) {
    return result;
  }

  return false;
}


static void json_append_one_char (unsigned int c) {
  static_SB.append_char ('\\');
  static_SB.append_char ('u');
  static_SB.append_char ("0123456789abcdef"[c >> 12]);
  static_SB.append_char ("0123456789abcdef"[(c >> 8) & 15]);
  static_SB.append_char ("0123456789abcdef"[(c >> 4) & 15]);
  static_SB.append_char ("0123456789abcdef"[c & 15]);
}

static bool json_append_char (unsigned int c) {
  if (c < 0x10000) {
    if (0xD7FF < c && c < 0xE000) {
      return false;
    }
    json_append_one_char (c);
    return true;
  } else if (c <= 0x10ffff){
    c -= 0x10000;
    json_append_one_char (0xD800 | (c >> 10));
    json_append_one_char (0xDC00 | (c & 0x3FF));
    return true;
  } else {
    return false;
  }
}

static void do_json_encode_string_php (const char *s, int len) {
  int begin_pos = static_SB.size();
  static_SB.reserve (6 * len + 2);
  static_SB.append_char ('"');

#define ERROR {static_SB.set_pos (begin_pos); static_SB.append ("null", 4); return;}
#define CHECK(x) if (!(x)) {php_warning ("Not a valid utf-8 character at pos %d in function json_encode", pos); ERROR}
#define APPEND_CHAR(x) if (!json_append_char (x)) ERROR
  
  int a, b, c, d;
  for (int pos = 0; pos < len; pos++) {
    switch (s[pos]) {
    case '"':
      static_SB.append_char ('\\');
      static_SB.append_char ('"');
      break;
    case '\\':
      static_SB.append_char ('\\');
      static_SB.append_char ('\\');
      break;
    case '/':
      static_SB.append_char ('\\');
      static_SB.append_char ('/');
      break;
    case '\b':
      static_SB.append_char ('\\');
      static_SB.append_char ('b');
      break;
    case '\f':
      static_SB.append_char ('\\');
      static_SB.append_char ('f');
      break;
    case '\n':
      static_SB.append_char ('\\');
      static_SB.append_char ('n');
      break;
    case '\r':
      static_SB.append_char ('\\');
      static_SB.append_char ('r');
      break;
    case '\t':
      static_SB.append_char ('\\');
      static_SB.append_char ('t');
      break;
    case 0 ... 7:
    case 11:
    case 14 ... 31:
      json_append_one_char (s[pos]);
      break;
    case -128 ... -1:
      a = s[pos];
      CHECK ((a & 0x40) != 0);

      b = s[++pos];
      CHECK((b & 0xc0) == 0x80);
      if ((a & 0x20) == 0) {
        CHECK((a & 0x1e) > 0);
        APPEND_CHAR(((a & 0x1f) << 6) | (b & 0x3f));
        break;
      }

      c = s[++pos];
      CHECK((c & 0xc0) == 0x80);
      if ((a & 0x10) == 0) {
        CHECK(((a & 0x0f) | (b & 0x20)) > 0);
        APPEND_CHAR(((a & 0x0f) << 12) | ((b & 0x3f) << 6) | (c & 0x3f));
        break;
      }

      d = s[++pos];
      CHECK((d & 0xc0) == 0x80);
      if ((a & 0x08) == 0) {
        CHECK(((a & 0x07) | (b & 0x30)) > 0);
        APPEND_CHAR(((a & 0x07) << 18) | ((b & 0x3f) << 12) | ((c & 0x3f) << 6) | (d & 0x3f));
        break;
      }

      CHECK(0);
      break;
    default:
      static_SB.append_char (s[pos]);
      break;
    }
  }

  static_SB.append_char ('"');
#undef ERROR
#undef CHECK
#undef APPEND_CHAR
}

static void do_json_encode_string_vkext (const char *s, int len) {
  static_SB.reserve (2 * len + 2);
  static_SB.append_char ('"');

  for (int pos = 0; pos < len; pos++) {
    char c = s[pos];
    if (unlikely ((unsigned int)c < 32u)) {
      switch (c) {
        case '\b':
          static_SB.append_char ('\\');
          static_SB.append_char ('b');
          break;
        case '\f':
          static_SB.append_char ('\\');
          static_SB.append_char ('f');
          break;
        case '\n':
          static_SB.append_char ('\\');
          static_SB.append_char ('n');
          break;
        case '\r':
          static_SB.append_char ('\\');
          static_SB.append_char ('r');
          break;
        case '\t':
          static_SB.append_char ('\\');
          static_SB.append_char ('t');
          break;
      }
    } else {
      if (c == '"' || c == '\\' || c == '/') {
        static_SB.append_char ('\\');
      }
      static_SB.append_char (c);
    }
  }

  static_SB.append_char ('"');
}

void do_json_encode (const var &v, bool simple_encode) {
  switch (v.type) {
    case var::NULL_TYPE:
      static_SB.append ("null", 4);
      return;
    case var::BOOLEAN_TYPE:
      if (v.b) {
        static_SB.append ("true", 4);
      } else {
        static_SB.append ("false", 5);
      }
      return;
    case var::INTEGER_TYPE:
      static_SB += v.i;
      return;
    case var::FLOAT_TYPE:
      if (is_ok_float (v.f)) {
        static_SB += (simple_encode ? f$number_format (v.f, 6, DOT, string()) : string (v.f));
      } else {
        php_warning ("strange double %lf in function json_encode", v.f);
        static_SB.append ("null", 4);
      }
      return;
    case var::STRING_TYPE:
      if (simple_encode) {
        do_json_encode_string_vkext (AS_CONST_STRING(v.s)->c_str(), AS_CONST_STRING(v.s)->size());
      } else {
        do_json_encode_string_php (AS_CONST_STRING(v.s)->c_str(), AS_CONST_STRING(v.s)->size());
      }
      return;
    case var::ARRAY_TYPE: {
      const array <var> &a = *AS_CONST_ARRAY(v.a);
      bool is_vector = a.is_vector();
      if (!is_vector && a.size().string_size == 0 && a.get_next_key() == a.count()) {
        int n = 0;
        for (array <var>::const_iterator p = a.begin(); p != a.end(); ++p) {
          if (p.get_key().to_int() != n) {
            break;
          }
          n++;
        }
        if (n == a.count()) {
          is_vector = true;
        }
      }

      static_SB += "{["[is_vector];

      for (array <var>::const_iterator p = a.begin(); p != a.end(); ++p) {
        if (p != a.begin()) {
          static_SB += ',';
        }

        if (!is_vector) {
          const array <var>::key_type key = p.get_key();
          if (array <var>::is_int_key (key)) {
            static_SB + '"' + key.to_int() + '"';
          } else {
            do_json_encode (key, simple_encode);
          }
          static_SB += ':';
        }

        do_json_encode (p.get_value(), simple_encode);
      }

      static_SB += "}]"[is_vector];

    }
    case var::OBJECT_TYPE: {
      //TODO
      return;
    }
    default:
      php_assert (0);
      exit (1);
  }
}

string f$json_encode (const var &v, bool simple_encode) {
  static_SB.clean();

  do_json_encode (v, simple_encode);

  return static_SB.str();
}

int string_buffer::string_buffer_error_flag = 0; // TODO: move in more logic place

string f$vk_json_encode_safe (const var &v, bool simple_encode) {
  static_SB.clean();
  string_buffer::string_buffer_error_flag = STRING_BUFFER_ERROR_FLAG_ON;
  do_json_encode (v, simple_encode);
  if (string_buffer::string_buffer_error_flag == STRING_BUFFER_ERROR_FLAG_FAILED) {
    static_SB.clean ();
    string_buffer::string_buffer_error_flag = STRING_BUFFER_ERROR_FLAG_OFF;
    THROW_EXCEPTION (f$new_Exception (string(__FILE__, (dl::size_type)strlen(__FILE__)), __LINE__, string("json_encode buffer overflow", 27)));
    return string();
  }
  string_buffer::string_buffer_error_flag = STRING_BUFFER_ERROR_FLAG_OFF;
  return static_SB.str();
}


static void json_skip_blanks(const char* s, int &i){
  while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') {
    i++;
  }
}

static bool do_json_decode (const char *s, int s_len, int &i, var &v) {
  if (!v.is_null()) {
    v.destroy();
  }
  json_skip_blanks(s, i);
  switch (s[i]) {
    case 'n':
      if (s[i + 1] == 'u' &&
          s[i + 2] == 'l' &&
          s[i + 3] == 'l') {
        i += 4;
        return true;
      }
      break;
    case 't':
      if (s[i + 1] == 'r' &&
          s[i + 2] == 'u' &&
          s[i + 3] == 'e') {
        i += 4;
        new (&v) var (true);
        return true;
      }
      break;
    case 'f':
      if (s[i + 1] == 'a' &&
          s[i + 2] == 'l' &&
          s[i + 3] == 's' &&
          s[i + 4] == 'e') {
        i += 5;
        new (&v) var (false);
        return true;
      }
      break;
    case '"': {
      int j = i + 1;
      int slashes = 0;
      while (j < s_len && s[j] != '"') {
        if (s[j] == '\\') {
          slashes++;
          j++;
        }
        j++;
      }
      if (j < s_len) {
        int len = j - i - 1 - slashes;

        string value (len, false);

        i++;
        int l;
        for (l = 0; l < len && i < j; l++) {
          char c = s[i];
          if (c == '\\') {
            i++;
            switch (s[i]) {
              case '"':
              case '\\':
              case '/':
                value[l] = s[i];
                break;
              case 'b':
                value[l] = '\b';
                break;
              case 'f':
                value[l] = '\f';
                break;
              case 'n':
                value[l] = '\n';
                break;
              case 'r':
                value[l] = '\r';
                break;
              case 't':
                value[l] = '\t';
                break;
              case 'u':
                if (isxdigit (s[i + 1]) && isxdigit (s[i + 2]) && isxdigit (s[i + 3]) && isxdigit (s[i + 4])) {
                  int num = 0;
                  for (int t = 0; t < 4; t++) {
                    char c = s[++i];
                    if ('0' <= c && c <= '9') {
                      num = num * 16 + c - '0';
                    } else {
                      c |= 0x20;
                      if ('a' <= c && c <= 'f') {
                        num = num * 16 + c - 'a' + 10;
                      }
                    }
                  }

                  if (0xD7FF < num && num < 0xE000) {
                    if (s[i + 1] == '\\' && s[i + 2] == 'u' &&
                        isxdigit (s[i + 3]) && isxdigit (s[i + 4]) && isxdigit (s[i + 5]) && isxdigit (s[i + 6])) {
                      i += 2;
                      int u = 0;
                      for (int t = 0; t < 4; t++) {
                        char c = s[++i];
                        if ('0' <= c && c <= '9') {
                          u = u * 16 + c - '0';
                        } else {
                          c |= 0x20;
                          if ('a' <= c && c <= 'f') {
                            u = u * 16 + c - 'a' + 10;
                          }
                        }
                      }

                      if (0xD7FF < u && u < 0xE000) {
                        num = (((num & 0x3FF) << 10) | (u & 0x3FF)) + 0x10000;
                      } else {
                        i -= 6;
//                        return false;
                      }
                    } else {
//                      return false;
                    }
                  }

                  if (num < 128) {
                    value[l] = (char)num;
                  } else if (num < 0x800) {
                    value[l++] = (char)(0xc0 + (num >> 6));
                    value[l] = (char)(0x80 + (num & 63));
                  } else if (num < 0xffff) {
                    value[l++] = (char)(0xe0 + (num >> 12));
                    value[l++] = (char)(0x80 + ((num >> 6) & 63));
                    value[l] = (char)(0x80 + (num & 63));
                  } else {
                    value[l++] = (char)(0xf0 + (num >> 18));
                    value[l++] = (char)(0x80 + ((num >> 12) & 63));
                    value[l++] = (char)(0x80 + ((num >> 6) & 63));
                    value[l] = (char)(0x80 + (num & 63));
                  }
                  break;
                }
              default:
                return false;
            }
            i++;
          } else {
            value[l] = s[i++];
          }
        }
        value.shrink (l);

        new (&v) var (value);
        i++;
        return true;
      }
      break;
    }
    case '[': {
      array <var> res;
      i++;
      json_skip_blanks(s, i);
      if (s[i] != ']') {
        do {
          var value;
          if (!do_json_decode (s, s_len, i, value)) {
            return false;
          }
          res.push_back (value);
          json_skip_blanks(s, i);
        } while (s[i++] == ',');

        if (s[i - 1] != ']') {
          return false;
        }
      } else {
        i++;
      }

      new (&v) var (res);
      return true;
    }
    case '{': {
      array <var> res;
      i++;
      json_skip_blanks(s, i);
      if (s[i] != '}') {
        do {
          var key;
          if (!do_json_decode (s, s_len, i, key) || !key.is_string()) {
            return false;
          }
          json_skip_blanks(s, i);
          if (s[i++] != ':') {
            return false;
          }

          if (!do_json_decode (s, s_len, i, res[key])) {
            return false;
          }
          json_skip_blanks(s, i);
        } while (s[i++] == ',');

        if (s[i - 1] != '}') {
          return false;
        }
      } else {
        i++;
      }

      new (&v) var (res);
      return true;
    }
    default: {
      int j = i;
      while (s[j] == '-' || ('0' <= s[j] && s[j] <= '9') || s[j] == 'e' || s[j] == 'E' || s[j] == '+' || s[j] == '.') {
        j++;
      }
      if (j > i) {
        int intval;
        if (php_try_to_int (s + i, j - i, &intval)) {
          i = j;
          new (&v) var (intval);
          return true;
        }

        char *end_ptr;
        double floatval = strtod (s + i, &end_ptr);
        if (end_ptr == s + j) {
          i = j;
          new (&v) var (floatval);
          return true;
        }
      }
      break;
    }
  }

  return false;
}

var f$json_decode (const string &v, bool assoc) {
  if (!assoc) {
//    php_warning ("json_decode doesn't support decoding to class, returning array");
  }

  var result;
  int i = 0;
  if (do_json_decode (v.c_str(), v.size(), i, result)) {
    while (v[i] == ' ') {
      i++;
    }
    if (i == (int)v.size()) {
      return result;
    }
  }

  return var();
}

void do_print_r (const var &v, int depth) {
  if (depth == 10) {
    php_warning ("Depth %d reached. Recursion?", depth);
    return;
  }

  switch (v.type) {
    case var::NULL_TYPE:
      break;
    case var::BOOLEAN_TYPE:
      if (v.b) {
        *coub += '1';
      }
      break;
    case var::INTEGER_TYPE:
      *coub += v.i;
      break;
    case var::FLOAT_TYPE:
      *coub += v.f;
      break;
    case var::STRING_TYPE:
      *coub += *AS_CONST_STRING(v.s);
      break;
    case var::ARRAY_TYPE: {
      const array <var> *a = AS_CONST_ARRAY(v.a);
      *coub += "Array\n";

      string shift (depth << 3, ' ');
      *coub + shift + "(\n";

      for (array <var>::const_iterator it = a->begin(); it != a->end(); ++it) {
        *coub + shift + "    [" + it.get_key() + "] => ";
        do_print_r (it.get_value(), depth + 1);
        *coub += '\n';
      }

      *coub + shift + ")\n";
      break;
    }
    case var::OBJECT_TYPE: {
      //TODO
      break;
    }
    default:
      php_assert (0);
      exit (1);
  }
}

void do_var_dump (const var &v, int depth) {
  if (depth == 10) {
    php_warning ("Depth %d reached. Recursion?", depth);
    return;
  }

  string shift (depth * 2, ' ');

  switch (v.type) {
    case var::NULL_TYPE:
      *coub + shift + "NULL";
      break;
    case var::BOOLEAN_TYPE:
      *coub + shift + "bool(" + (v.b ? "true" : "false") + ')';
      break;
    case var::INTEGER_TYPE:
      *coub + shift + "int(" + v.i + ')';
      break;
    case var::FLOAT_TYPE:
      *coub + shift + "float(" + v.f + ')';
      break;
    case var::STRING_TYPE:
      *coub + shift + "string(" + (int)AS_CONST_STRING(v.s)->size() + ") \"" + *AS_CONST_STRING(v.s) + '"';
      break;
    case var::ARRAY_TYPE: {
      const array <var> *a = AS_CONST_ARRAY(v.a);
      string shift (depth * 2, ' ');

      *coub + shift + (0 && a->is_vector() ? "vector(" : "array(") + a->count() + ") {\n";

      for (array <var>::const_iterator it = a->begin(); it != a->end(); ++it) {
        *coub + shift + "  [";
        if (array <var>::is_int_key (it.get_key())) {
          *coub += it.get_key();
        } else {
          *coub + '"' + it.get_key() + '"';
        }
        *coub += "]=>\n";
        do_var_dump (it.get_value(), depth + 1);
      }

      *coub + shift + "}";
      break;
    }
    case var::OBJECT_TYPE: {
      //TODO
      break;
    }
    default:
      php_assert (0);
      exit (1);
  }
  *coub += '\n';
}

void var_export_escaped_string(const string &s) {
  for (size_t i = 0; i < s.size(); i++) {
    switch (s[i]) {
      case '\'':
      case '\\':
        *coub + "\\";
        *coub + s[i];
        break;
      case '\0':
        *coub + "\' . \"\\0\" . \'";
        break;
      default:
        *coub + s[i];
    }
  }
}

void do_var_export (const var &v, int depth, char endc) {
  if (depth == 10) {
    php_warning ("Depth %d reached. Recursion?", depth);
    return;
  }

  string shift (depth * 2, ' ');

  switch (v.type) {
    case var::NULL_TYPE:
      *coub + shift + "NULL";
      break;
    case var::BOOLEAN_TYPE:
      *coub + shift + (v.b ? "true" : "false");
      break;
    case var::INTEGER_TYPE:
      *coub + shift + v.i;
      break;
    case var::FLOAT_TYPE:
      *coub + shift + v.f;
      break;
    case var::STRING_TYPE:
      *coub + shift + '\'';
      var_export_escaped_string(*AS_CONST_STRING(v.s));
      *coub + '\'';
      break;
    case var::ARRAY_TYPE: {
      const array <var> *a = AS_CONST_ARRAY(v.a);
      string shift (depth * 2, ' ');

      bool is_vector = a->is_vector();
      *coub + shift + "array(\n";

      for (array <var>::const_iterator it = a->begin(); it != a->end(); ++it) {
        if (!is_vector) {
          *coub + shift;
          if (array <var>::is_int_key (it.get_key())) {
            *coub += it.get_key();
          } else {
            *coub + '\'' + it.get_key() + '\'';
          }
          *coub += " =>";
          if (it.get_value().type == var::ARRAY_TYPE){
            *coub += "\n";
            do_var_export (it.get_value(), depth + 1, ',');
          } else {
            do_var_export (it.get_value(), 1, ',');
          }
        } else {
          do_var_export (it.get_value(), depth + 1, ',');
        }
      }

      *coub + shift + ")";
      break;
    }
    case var::OBJECT_TYPE: {
      //TODO
      break;
    }
    default:
      php_assert (0);
      exit (1);
  }
  if (endc != 0) {
    *coub += endc;
  }
  *coub += '\n';
}


#undef AS_CONST_STRING
#undef AS_CONST_ARRAY

string f$print_r (const var &v, bool buffered) {
  if (buffered) {
    f$ob_start();
    do_print_r (v, 0);
    return f$ob_get_clean().val();
  }

  do_print_r (v, 0);
  if (run_once) {
    dprintf(kstdout, "%s", f$ob_get_contents().c_str());
    f$ob_clean();
  }
  return string();
}

void f$var_dump (const var &v) {
  do_var_dump (v, 0);
  if (run_once) {
    dprintf(kstdout, "%s", f$ob_get_contents().c_str());
    f$ob_clean();
  }
}

string f$var_export (const var &v, bool buffered) {
  if (buffered) {
    f$ob_start();
    do_var_export (v, 0);
    return f$ob_get_clean().val();
  }
  do_var_export (v, 0);
  if (run_once) {
    dprintf(kstdout, "%s", f$ob_get_contents().c_str());
    f$ob_clean();
  }
  return string();
}



int f$system (const string &query) {
  return system (query.c_str());
}
