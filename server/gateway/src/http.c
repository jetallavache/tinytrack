#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/log/log.h"
#include "printf.h"

static int ncasecmp(const char* s1, const char* s2, size_t len) {
  int diff = 0;
  if (len > 0)
    do {
      int c = *s1++, d = *s2++;
      if (c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
      if (d >= 'A' && d <= 'Z')
        d += 'a' - 'A';
      diff = c - d;
    } while (diff == 0 && s1[-1] != '\0' && --len > 0);
  return diff;
}

static bool isok(uint8_t c) {
  return c == '\n' || c == '\r' || c == '\t' || c >= ' ';
}

int ttg_http_get_request_len(const unsigned char* buf, size_t buf_len) {
  size_t i;
  for (i = 0; i < buf_len; i++) {
    if (!isok(buf[i]))
      return -1;
    if ((i > 0 && buf[i] == '\n' && buf[i - 1] == '\n') ||
        (i > 3 && buf[i] == '\n' && buf[i - 1] == '\r' && buf[i - 2] == '\n'))
      return (int)i + 1;
  }
  return 0;
}

/* Дублирующиеся заголовки: политика "первый побеждает" — возвращается первое
 * вхождение. Это соответствует поведению большинства HTTP-серверов и RFC 7230
 * §3.2.2 для заголовков, не допускающих объединения. */
struct ttg_str* ttg_http_get_header(struct ttg_http_message* h,
                                    const char* name) {
  size_t i, n = strlen(name), max = sizeof(h->headers) / sizeof(h->headers[0]);
  for (i = 0; i < max && h->headers[i].name.len > 0; i++) {
    struct ttg_str *k = &h->headers[i].name, *v = &h->headers[i].value;
    if (n == k->len && ncasecmp(k->buf, name, n) == 0)
      return v;
  }
  return NULL;
}

/* This is a valid UTF-8 continuation byte. */
static bool vcb(uint8_t c) {
  return (c & 0xc0) == 0x80;
}

/* Get the length of a character (valid UTF-8). Used to analyze the method, URI,
 * headers */
static size_t clen(const char* s, const char* end) {
  const unsigned char *u = (unsigned char*)s, c = *u;
  long n = (long)(end - s);
  if (c > ' ' && c <= '~')
    return 1; /* Usual ascii printed char */
  if ((c & 0xe0) == 0xc0 && n > 1 && vcb(u[1]))
    return 2; /* 2-byte UTF8 */
  if ((c & 0xf0) == 0xe0 && n > 2 && vcb(u[1]) && vcb(u[2]))
    return 3;
  if ((c & 0xf8) == 0xf0 && n > 3 && vcb(u[1]) && vcb(u[2]) && vcb(u[3]))
    return 4;
  return 0;
}

/* Skip to a new line. Return the extended letter "s" or NULL in case of an
 * error. Rejects null bytes and non-printable control characters in values. */
static const char* skiptorn(const char* s, const char* end, struct ttg_str* v) {
  v->buf = (char*)s;
  while (s < end && s[0] != '\n' && s[0] != '\r') {
    if ((unsigned char)s[0] < ' ' && s[0] != '\t')
      return NULL; /* null byte or control char in header value */
    s++, v->len++; /* To newline */
  }
  if (s >= end || (s[0] == '\r' && s[1] != '\n'))
    return NULL; /* Stray \r */
  if (s[0] == '\r')
    s++; /* Skip \r */
  if (s >= end || *s++ != '\n')
    return NULL; /* Skip \n */
  return s;
}

static bool ttg_http_parse_headers(const char* s, const char* end,
                                   struct ttg_http_header* h, size_t max_hdrs) {
  size_t i, n;
  for (i = 0; i < max_hdrs; i++) {
    struct ttg_str k = {NULL, 0}, v = {NULL, 0};
    if (s >= end)
      return false;
    if (s[0] == '\n' || (s[0] == '\r' && s[1] == '\n'))
      break;
    k.buf = (char*)s;
    while (s < end && s[0] != ':' && (n = clen(s, end)) > 0)
      s += n, k.len += n;
    if (k.len == 0)
      return false; /* Empty name */
    if (s >= end || clen(s, end) == 0)
      return false; /* Invalid UTF-8 */
    if (*s++ != ':')
      return false; /* Invalid, not followed by : */
    /* if (clen(s, end) == 0) return false;  /* Invalid UTF-8 */
    while (s < end && (s[0] == ' ' || s[0] == '\t'))
      s++; /* Skip spaces */
    if ((s = skiptorn(s, end, &v)) == NULL)
      return false;
    while (v.len > 0 && (v.buf[v.len - 1] == ' ' || v.buf[v.len - 1] == '\t')) {
      v.len--; /* Trim spaces */
    }
    /* tt_log_info("--HH [%.*s] [%.*s]", (int)k.len, k.buf, (int)v.len, v.buf);
     */
    h[i].name = k, h[i].value = v; /* Success. Assign values */
  }
  return true;
}

/* bool to_size_t(struct mg_str str, size_t *val); */
bool to_size_t(struct ttg_str str, size_t* val) {
  size_t i = 0, max = (size_t)-1, max2 = max / 10, result = 0, ndigits = 0;
  while (i < str.len && (str.buf[i] == ' ' || str.buf[i] == '\t'))
    i++;
  if (i < str.len && str.buf[i] == '-')
    return false;
  while (i < str.len && str.buf[i] >= '0' && str.buf[i] <= '9') {
    size_t digit = (size_t)(str.buf[i] - '0');
    if (result > max2)
      return false; /* Overflow */
    result *= 10;
    if (result > max - digit)
      return false; /* Overflow */
    result += digit;
    i++, ndigits++;
  }
  while (i < str.len && (str.buf[i] == ' ' || str.buf[i] == '\t'))
    i++;
  if (ndigits == 0)
    return false; /* #2322: Content-Length = 1 * DIGIT */
  if (i != str.len)
    return false; /* Ditto */
  *val = (size_t)result;
  return true;
}

int ttg_http_parse(const char* s, size_t len, struct ttg_http_message* hm) {
  int is_response, req_len = ttg_http_get_request_len((unsigned char*)s, len);
  const char *end = s == NULL ? NULL : s + req_len, *qs;
  const struct ttg_str* cl;
  size_t n;
  bool version_prefix_valid;

  memset(hm, 0, sizeof(*hm));
  if (req_len <= 0)
    return req_len;

  hm->message.buf = hm->head.buf = (char*)s;
  hm->body.buf = (char*)end;
  hm->head.len = (size_t)req_len;
  hm->message.len = hm->body.len = (size_t)-1;

  hm->method.buf = (char*)s;
  while (s < end && (n = clen(s, end)) > 0)
    s += n, hm->method.len += n;
  while (s < end && s[0] == ' ')
    s++;
  hm->uri.buf = (char*)s;
  while (s < end && (n = clen(s, end)) > 0)
    s += n, hm->uri.len += n;
  while (s < end && s[0] == ' ')
    s++;
  is_response =
      hm->method.len > 5 && (ncasecmp(hm->method.buf, "HTTP/", 5) == 0);
  if ((s = skiptorn(s, end, &hm->proto)) == NULL)
    return false;
  /* If we're given a version, check that it is HTTP/x.x */
  version_prefix_valid =
      hm->proto.len > 5 && (ncasecmp(hm->proto.buf, "HTTP/", 5) == 0);
  if (!is_response && hm->proto.len > 0 &&
      (!version_prefix_valid || hm->proto.len != 8 ||
       (hm->proto.buf[5] < '0' || hm->proto.buf[5] > '9') ||
       (hm->proto.buf[6] != '.') ||
       (hm->proto.buf[7] < '0' || hm->proto.buf[7] > '9'))) {
    return -1;
  }

  /* If URI contains '?' character, setup query string */
  if ((qs = (const char*)memchr(hm->uri.buf, '?', hm->uri.len)) != NULL) {
    hm->query.buf = (char*)qs + 1;
    hm->query.len = (size_t)(&hm->uri.buf[hm->uri.len] - (qs + 1));
    hm->uri.len = (size_t)(qs - hm->uri.buf);
  }

  /* Sanity check. Allow protocol/reason to be empty */
  /* Do this check after hm->method.len and hm->uri.len are finalised */
  if (hm->method.len == 0 || hm->uri.len == 0)
    return -1;

  if (!ttg_http_parse_headers(s, end, hm->headers,
                              sizeof(hm->headers) / sizeof(hm->headers[0])))
    return -1; /* error when parsing */
  if ((cl = ttg_http_get_header(hm, "Content-Length")) != NULL) {
    if (to_size_t(*cl, &hm->body.len) == false)
      return -1;
    hm->message.len = (size_t)req_len + hm->body.len;
  }

  if (hm->body.len == (size_t)~0 && !is_response &&
      ttg_str_casecmp(hm->method, str("PUT")) != 0 &&
      ttg_str_casecmp(hm->method, str("POST")) != 0) {
    hm->body.len = 0;
    hm->message.len = (size_t)req_len;
  }

  if (hm->body.len == (size_t)~0 && is_response &&
      ttg_str_casecmp(hm->uri, str("204")) == 0) {
    hm->body.len = 0;
    hm->message.len = (size_t)req_len;
  }
  if (hm->message.len < (size_t)req_len)
    return -1; /* Overflow protection */

  return req_len;
}

static const char* ttg_http_status_code_str(int status_code) {
  switch (status_code) {
    case 100:
      return "Continue";
    case 101:
      return "Switching Protocols";
    case 102:
      return "Processing";
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 202:
      return "Accepted";
    case 203:
      return "Non-authoritative Information";
    case 204:
      return "No Content";
    case 205:
      return "Reset Content";
    case 206:
      return "Partial Content";
    case 207:
      return "Multi-Status";
    case 208:
      return "Already Reported";
    case 226:
      return "IM Used";
    case 300:
      return "Multiple Choices";
    case 301:
      return "Moved Permanently";
    case 302:
      return "Found";
    case 303:
      return "See Other";
    case 304:
      return "Not Modified";
    case 305:
      return "Use Proxy";
    case 307:
      return "Temporary Redirect";
    case 308:
      return "Permanent Redirect";
    case 400:
      return "Bad Request";
    case 401:
      return "Unauthorized";
    case 402:
      return "Payment Required";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 406:
      return "Not Acceptable";
    case 407:
      return "Proxy Authentication Required";
    case 408:
      return "Request Timeout";
    case 409:
      return "Conflict";
    case 410:
      return "Gone";
    case 411:
      return "Length Required";
    case 412:
      return "Precondition Failed";
    case 413:
      return "Payload Too Large";
    case 414:
      return "Request-URI Too Long";
    case 415:
      return "Unsupported Media Type";
    case 416:
      return "Requested Range Not Satisfiable";
    case 417:
      return "Expectation Failed";
    case 418:
      return "I'm a teapot";
    case 421:
      return "Misdirected Request";
    case 422:
      return "Unprocessable Entity";
    case 423:
      return "Locked";
    case 424:
      return "Failed Dependency";
    case 426:
      return "Upgrade Required";
    case 428:
      return "Precondition Required";
    case 429:
      return "Too Many Requests";
    case 431:
      return "Request Header Fields Too Large";
    case 444:
      return "Connection Closed Without Response";
    case 451:
      return "Unavailable For Legal Reasons";
    case 499:
      return "Client Closed Request";
    case 500:
      return "Internal Server Error";
    case 501:
      return "Not Implemented";
    case 502:
      return "Bad Gateway";
    case 503:
      return "Service Unavailable";
    case 504:
      return "Gateway Timeout";
    case 505:
      return "HTTP Version Not Supported";
    case 506:
      return "Variant Also Negotiates";
    case 507:
      return "Insufficient Storage";
    case 508:
      return "Loop Detected";
    case 510:
      return "Not Extended";
    case 511:
      return "Network Authentication Required";
    case 599:
      return "Network Connect Timeout Error";
    default:
      return "";
  }
}

void ttg_http_reply(struct ttg_conn* c, int code, const char* headers,
                    const char* fmt, ...) {
  va_list ap;
  size_t len;
  ttg_net_printf(c, "HTTP/1.1 %d %s\r\n%sContent-Length:            \r\n\r\n",
                 code, ttg_http_status_code_str(code),
                 headers == NULL ? "" : headers);
  len = c->send.len;
  va_start(ap, fmt);
  ttg_vxprintf(ttg_pfn_iobuf, &c->send, fmt, &ap);
  va_end(ap);
  if (c->send.len > 16) {
    size_t n = snprintf((char*)&c->send.buf[len - 15], 11, "%-10lu",
                        (unsigned long)(c->send.len - len));
    c->send.buf[len - 15 + n] = ' '; /* Change ending 0 to space */
  }
  c->is_resp = 0;
}

int ttg_http_status(const struct ttg_http_message* hm) {
  return atoi(hm->uri.buf);
}

static bool is_hex_digit(int c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static int skip_chunk(const char* buf, int len, int* pl, int* dl) {
  int i = 0, n = 0;
  if (len < 3)
    return 0;
  while (i < len && is_hex_digit(buf[i]))
    i++;
  if (i == 0)
    return -1; /* Error, no length specified */
  if (i > (int)sizeof(int) * 2)
    return -1; /* Chunk length is too big */
  if (len < i + 1 || buf[i] != '\r' || buf[i + 1] != '\n')
    return -1; /* Error */
  if (ttg_str_to_num(strl(buf, (size_t)i), 16, &n, sizeof(int)) == false)
    return -1; /* Decode chunk length, overflow */
  if (n < 0)
    return -1; /* Error. TODO(): some checks now redundant */
  if (n > len - i - 4)
    return 0; /* Chunk not yet fully buffered */
  if (buf[i + n + 2] != '\r' || buf[i + n + 3] != '\n')
    return -1; /* Error */
  *pl = i + 2, *dl = n;
  return i + 2 + n + 2;
}

static void http_cb(struct ttg_conn* c, int ev, void* ev_data) {
  if (ev == TTG_EVENT_READ || ev == TTG_EVENT_CLOSE ||
      (ev == TTG_EVENT_POLL && c->is_accepted && !c->is_draining &&
       c->recv.len > 0)) {
    struct ttg_http_message hm;
    size_t ofs = 0; /* Parsing offset */

    while (c->is_resp == 0 && ofs < c->recv.len) {
      const char* buf = (char*)c->recv.buf + ofs;
      int n = ttg_http_parse(buf, c->recv.len - ofs, &hm);
      struct ttg_str* te; /* Transfer - encoding header */
      bool is_chunked = false;
      size_t old_len = c->recv.len;

      if (n < 0) {
        /* We don't use ttg_error() here, to avoid closing pipelined requests */
        /* prematurely */
        tt_log_err("HTTP parse, %lu bytes", c->recv.len);
        c->is_draining = 1;
        /* ttg_hexdump(buf, c->recv.len - ofs > 16 ? 16 : c->recv.len - ofs); */
        /* LOG??? */
        c->recv.len = 0;
        return;
      }
      if (n == 0)
        break; /* Request is not buffered yet */

      /* Validate URI and headers size against configured limits */
      {
        uint32_t max_uri = c->mgr->max_uri_size ? c->mgr->max_uri_size : 8192;
        uint32_t max_hdr =
            c->mgr->max_headers_size ? c->mgr->max_headers_size : 16384;
        if (hm.uri.len > max_uri) {
          tt_log_info("%lu URI too long (%zu > %u), sending 414", c->id,
                      hm.uri.len, max_uri);
          ttg_http_reply(c, 414, "", "URI Too Long");
          c->is_draining = 1;
          return;
        }
        if ((size_t)n > max_hdr) {
          tt_log_info("%lu headers too large (%d > %u), sending 431", c->id, n,
                      max_hdr);
          ttg_http_reply(c, 431, "", "Request Header Fields Too Large");
          c->is_draining = 1;
          return;
        }
      }

      ttg_event_call(c, TTG_EVENT_HTTP_HDRS, &hm); /* Got all HTTP headers */
      if (c->recv.len != old_len) {
        /* User manipulated received data. Wash our hands */
        tt_log_debug("%lu detaching HTTP handler", c->id);
        c->pfn = NULL;
        return;
      }
      if (ev == TTG_EVENT_CLOSE) { /* If client did not set Content-Length */
        hm.message.len = c->recv.len - ofs; /* and closes now, deliver MSG */
        hm.body.len = hm.message.len - (size_t)(hm.body.buf - hm.message.buf);
      }
      if ((te = ttg_http_get_header(&hm, "Transfer-Encoding")) != NULL) {
        if (ttg_str_casecmp(*te, str("chunked")) == 0) {
          is_chunked = true;
        } else {
          ttg_event_error(c, "Invalid Transfer-Encoding"); /* See #2460 */
          return;
        }
      } else if (ttg_http_get_header(&hm, "Content-length") == NULL) {
        /* HTTP packets must contain either Transfer-Encoding or */
        /* Content-length */
        bool is_response = ncasecmp(hm.method.buf, "HTTP/", 5) == 0;
        bool require_content_len = false;
        if (!is_response && (ttg_str_casecmp(hm.method, str("POST")) == 0 ||
                             ttg_str_casecmp(hm.method, str("PUT")) == 0)) {
          /* POST and PUT should include an entity body. Therefore, they should
           */
          /* contain a Content-length header. Other requests can also contain a
           */
          /* body, but their content has no defined semantics (RFC 7231) */
          require_content_len = true;
          ofs += (size_t)n; /* this request has been processed */
        } else if (is_response) {
          /* HTTP spec 7.2 Entity body: All other responses must include a body
           */
          /* or Content-Length header field defined with a value of 0. */
          int status = ttg_http_status(&hm);
          require_content_len = status >= 200 && status != 204 && status != 304;
        }
        if (require_content_len) {
          if (!c->is_client)
            ttg_http_reply(c, 411, "", "");
          tt_log_err("Content length missing from %s",
                     is_response ? "response" : "request");
        }
      }

      if (is_chunked) {
        /* For chunked data, strip off prefixes and suffixes from chunks */
        /* and relocate them right after the headers, then report a message */
        char* s = (char*)c->recv.buf + ofs + n;
        int o = 0, pl, dl, cl, len = (int)(c->recv.len - ofs - (size_t)n);

        /* Find zero-length chunk (the end of the body) */
        while ((cl = skip_chunk(s + o, len - o, &pl, &dl)) > 0 && dl)
          o += cl;
        if (cl == 0)
          break; /* No zero-len chunk, buffer more data */
        if (cl < 0) {
          ttg_event_error(c, "Invalid chunk");
          break;
        }

        /* Zero chunk found. Second pass: strip + relocate */
        o = 0, hm.body.len = 0, hm.message.len = (size_t)n;
        while ((cl = skip_chunk(s + o, len - o, &pl, &dl)) > 0) {
          memmove(s + hm.body.len, s + o + pl, (size_t)dl);
          o += cl, hm.body.len += (size_t)dl, hm.message.len += (size_t)dl;
          if (dl == 0)
            break;
        }
        ofs += (size_t)(n + o);
      } else { /* Normal, non-chunked data */
        size_t len = c->recv.len - ofs - (size_t)n;
        if (hm.body.len > len)
          break; /* Buffer more data */
        ofs += (size_t)n + hm.body.len;
      }

      if (c->is_accepted)
        c->is_resp = 1; /* Start generating response */
      ttg_event_call(c, TTG_EVENT_HTTP_MSG,
                     &hm); /* User handler can clear is_resp */
      if (c->is_accepted && !c->is_resp) {
        struct ttg_str* cc = ttg_http_get_header(&hm, "Connection");
        if (cc != NULL && ttg_str_casecmp(*cc, str("close")) == 0) {
          c->is_draining = 1; /* honor "Connection: close" */
          break;
        }
      }
    }
    if (ofs > 0)
      ttg_iobuf_del(&c->recv, 0, ofs); /* Delete processed data */
  }
  (void)ev_data;
}

struct ttg_conn* ttg_http_connect(struct ttg_mgr* mgr, const char* url,
                                  ttg_event_handler fn, void* fn_data) {
  return ttg_net_connect_svc(mgr, url, fn, fn_data, http_cb, NULL);
}

struct ttg_conn* ttg_http_listen(struct ttg_mgr* mgr, const char* url,
                                 ttg_event_handler fn, void* fn_data) {
  struct ttg_conn* c = ttg_net_listen(mgr, url, fn, fn_data);
  if (c != NULL)
    c->pfn = http_cb;
  return c;
}