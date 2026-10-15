#include "tls_redirect.h"
#include "tls_cert.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/version.h>
#include <mbedtls/ssl_ciphersuites.h>

#include <stdarg.h>

static const int DEBUG_MAX = 24;
static String debugRing[DEBUG_MAX];
static int debugCount = 0;
static String gCipherInfo;

void portalDebug(const char* fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    debugRing[debugCount % DEBUG_MAX] = String(buf);
    debugCount++;
    Serial.println(buf);
}

String getPortalDebugLog() {
    String out;
    if (gCipherInfo.length() > 0) {
        out += gCipherInfo;
        out += "\n";
    }
    int n = debugCount < DEBUG_MAX ? debugCount : DEBUG_MAX;
    for (int i = debugCount - n; i < debugCount; i++) {
        out += debugRing[i % DEBUG_MAX];
        out += "\n";
    }
    return out;
}

static WiFiServer tlsServer(443);

static uint32_t connStartMs = 0;
static bool firstDataLogged = false;
static bool firstSendLogged = false;

static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;
static mbedtls_x509_crt srvcert;
static mbedtls_pk_context pkey;
static bool tlsInitDone = false;

static int netRecv(void* ctx, unsigned char* buf, size_t len) {
    WiFiClient* c = static_cast<WiFiClient*>(ctx);
    uint32_t start = millis();
    while (c->available() == 0) {
        if (!c->connected()) {
            portalDebug("[tls] recv: peer disconnected");
            return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
        }
        if (millis() - start > 5000) {
            portalDebug("[tls] recv: timeout");
            return MBEDTLS_ERR_SSL_TIMEOUT;
        }
        delay(1);
    }
    int r = c->read(buf, len);
    if (r > 0 && !firstDataLogged) {
        firstDataLogged = true;
        portalDebug("[tls] first data after %lums, bytes=%02X %02X %02X",
                    millis() - connStartMs, buf[0], buf[1], buf[2]);
    }
    if (r <= 0) {
        portalDebug("[tls] recv: read failed r=%d", r);
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return r;
}

static int netSend(void* ctx, const unsigned char* buf, size_t len) {
    WiFiClient* c = static_cast<WiFiClient*>(ctx);
    size_t written = 0;
    while (written < len) {
        if (!firstSendLogged) {
            firstSendLogged = true;
            portalDebug("[tls] first send after %lums, byte=%02X",
                        millis() - connStartMs, buf[0]);
        }
        int w = c->write(buf + written, len - written);
        if (w < 0) {
            portalDebug("[tls] send: write error %d", w);
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        if (w == 0) {
            portalDebug("[tls] send: write returned 0 (peer closed?)");
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        written += w;
    }
    return written;
}

bool TLSRedirectServer::init() {
    if (tlsInitDone) return true;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_x509_crt_init(&srvcert);
    mbedtls_pk_init(&pkey);

    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, nullptr, 0);

    if (mbedtls_x509_crt_parse(&srvcert,
                               reinterpret_cast<const unsigned char*>(SERVER_CERT_PEM),
                               strlen(SERVER_CERT_PEM) + 1) != 0) {
        return false;
    }

    if (mbedtls_pk_parse_key(&pkey,
                             reinterpret_cast<const unsigned char*>(SERVER_KEY_PEM),
                             strlen(SERVER_KEY_PEM) + 1, nullptr, 0) != 0) {
        return false;
    }

    tlsInitDone = true;

    portalDebug("[tls] mbedtls %s", MBEDTLS_VERSION_STRING);
    const int* ciphers = mbedtls_ssl_list_ciphersuites();
    String list;
    for (int i = 0; ciphers[i] != 0; i++) {
        if (i > 0) list += ", ";
        list += String(mbedtls_ssl_get_ciphersuite_name(ciphers[i]));
    }
    gCipherInfo = "[tls] mbedtls " + String(MBEDTLS_VERSION_STRING) + " | ciphers: " + list;
    portalDebug("%s", gCipherInfo.c_str());
    return true;
}

void TLSRedirectServer::begin() {
    if (!init()) return;
    if (_running) return;
    tlsServer.begin();
    _running = true;
}

void TLSRedirectServer::stop() {
    if (!_running) return;
    tlsServer.stop();
    _running = false;
}

void TLSRedirectServer::handle() {
    if (!_running) return;

    WiFiClient client = tlsServer.accept();
    if (!client) return;

    portalDebug("[tls] accept from %s", client.remoteIP().toString().c_str());
    client.setNoDelay(true);
    client.setTimeout(5);

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);

    mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_conf_own_cert(&conf, &srvcert, &pkey);

    if (mbedtls_ssl_setup(&ssl, &conf) != 0) {
        portalDebug("[tls] setup failed");
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        client.stop();
        return;
    }

    mbedtls_ssl_set_bio(&ssl, &client, netSend, netRecv, nullptr);

    connStartMs = millis();
    firstDataLogged = false;
    firstSendLogged = false;
    uint32_t t0 = millis();
    int ret = mbedtls_ssl_handshake(&ssl);
    uint32_t dt = millis() - t0;
    if (ret == 0) {
        portalDebug("[tls] handshake ok");
        char buf[256];
        size_t len = 0;
        uint32_t start = millis();
        while (len < sizeof(buf) - 1 && millis() - start < 2000) {
            int r = mbedtls_ssl_read(&ssl, reinterpret_cast<unsigned char*>(buf) + len,
                                     sizeof(buf) - 1 - len);
            if (r > 0) {
                len += r;
                buf[len] = 0;
                if (strstr(buf, "\r\n\r\n") != nullptr || strstr(buf, "\n\n") != nullptr) {
                    break;
                }
            } else if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
                break;
            } else {
                break;
            }
        }

        if (len > 0) {
            buf[len] = 0;
            String firstLine = String(buf);
            int nl = firstLine.indexOf('\n');
            if (nl > 0) firstLine = firstLine.substring(0, nl);
            portalDebug("[tls] request: %s", firstLine.c_str());
        } else {
            portalDebug("[tls] no request read");
        }

        const char* body = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head>"
                           "<body><p>ESP-Pompa</p></body></html>";
        String resp = "HTTP/1.1 302 Found\r\n";
        resp += "Location: http://192.168.4.1/\r\n";
        resp += "Content-Type: text/html\r\n";
        resp += "Content-Length: ";
        resp += String(strlen(body));
        resp += "\r\nConnection: close\r\n\r\n";
        resp += body;

        int wr = mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(resp.c_str()),
                                   resp.length());
        portalDebug("[tls] sent 302 (write rc=%d)", wr);
    } else {
        portalDebug("[tls] handshake failed: -0x%04X after %lums", -ret, dt);
    }

    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    client.stop();
}
