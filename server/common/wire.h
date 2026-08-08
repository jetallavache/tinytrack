#ifndef TT_WIRE_H
#define TT_WIRE_H

#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>

static inline uint64_t htonll(uint64_t host) {
  if (htonl(1) == 1) {
    return host;
  }
  /* Little-Endian to Big-Endian */
  return ((uint64_t)htonl(host & 0xFFFFFFFF) << 32) | htonl(host >> 32);
}

static inline uint64_t ntohll(uint64_t net) {
  /* Symmetric operation */
  if (ntohl(1) == 1) {
    return net; /* Already Big-Endian */
  }
  return ((uint64_t)ntohl(net & 0xFFFFFFFF) << 32) | ntohl(net >> 32);
}

static inline void tt_wire_write_be64(uint8_t* buf, uint64_t val) {
  uint64_t net = htonll(val);
  memcpy(buf, &net, 8);
}

static inline void tt_wire_write_be32(uint8_t* buf, uint32_t val) {
  uint32_t net = htonl(val);
  memcpy(buf, &net, 4);
}

static inline void tt_wire_write_be16(uint8_t* buf, uint16_t val) {
  uint16_t net = htons(val);
  memcpy(buf, &net, 2);
}

static inline void tt_wire_write_u8(uint8_t* buf, uint8_t val) {
  *buf = val; /* 1-byte values are independent of the order */
}

static inline uint64_t tt_wire_read_be64(const uint8_t* buf) {
  uint64_t net;
  memcpy(&net, buf, 8);
  return ntohll(net);
}

static inline uint32_t tt_wire_read_be32(const uint8_t* buf) {
  uint32_t net;
  memcpy(&net, buf, 4);
  return ntohl(net);
}

static inline uint16_t tt_wire_read_be16(const uint8_t* buf) {
  uint16_t net;
  memcpy(&net, buf, 2);
  return ntohs(net);
}

static inline uint8_t tt_wire_read_u8(const uint8_t* buf) {
  return *buf;
}

#endif /* TT_WIRE_H */