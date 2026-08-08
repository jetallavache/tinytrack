#include "session.h"

#include <alloca.h>
#include <arpa/inet.h>
#include <endian.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common/log/log.h"
#include "common/proto/v1.h"
#include "common/proto/v2.h"
#include "common/ringbuf/layout.h"
#include "http.h"
#include "proto.h"
#include "str.h"
#include "ws.h"

#define tt_metrics tt_metrics_ex
#define tt_agg_metrics tt_agg_metrics_ex

#define WS_MARK 'W'

static struct ttg_reader* g_reader;
static char g_auth_token[128]; /* empty = auth disabled */
static uint32_t g_auth_timeout_ms;
static char g_cors_origins[512]; /* empty = CORS disabled */

void ttg_session_init(struct ttg_reader* reader) {
  g_reader = reader;
}

void ttg_session_set_auth(const char* token, uint32_t timeout_ms) {
  snprintf(g_auth_token, sizeof(g_auth_token), "%s", token ? token : "");
  g_auth_timeout_ms = timeout_ms > 0 ? timeout_ms : 5000;
}

void ttg_session_set_cors(const char* origins) {
  snprintf(g_cors_origins, sizeof(g_cors_origins), "%s",
           origins ? origins : "");
}

/*
 * Build CORS headers for a given request Origin.
 * Rules:
 *   - cors_origins empty - no CORS headers (disabled)
 *   - cors_origins == "*" - allow all (wildcard, no credentials)
 *   - otherwise - check if request Origin is in the comma-separated whitelist;
 *     if yes - reflect it back with Vary: Origin
 *
 * out must be at least 256 bytes.
 */
static void cors_headers_for(const char* req_origin, char* out,
                             size_t out_size) {
  out[0] = '\0';
  if (g_cors_origins[0] == '\0' || req_origin == NULL || req_origin[0] == '\0')
    return;

  if (strcmp(g_cors_origins, "*") == 0) {
    /* Wildcard: no credentials, no Authorization reflection */
    snprintf(out, out_size,
             "Access-Control-Allow-Origin: *\r\n"
             "Access-Control-Allow-Headers: Authorization\r\n");
    return;
  }

  /* Check whitelist: iterate comma-separated origins */
  char list[512];
  snprintf(list, sizeof(list), "%s", g_cors_origins);
  char* tok = list;
  char* end;
  while (tok && *tok) {
    /* trim leading spaces */
    while (*tok == ' ')
      tok++;
    end = strchr(tok, ',');
    if (end)
      *end = '\0';
    /* trim trailing spaces */
    size_t len = strlen(tok);
    while (len > 0 && tok[len - 1] == ' ')
      tok[--len] = '\0';

    if (strcmp(tok, req_origin) == 0) {
      snprintf(out, out_size,
               "Access-Control-Allow-Origin: %s\r\n"
               "Access-Control-Allow-Headers: Authorization\r\n"
               "Access-Control-Allow-Credentials: true\r\n"
               "Vary: Origin\r\n",
               req_origin);
      return;
    }
    tok = end ? end + 1 : NULL;
  }
  /* Origin not in whitelist — no CORS headers - browser blocks the request */
}

static void send_metrics(struct ttg_conn* c) {
  struct tt_metrics m;
  int ret;

  if (c->sub_level == RING_LEVEL_L1 || c->sub_level == 0) {
    ret = ttg_reader_get_latest(g_reader, &m);
  } else {
    ret = ttg_reader_get_history(g_reader, c->sub_level, &m, 1);
    if (ret == 1)
      ret = 0;
  }
  if (ret != 0)
    return;

  uint8_t buf[sizeof(struct tt_proto_header) + sizeof(m)];

  /* Проверить так ли это на самом деле! */
  uint8_t wire[sizeof(m)];
  tt_metrics_ex_serialize(&m, wire);
  size_t n =
      ttg_proto_build(buf, sizeof(buf), TT_PROTO_V1, PKT_METRICS,
                      (uint32_t)(m.timestamp / 1000), &wire, sizeof(wire));
  if (n > 0)
    ttg_ws_send(c, buf, n, TTG_WS_OP_BINARY);
}

static void send_history(struct ttg_conn* c,
                         const struct tt_proto_history_req* req) {
  uint16_t max = ntohs(req->max_count);
  if (max == 0 || max > TT_HISTORY_BATCH_MAX * 10)
    max = TT_HISTORY_BATCH_MAX * 10;

  int remaining = max;
  struct tt_metrics samples[TT_HISTORY_BATCH_MAX];
  int is_agg = (req->level == RING_LEVEL_L2 || req->level == RING_LEVEL_L3);

  while (remaining > 0) {
    int batch =
        remaining < TT_HISTORY_BATCH_MAX ? remaining : TT_HISTORY_BATCH_MAX;
    int got = ttg_reader_get_history(g_reader, req->level, samples, batch);
    if (got <= 0)
      break;

    remaining -= got;
    int is_last = (remaining <= 0 || got < batch);

    struct tt_proto_history_resp resp = {
        .level = req->level,
        .count = htons((uint16_t)got),
        .flags = (uint8_t)((is_last ? HISTORY_FLAG_LAST : 0) |
                           (is_agg ? HISTORY_FLAG_AGG : 0)),
    };

    if (is_agg) {
      /* L2/L3: compute tt_agg_metrics per sample and send */
      struct tt_agg_metrics agg_samples[TT_HISTORY_BATCH_MAX];
      for (int i = 0; i < got; i++) {
        /* Each L2/L3 entry is already an aggregated sample from writer.
         * We wrap it: avg=sample, min=sample, max=sample for single-entry
         * windows, or compute from the raw L1 window if available.
         * For now: use the stored sample as avg; min/max = avg (best effort
         * until writer stores tt_agg_metrics natively — roadmap §8). */
        agg_samples[i].avg = samples[i];
        agg_samples[i].min = samples[i];
        agg_samples[i].max = samples[i];
      }
      size_t agg_payload_size =
          sizeof(resp) + (size_t)got * sizeof(*agg_samples);
      uint8_t* payload = alloca(agg_payload_size);
      memcpy(payload, &resp, sizeof(resp));
      memcpy(payload + sizeof(resp), agg_samples,
             (size_t)got * sizeof(*agg_samples));

      uint8_t buf[sizeof(struct tt_proto_header) + 4096];
      size_t n = ttg_proto_build(buf, sizeof(buf), TT_PROTO_V2,
                                 PKT_HISTORY_RESP, (uint32_t)time(NULL),
                                 payload, (uint16_t)agg_payload_size);
      if (n > 0)
        ttg_ws_send(c, buf, n, TTG_WS_OP_BINARY);
    } else {
      /* L1: raw tt_metrics */
      size_t raw_payload_size = sizeof(resp) + (size_t)got * sizeof(*samples);
      uint8_t* payload = alloca(raw_payload_size);
      memcpy(payload, &resp, sizeof(resp));
      memcpy(payload + sizeof(resp), samples, (size_t)got * sizeof(*samples));

      uint8_t buf[sizeof(struct tt_proto_header) + 4096];
      size_t n = ttg_proto_build(buf, sizeof(buf), TT_PROTO_V2,
                                 PKT_HISTORY_RESP, (uint32_t)time(NULL),
                                 payload, (uint16_t)raw_payload_size);
      if (n > 0)
        ttg_ws_send(c, buf, n, TTG_WS_OP_BINARY);
    }
  }
}

static void send_stats(struct ttg_conn* c) {
  struct tt_proto_stats stats;
  ttg_reader_get_stats(g_reader, RING_LEVEL_L1, &stats.l1);
  ttg_reader_get_stats(g_reader, RING_LEVEL_L2, &stats.l2);
  ttg_reader_get_stats(g_reader, RING_LEVEL_L3, &stats.l3);

  uint8_t buf[sizeof(struct tt_proto_header) + sizeof(stats)];
  size_t n = ttg_proto_build(buf, sizeof(buf), TT_PROTO_V2, PKT_RING_STATS,
                             (uint32_t)time(NULL), &stats, sizeof(stats));
  if (n > 0)
    ttg_ws_send(c, buf, n, TTG_WS_OP_BINARY);
}

static void handle_subscribe(struct ttg_conn* c,
                             const struct tt_proto_subscribe* sub) {
  struct tt_proto_ack ack = {.cmd_type = PKT_SUBSCRIBE, .status = ACK_OK};

  if (sub->level < RING_LEVEL_L1 || sub->level > RING_LEVEL_L3) {
    ack.status = ACK_ERROR;
  } else {
    c->sub_level = sub->level;
    uint32_t ms = ntohl(sub->interval_ms);
    if (ms >= 1000 && ms <= 60000)
      c->update_interval_ms = ms;
    tt_log_info("Client subscribed to level %u @ %u ms", c->sub_level,
                c->update_interval_ms);
  }

  uint8_t buf[sizeof(struct tt_proto_header) + sizeof(ack)];
  size_t n = ttg_proto_build(buf, sizeof(buf), TT_PROTO_V2, PKT_ACK,
                             (uint32_t)time(NULL), &ack, sizeof(ack));
  if (n > 0)
    ttg_ws_send(c, buf, n, TTG_WS_OP_BINARY);
}

static void send_sysinfo(struct ttg_conn* c) {
  struct tt_proto_sysinfo info;
  ttg_reader_get_sysinfo(g_reader, &info);

  uint8_t buf[sizeof(struct tt_proto_header) + sizeof(info)];
  size_t n = ttg_proto_build(buf, sizeof(buf), TT_PROTO_V2, PKT_SYS_INFO,
                             (uint32_t)time(NULL), &info, sizeof(info));
  if (n > 0)
    ttg_ws_send(c, buf, n, TTG_WS_OP_BINARY);
}

static void handle_cmd(struct ttg_conn* c, const struct tt_proto_cmd* cmd) {
  struct tt_proto_ack ack = {.cmd_type = cmd->cmd_type, .status = ACK_OK};

  if (cmd->cmd_type == CMD_SET_INTERVAL) {
    uint32_t ms = ntohl(cmd->interval_ms);
    if (ms >= 1000 && ms <= 60000) {
      c->update_interval_ms = ms;
      tt_log_info("Client interval set to %u ms", ms);
    } else {
      ack.status = ACK_ERROR;
    }
  } else if (cmd->cmd_type == CMD_SET_ALERTS) {
    tt_log_info("Client alerts_enabled=%u", cmd->alerts_enabled);
  } else if (cmd->cmd_type == CMD_GET_SNAPSHOT) {
    send_metrics(c);
  } else if (cmd->cmd_type == CMD_GET_RING_STATS) {
    send_stats(c);
  } else if (cmd->cmd_type == CMD_GET_SYS_INFO) {
    send_sysinfo(c);
  } else if (cmd->cmd_type == CMD_START) {
    c->streaming_paused = 0;
    tt_log_info("Client streaming resumed");
  } else if (cmd->cmd_type == CMD_STOP) {
    c->streaming_paused = 1;
    tt_log_info("Client streaming paused");
  } else {
    ack.status = ACK_ERROR;
  }

  uint8_t buf[sizeof(struct tt_proto_header) + sizeof(ack)];
  size_t n = ttg_proto_build(buf, sizeof(buf), TT_PROTO_V1, PKT_ACK,
                             (uint32_t)time(NULL), &ack, sizeof(ack));
  if (n > 0)
    ttg_ws_send(c, buf, n, TTG_WS_OP_BINARY);
}

static void on_ws_message(struct ttg_conn* c, const void* data, size_t len) {
  if (len < sizeof(struct tt_proto_header))
    return;

  const struct tt_proto_header* hdr = (const struct tt_proto_header*)data;
  if (ttg_proto_validate(hdr) != 0)
    return;

  const uint8_t* payload = (const uint8_t*)data + sizeof(*hdr);
  size_t payload_len = len - sizeof(*hdr);

  /* Handle CMD_AUTH before any auth check */
  if (hdr->type == PKT_CMD && payload_len >= 1 && payload[0] == CMD_AUTH) {
    struct tt_proto_ack ack = {.cmd_type = CMD_AUTH, .status = ACK_ERROR};

    if (payload_len >= 1 + sizeof(struct tt_proto_auth)) {
      const struct tt_proto_auth* auth =
          (const struct tt_proto_auth*)(payload + 1);
      size_t tlen = strnlen(auth->token, sizeof(auth->token));
      size_t expected = strlen(g_auth_token);
      if (tlen == expected && strncmp(auth->token, g_auth_token, tlen) == 0) {
        c->is_authed = 1;
        c->auth_deadline = 0;
        ack.status = ACK_OK;
        tt_log_info("%lu CMD_AUTH accepted", c->id);
      } else {
        ack.status = ACK_AUTH_FAIL;
        tt_log_info("%lu CMD_AUTH rejected: wrong token", c->id);
      }
    }

    uint8_t buf[sizeof(struct tt_proto_header) + sizeof(ack)];
    size_t n = ttg_proto_build(buf, sizeof(buf), TT_PROTO_V2, PKT_ACK,
                               (uint32_t)time(NULL), &ack, sizeof(ack));
    if (n > 0)
      ttg_ws_send(c, buf, n, TTG_WS_OP_BINARY);

    if (!c->is_authed) {
      c->is_draining = 1; /* close after sending ACK_AUTH_FAIL */
      return;
    }

    /* Auth succeeded — send PKT_CONFIG now */
    struct tt_proto_config cfg = {
        .interval_ms = htonl(c->update_interval_ms),
        .alerts_enabled = 0,
    };
    uint8_t cbuf[sizeof(struct tt_proto_header) + sizeof(cfg)];
    size_t cn = ttg_proto_build(cbuf, sizeof(cbuf), TT_PROTO_V1, PKT_CONFIG,
                                (uint32_t)time(NULL), &cfg, sizeof(cfg));
    if (cn > 0)
      ttg_ws_send(c, cbuf, cn, TTG_WS_OP_BINARY);
    return;
  }

  /* Reject all other messages from unauthenticated clients */
  if (!c->is_authed) {
    tt_log_info("%lu message before auth, closing", c->id);
    c->is_draining = 1;
    return;
  }

  if (hdr->type == PKT_HISTORY_REQ) {
    if (payload_len >= sizeof(struct tt_proto_history_req))
      send_history(c, (const struct tt_proto_history_req*)payload);
  } else if (hdr->type == PKT_SUBSCRIBE) {
    if (payload_len >= sizeof(struct tt_proto_subscribe))
      handle_subscribe(c, (const struct tt_proto_subscribe*)payload);
  } else if (hdr->type == PKT_CMD) {
    if (payload_len >= sizeof(struct tt_proto_cmd))
      handle_cmd(c, (const struct tt_proto_cmd*)payload);
  }
}

static void on_ws_open(struct ttg_conn* c, struct ttg_http_message* hm) {
  c->data[0] = WS_MARK;
  c->update_interval_ms = 1000;
  c->last_update_time = 0;
  c->sub_level = RING_LEVEL_L1;
  c->streaming_paused = 0;

  /* Authentication: check Bearer token or schedule CMD_AUTH challenge */
  if (g_auth_token[0] == '\0') {
    c->is_authed = 1; /* auth disabled */
  } else {
    c->is_authed = 0;
    /* Check Authorization: Bearer <token> header */
    struct ttg_str* auth_hdr = ttg_http_get_header(hm, "Authorization");
    if (auth_hdr != NULL) {
      const char* prefix = "Bearer ";
      size_t plen = 7;
      if (auth_hdr->len > plen && strncmp(auth_hdr->buf, prefix, plen) == 0) {
        size_t tlen = auth_hdr->len - plen;
        size_t expected = strlen(g_auth_token);
        if (tlen == expected &&
            strncmp(auth_hdr->buf + plen, g_auth_token, tlen) == 0) {
          c->is_authed = 1;
        }
      }
    }
    if (!c->is_authed) {
      /* Send PKT_AUTH_REQ challenge and set deadline */
      uint8_t buf[sizeof(struct tt_proto_header)];
      size_t n = ttg_proto_build(buf, sizeof(buf), TT_PROTO_V2, PKT_AUTH_REQ,
                                 (uint32_t)time(NULL), NULL, 0);
      if (n > 0)
        ttg_ws_send(c, buf, n, TTG_WS_OP_BINARY);
      c->auth_deadline = time(NULL) + (time_t)(g_auth_timeout_ms / 1000);
      tt_log_info("%lu WS auth required, deadline in %u ms", c->id,
                  g_auth_timeout_ms);
      return; /* Don't send PKT_CONFIG until authed */
    }
  }

  tt_log_info("WebSocket client connected (authed)");

  struct tt_proto_config cfg = {
      .interval_ms = htonl(c->update_interval_ms),
      .alerts_enabled = 0,
  };
  uint8_t buf[sizeof(struct tt_proto_header) + sizeof(cfg)];
  size_t n = ttg_proto_build(buf, sizeof(buf), TT_PROTO_V1, PKT_CONFIG,
                             (uint32_t)time(NULL), &cfg, sizeof(cfg));
  if (n > 0)
    ttg_ws_send(c, buf, n, TTG_WS_OP_BINARY);
}

/* ---------------------------------------------------------------------------
 * Format helpers
 * ------------------------------------------------------------------------- */

/* Detect requested format from Accept header or ?format= query param.
 * Returns: "json" | "csv" | "xml" | "prometheus" */
static const char* detect_format(struct ttg_http_message* hm) {
  /* 1. ?format= query param takes priority */
  if (hm->query.len > 0) {
    char q[64] = "";
    size_t n = hm->query.len < sizeof(q) - 1 ? hm->query.len : sizeof(q) - 1;
    memcpy(q, hm->query.buf, n);
    q[n] = '\0';
    if (strstr(q, "format=csv"))
      return "csv";
    if (strstr(q, "format=xml"))
      return "xml";
    if (strstr(q, "format=prometheus"))
      return "prometheus";
    if (strstr(q, "format=json"))
      return "json";
  }
  /* 2. Accept header */
  struct ttg_str* accept = ttg_http_get_header(hm, "Accept");
  if (accept && accept->len > 0) {
    if (ttg_str_match(*accept, str("text/csv"), NULL))
      return "csv";
    if (ttg_str_match(*accept, str("text/xml"), NULL))
      return "xml";
    if (ttg_str_match(*accept, str("application/xml"), NULL))
      return "xml";
    if (ttg_str_match(*accept, str("text/plain"), NULL))
      return "prometheus";
    if (ttg_str_match(*accept, str("application/openmetrics"), NULL))
      return "prometheus";
  }
  return "json"; /* default */
}

static void reply_metrics_json(struct ttg_conn* c, const struct tt_metrics* m,
                               const char* cors) {
  char buf[512];
  snprintf(buf, sizeof(buf),
           "{"
           "\"timestamp\":%llu,"
           "\"cpu\":%.4f,"
           "\"mem\":%.4f,"
           "\"disk\":%.4f,"
           "\"disk_total\":%llu,"
           "\"disk_free\":%llu,"
           "\"load\":{\"1m\":%.2f,\"5m\":%.2f,\"15m\":%.2f},"
           "\"procs\":{\"running\":%u,\"total\":%u},"
           "\"net\":{\"rx\":%u,\"tx\":%u}"
           "}",
           (unsigned long long)m->timestamp, m->cpu_usage_pct / 10000.0,
           m->mem_usage_pct / 10000.0,
           ((m->du_total_bytes - m->du_free_bytes) * 100 / m->du_total_bytes) /
               10000.0,
           (unsigned long long)m->du_total_bytes,
           (unsigned long long)m->du_free_bytes, m->load_1min / 100.0,
           m->load_5min / 100.0, m->load_15min / 100.0, m->nr_running,
           m->nr_total, m->net_rx_bytes, m->net_tx_bytes);
  char hdrs[320];
  snprintf(hdrs, sizeof(hdrs), "Content-Type: application/json\r\n%s", cors);
  ttg_http_reply(c, 200, hdrs, "%s", buf);
}

static void reply_metrics_csv(struct ttg_conn* c, const struct tt_metrics* m,
                              const char* cors) {
  char buf[512];
  snprintf(buf, sizeof(buf),
           "timestamp,cpu,mem,disk,disk_total,disk_free,"
           "load_1m,load_5m,load_15m,procs_running,procs_total,net_rx_bytes,"
           "net_tx_bytes\n"
           "%llu,%.4f,%.4f,%.4f,%llu,%llu,%.2f,%.2f,%.2f,%u,%u,%u,%u\n",
           (unsigned long long)m->timestamp, m->cpu_usage_pct / 10000.0,
           m->mem_usage_pct / 10000.0,
           ((m->du_total_bytes - m->du_free_bytes) * 100 / m->du_total_bytes) /
               10000.0,
           (unsigned long long)m->du_total_bytes,
           (unsigned long long)m->du_free_bytes, m->load_1min / 100.0,
           m->load_5min / 100.0, m->load_15min / 100.0, m->nr_running,
           m->nr_total, m->net_rx_bytes, m->net_tx_bytes);
  char hdrs[320];
  snprintf(hdrs, sizeof(hdrs), "Content-Type: text/csv\r\n%s", cors);
  ttg_http_reply(c, 200, hdrs, "%s", buf);
}

static void reply_metrics_xml(struct ttg_conn* c, const struct tt_metrics* m,
                              const char* cors) {
  char buf[768];
  snprintf(buf, sizeof(buf),
           "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<metrics timestamp=\"%llu\">\n"
           "  <cpu>%.4f</cpu>\n"
           "  <mem>%.4f</mem>\n"
           "  <disk usage=\"%.4f\" total=\"%llu\" free=\"%llu\"/>\n"
           "  <load m1=\"%.2f\" m5=\"%.2f\" m15=\"%.2f\"/>\n"
           "  <procs running=\"%u\" total=\"%u\"/>\n"
           "  <net rx=\"%u\" tx=\"%u\"/>\n"
           "</metrics>\n",
           (unsigned long long)m->timestamp, m->cpu_usage_pct / 10000.0,
           m->mem_usage_pct / 10000.0,
           ((m->du_total_bytes - m->du_free_bytes) * 100 / m->du_total_bytes) /
               10000.0,
           (unsigned long long)m->du_total_bytes,
           (unsigned long long)m->du_free_bytes, m->load_1min / 100.0,
           m->load_5min / 100.0, m->load_15min / 100.0, m->nr_running,
           m->nr_total, m->net_rx_bytes, m->net_tx_bytes);
  char hdrs[320];
  snprintf(hdrs, sizeof(hdrs), "Content-Type: application/xml\r\n%s", cors);
  ttg_http_reply(c, 200, hdrs, "%s", buf);
}

static void reply_metrics_prometheus(struct ttg_conn* c,
                                     const struct tt_metrics* m,
                                     const char* cors) {
  char buf[2048];
  snprintf(buf, sizeof(buf),
           "# HELP tinytrack_cpu_usage_pct_ratio CPU usage (0..1)\n"
           "# TYPE tinytrack_cpu_usage_pct_ratio gauge\n"
           "tinytrack_cpu_usage_pct_ratio %.4f\n"
           "# HELP tinytrack_memory_usage_ratio Memory usage (0..1)\n"
           "# TYPE tinytrack_memory_usage_ratio gauge\n"
           "tinytrack_memory_usage_ratio %.4f\n"
           "# HELP tinytrack_disk_usage_ratio Disk usage (0..1)\n"
           "# TYPE tinytrack_disk_usage_ratio gauge\n"
           "tinytrack_disk_usage_ratio %.4f\n"
           "# HELP tinytrack_disk_total_bytes Total disk space in bytes\n"
           "# TYPE tinytrack_disk_total_bytes gauge\n"
           "tinytrack_disk_total_bytes %llu\n"
           "# HELP tinytrack_disk_free_bytes Free disk space in bytes\n"
           "# TYPE tinytrack_disk_free_bytes gauge\n"
           "tinytrack_disk_free_bytes %llu\n"
           "# HELP tinytrack_load_average Load average\n"
           "# TYPE tinytrack_load_average gauge\n"
           "tinytrack_load_average{interval=\"1m\"} %.2f\n"
           "tinytrack_load_average{interval=\"5m\"} %.2f\n"
           "tinytrack_load_average{interval=\"15m\"} %.2f\n"
           "# HELP tinytrack_processes_running Running processes\n"
           "# TYPE tinytrack_processes_running gauge\n"
           "tinytrack_processes_running %u\n"
           "# HELP tinytrack_processes_total Total processes\n"
           "# TYPE tinytrack_processes_total gauge\n"
           "tinytrack_processes_total %u\n"
           "# HELP tinytrack_network_receive_bytes_total Network RX bytes/s\n"
           "# TYPE tinytrack_network_receive_bytes_total counter\n"
           "tinytrack_network_receive_bytes_total %u\n"
           "# HELP tinytrack_network_transmit_bytes_total Network TX bytes/s\n"
           "# TYPE tinytrack_network_transmit_bytes_total counter\n"
           "tinytrack_network_transmit_bytes_total %u\n"
           "# HELP tinytrack_scrape_timestamp_ms Timestamp of last sample ms\n"
           "# TYPE tinytrack_scrape_timestamp_ms gauge\n"
           "tinytrack_scrape_timestamp_ms %llu\n",
           m->cpu_usage_pct / 10000.0, m->mem_usage_pct / 10000.0,
           ((m->du_total_bytes - m->du_free_bytes) * 100 / m->du_total_bytes) /
               10000.0,
           (unsigned long long)m->du_total_bytes,
           (unsigned long long)m->du_free_bytes, m->load_1min / 100.0,
           m->load_5min / 100.0, m->load_15min / 100.0, m->nr_running,
           m->nr_total, m->net_rx_bytes, m->net_tx_bytes,
           (unsigned long long)m->timestamp);
  char hdrs[320];
  snprintf(hdrs, sizeof(hdrs), "Content-Type: text/plain; version=0.0.4\r\n%s",
           cors);
  ttg_http_reply(c, 200, hdrs, "%s", buf);
}

/**
 * Calculate rate (per 1 sec!) of change rx/tx,
 * but we just need to get the raw data and store it in this form in the tsdb.
 */

/*
function - irate()

 time_t now = time(NULL);
time_t elapsed = now - st->net_time_prev;

if (elapsed > 0 && st->net_time_prev > 0) {
  if (curr.rx_bytes >= st->pr_net_prev.rx_bytes)
    *rx = (curr.rx_bytes - st->pr_net_prev.rx_bytes) / elapsed;
  else
    *rx = 0; counter reset / wrap
  if (curr.tx_bytes >= st->pr_net_prev.tx_bytes)
    *tx = (curr.tx_bytes - st->pr_net_prev.tx_bytes) / elapsed;
  else
    *tx = 0;
} else {
  *rx = *tx = 0;
}

st->pr_net_prev = curr;
st->net_time_prev = now;
 */

static void reply_metrics(struct ttg_conn* c, struct ttg_http_message* hm,
                          const char* cors) {
  struct tt_metrics m;
  if (ttg_reader_get_latest(g_reader, &m) != 0) {
    ttg_http_reply(c, 503, cors, "{\"error\":\"no data\"}");
    return;
  }
  const char* fmt = detect_format(hm);
  if (strcmp(fmt, "csv") == 0)
    reply_metrics_csv(c, &m, cors);
  else if (strcmp(fmt, "xml") == 0)
    reply_metrics_xml(c, &m, cors);
  else if (strcmp(fmt, "prometheus") == 0)
    reply_metrics_prometheus(c, &m, cors);
  else
    reply_metrics_json(c, &m, cors);
}

static void reply_sysinfo_json(struct ttg_conn* c, const char* cors) {
  struct tt_proto_sysinfo info;
  ttg_reader_get_sysinfo(g_reader, &info);
  char buf[512];
  snprintf(buf, sizeof(buf),
           "{\"hostname\":\"%s\",\"os\":\"%s\",\"uptime\":%llu,"
           "\"slots\":{\"l1\":%u,\"l2\":%u,\"l3\":%u},"
           "\"interval_ms\":%u}",
           info.hostname, info.os_type,
           (unsigned long long)be64toh(info.uptime_sec), ntohl(info.slots_l1),
           ntohl(info.slots_l2), ntohl(info.slots_l3), ntohl(info.interval_ms));
  char hdrs[320];
  snprintf(hdrs, sizeof(hdrs), "Content-Type: application/json\r\n%s", cors);
  ttg_http_reply(c, 200, hdrs, "%s", buf);
}

static void reply_sysinfo(struct ttg_conn* c, struct ttg_http_message* hm,
                          const char* cors) {
  struct tt_proto_sysinfo info;
  ttg_reader_get_sysinfo(g_reader, &info);
  uint64_t uptime = be64toh(info.uptime_sec);
  uint32_t l1 = ntohl(info.slots_l1), l2 = ntohl(info.slots_l2),
           l3 = ntohl(info.slots_l3), iv = ntohl(info.interval_ms);

  const char* fmt = detect_format(hm);
  char buf[768];
  char hdrs[320];

  if (strcmp(fmt, "csv") == 0) {
    snprintf(buf, sizeof(buf),
             "hostname,os,uptime,slots_l1,slots_l2,slots_l3,interval_ms\n"
             "%s,%s,%llu,%u,%u,%u,%u\n",
             info.hostname, info.os_type, (unsigned long long)uptime, l1, l2,
             l3, iv);
    snprintf(hdrs, sizeof(hdrs), "Content-Type: text/csv\r\n%s", cors);
  } else if (strcmp(fmt, "xml") == 0) {
    snprintf(buf, sizeof(buf),
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<sysinfo>\n"
             "  <hostname>%s</hostname>\n"
             "  <os>%s</os>\n"
             "  <uptime>%llu</uptime>\n"
             "  <slots l1=\"%u\" l2=\"%u\" l3=\"%u\"/>\n"
             "  <interval_ms>%u</interval_ms>\n"
             "</sysinfo>\n",
             info.hostname, info.os_type, (unsigned long long)uptime, l1, l2,
             l3, iv);
    snprintf(hdrs, sizeof(hdrs), "Content-Type: application/xml\r\n%s", cors);
  } else {
    snprintf(buf, sizeof(buf),
             "{\"hostname\":\"%s\",\"os\":\"%s\",\"uptime\":%llu,"
             "\"slots\":{\"l1\":%u,\"l2\":%u,\"l3\":%u},\"interval_ms\":%u}",
             info.hostname, info.os_type, (unsigned long long)uptime, l1, l2,
             l3, iv);
    snprintf(hdrs, sizeof(hdrs), "Content-Type: application/json\r\n%s", cors);
  }
  ttg_http_reply(c, 200, hdrs, "%s", buf);
}

/* ---------------------------------------------------------------------------
 * HTTP router  /v1/*
 * ------------------------------------------------------------------------- */
static void on_http(struct ttg_conn* c, struct ttg_http_message* hm) {
  /* Build CORS headers */
  char cors[256] = "";
  struct ttg_str* origin_hdr = ttg_http_get_header(hm, "Origin");
  char origin_buf[256] = "";
  if (origin_hdr && origin_hdr->len < sizeof(origin_buf)) {
    memcpy(origin_buf, origin_hdr->buf, origin_hdr->len);
    origin_buf[origin_hdr->len] = '\0';
  }
  cors_headers_for(origin_buf, cors, sizeof(cors));

  /* CORS preflight */
  if (ttg_str_casecmp(hm->method, str("OPTIONS")) == 0) {
    ttg_http_reply(
        c, 204,
        cors[0] ? "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n" : "",
        "%s", cors);
    return;
  }

  /* ── HTTP Authentication ───────────────────────────────────────── */
  /* /v1/status is public (health check for load balancers).
   * All other /v1/* endpoints require auth when auth_token is configured. */
  if (g_auth_token[0] && !ttg_str_match(hm->uri, str("/v1/status"), NULL) &&
      !ttg_str_match(hm->uri, str("/websocket"), NULL) &&
      !ttg_str_match(hm->uri, str("/v1/stream"), NULL)) {
    struct ttg_str* auth_hdr = ttg_http_get_header(hm, "Authorization");
    bool authed = false;
    if (auth_hdr && auth_hdr->len > 7) {
      const char* prefix = "Bearer ";
      if (strncmp(auth_hdr->buf, prefix, 7) == 0) {
        size_t tlen = auth_hdr->len - 7;
        size_t expected = strlen(g_auth_token);
        authed = (tlen == expected &&
                  strncmp(auth_hdr->buf + 7, g_auth_token, tlen) == 0);
      }
    }
    if (!authed) {
      ttg_http_reply(c, 401, "WWW-Authenticate: Bearer realm=\"tinytrack\"\r\n",
                     "{\"error\":\"unauthorized\"}");
      return;
    }
  }

  bool is_get = ttg_str_casecmp(hm->method, str("GET")) == 0;
  bool is_post = ttg_str_casecmp(hm->method, str("POST")) == 0;

  /* ── WebSocket upgrade ─────────────────────────────────────────── */
  /* /v1/stream  (new canonical)  +  /websocket  (legacy compat) */
  if (ttg_str_match(hm->uri, str("/v1/stream"), NULL) ||
      ttg_str_match(hm->uri, str("/websocket"), NULL)) {
    ttg_ws_upgrade(c, hm, NULL);
    return;
  }

  /* ── GET /v1/metrics ───────────────────────────────────────────── */
  /* Content negotiation: Accept header or ?format=json|csv|xml|prometheus */
  if (is_get && ttg_str_match(hm->uri, str("/v1/metrics"), NULL)) {
    reply_metrics(c, hm, cors);
    return;
  }

  /* ── GET /v1/sysinfo ───────────────────────────────────────────── */
  if (is_get && ttg_str_match(hm->uri, str("/v1/sysinfo"), NULL)) {
    reply_sysinfo(c, hm, cors);
    return;
  }

  /* ── GET /v1/status ────────────────────────────────────────────── */
  if (is_get && ttg_str_match(hm->uri, str("/v1/status"), NULL)) {
    struct tt_metrics m;
    int ok = ttg_reader_get_latest(g_reader, &m) == 0;
    const char* fmt = detect_format(hm);
    char hdrs[320];
    if (strcmp(fmt, "csv") == 0) {
      snprintf(hdrs, sizeof(hdrs), "Content-Type: text/csv\r\n%s", cors);
      ttg_http_reply(c, ok ? 200 : 503, hdrs, "status\n%s\n",
                     ok ? "ok" : "no_data");
    } else if (strcmp(fmt, "xml") == 0) {
      snprintf(hdrs, sizeof(hdrs), "Content-Type: application/xml\r\n%s", cors);
      ttg_http_reply(c, ok ? 200 : 503, hdrs,
                     "<?xml version=\"1.0\"?><status>%s</status>\n",
                     ok ? "ok" : "no_data");
    } else {
      snprintf(hdrs, sizeof(hdrs), "Content-Type: application/json\r\n%s",
               cors);
      ttg_http_reply(c, ok ? 200 : 503, hdrs, "{\"status\":\"%s\"}",
                     ok ? "ok" : "no_data");
    }
    return;
  }

  /* ── POST /v1/stream/pause ─────────────────────────────────────── */
  if (is_post && ttg_str_match(hm->uri, str("/v1/stream/pause"), NULL)) {
    for (struct ttg_conn* p = c->mgr->conns; p; p = p->next)
      if (p->data[0] == WS_MARK)
        p->streaming_paused = 1;
    ttg_http_reply(c, 200, cors, "{\"status\":\"paused\"}");
    return;
  }

  /* ── POST /v1/stream/resume ────────────────────────────────────── */
  if (is_post && ttg_str_match(hm->uri, str("/v1/stream/resume"), NULL)) {
    for (struct ttg_conn* p = c->mgr->conns; p; p = p->next)
      if (p->data[0] == WS_MARK)
        p->streaming_paused = 0;
    ttg_http_reply(c, 200, cors, "{\"status\":\"resumed\"}");
    return;
  }

  /* ── Legacy: /metrics - Prometheus, /api/metrics/live - JSON ──── */
  if (is_get && ttg_str_match(hm->uri, str("/metrics"), NULL)) {
    struct tt_metrics m;
    if (ttg_reader_get_latest(g_reader, &m) == 0)
      reply_metrics_prometheus(c, &m, cors);
    else
      ttg_http_reply(c, 503, cors, "# No data available\n");
    return;
  }
  if (is_get && ttg_str_match(hm->uri, str("/api/metrics/live"), NULL)) {
    struct tt_metrics m;
    if (ttg_reader_get_latest(g_reader, &m) == 0)
      reply_metrics_json(c, &m, cors);
    else
      ttg_http_reply(c, 503, cors, "{\"error\":\"no data\"}");
    return;
  }

  ttg_http_reply(c, 404, cors, "{\"error\":\"not found\"}");
}

void ttg_session_event_fn(struct ttg_conn* c, int ev, void* ev_data) {
  if (ev == TTG_EVENT_WS_OPEN) {
    on_ws_open(c, (struct ttg_http_message*)ev_data);
  } else if (ev == TTG_EVENT_HTTP_MSG) {
    on_http(c, (struct ttg_http_message*)ev_data);
  } else if (ev == TTG_EVENT_WS_MSG) {
    struct ttg_ws_message* wm = (struct ttg_ws_message*)ev_data;
    on_ws_message(c, wm->data.buf, wm->data.len);
  }
}

static void send_alert(struct ttg_conn* c, uint8_t level, const char* msg) {
  struct tt_proto_alert alert;
  alert.level = level;
  snprintf(alert.message, sizeof(alert.message), "%s", msg);

  uint8_t buf[sizeof(struct tt_proto_header) + sizeof(alert)];
  size_t n = ttg_proto_build(buf, sizeof(buf), TT_PROTO_V1, PKT_ALERT,
                             (uint32_t)time(NULL), &alert, sizeof(alert));
  if (n > 0)
    ttg_ws_send(c, buf, n, TTG_WS_OP_BINARY);
}

void ttg_session_timer_fn(void* arg) {
  struct ttg_mgr* mgr = (struct ttg_mgr*)arg;
  time_t now = time(NULL);

  /* ── Watchdog: detect tinytd death ─────────────────────────────── */
  static int daemon_dead = 0; /* 0=ok, 1=warned, 2=disconnecting */
  static time_t last_watchdog = 0;

  if (now - last_watchdog >= 2) { /* check every 2 seconds */
    last_watchdog = now;
    const struct ttr_header* hdr =
        (const struct ttr_header*)g_reader->ring.addr;

    if (hdr && hdr->last_update_ts > 0) {
      uint64_t now_ms = (uint64_t)now * 1000;
      uint32_t interval = hdr->interval_ms > 0 ? hdr->interval_ms : 1000;
      uint64_t age_ms =
          now_ms > hdr->last_update_ts ? now_ms - hdr->last_update_ts : 0;

      if (age_ms > (uint64_t)interval * 10 && daemon_dead < 2) {
        /* Hard threshold: disconnect all clients */
        daemon_dead = 2;
        tt_log_err(
            "Daemon     tinytd stopped — no update for %llums, disconnecting "
            "clients",
            (unsigned long long)age_ms);
        tt_log_err(
            "           See "
            "https://tinytrack.dev/docs/troubleshooting#daemon-stopped");
        for (struct ttg_conn* c = mgr->conns; c != NULL; c = c->next) {
          if (c->data[0] != WS_MARK || !c->is_authed)
            continue;
          send_alert(c, ALERT_CRITICAL,
                     "tinytd daemon stopped — metrics unavailable");
          c->is_draining = 1;
        }
      } else if (age_ms > (uint64_t)interval * 3 && daemon_dead < 1) {
        /* Soft threshold: warn clients */
        daemon_dead = 1;
        tt_log_warning("Daemon     tinytd unresponsive — no update for %llums",
                       (unsigned long long)age_ms);
        tt_log_warning(
            "           See "
            "https://tinytrack.dev/docs/troubleshooting#daemon-stopped");
        for (struct ttg_conn* c = mgr->conns; c != NULL; c = c->next) {
          if (c->data[0] != WS_MARK || !c->is_authed)
            continue;
          send_alert(c, ALERT_WARNING,
                     "tinytd daemon unresponsive — metrics may be stale");
        }
      } else if (age_ms <= (uint64_t)interval * 3 && daemon_dead > 0) {
        /* Daemon recovered */
        daemon_dead = 0;
        tt_log_notice("Daemon     tinytd recovered — metrics stream resumed");
      }
    }
  }

  /* ── Per-connection work ─────────────────────────────────────────── */
  for (struct ttg_conn* c = mgr->conns; c != NULL; c = c->next) {
    if (c->data[0] != WS_MARK)
      continue;

    /* Auth deadline: close if CMD_AUTH not received in time */
    if (!c->is_authed && c->auth_deadline > 0 && now >= c->auth_deadline) {
      tt_log_info("%lu auth timeout, closing", c->id);
      c->is_draining = 1;
      continue;
    }

    if (c->streaming_paused || !c->is_authed)
      continue;
    if (now - c->last_update_time >= (time_t)(c->update_interval_ms / 1000)) {
      send_metrics(c);
      c->last_update_time = now;
    }
  }
}
