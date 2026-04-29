#include "WebAuth.h"
#include "Credentials.h"
#include <mbedtls/base64.h>
#include <stdlib.h>

namespace WebAuth {

static constexpr const char* ADMIN_USER = "admin";

// Decode base64 string using mbedTLS (ships with ESP-IDF). Returns empty
// string on decode failure.
static String base64Decode(const String& b64) {
  // mbedtls needs a buffer; upper bound is (len * 3) / 4 + 1.
  size_t outLen = 0;
  size_t needed = (b64.length() * 3) / 4 + 1;
  uint8_t* buf = reinterpret_cast<uint8_t*>(malloc(needed));
  if (!buf) return String();
  int rc = mbedtls_base64_decode(buf, needed, &outLen,
                                 reinterpret_cast<const uint8_t*>(b64.c_str()),
                                 b64.length());
  String out;
  if (rc == 0 && outLen > 0) {
    out.reserve(outLen);
    for (size_t i = 0; i < outLen; ++i) out += static_cast<char>(buf[i]);
  }
  free(buf);
  return out;
}

bool checkAdminPassword(const String& plaintext) {
  const String stored = Credentials::adminPwdSha256Hex();
  if (stored.isEmpty()) return false;   // no password set = no admin access
  return Credentials::sha256Hex(plaintext) == stored;
}

bool requireAdmin(AsyncWebServerRequest* request) {
  // If no admin password is set yet, grant access (pre-setup state).
  // The setup wizard will prompt to set one; until then, UX > security.
  if (Credentials::adminPwdSha256Hex().isEmpty()) return true;

  if (!request->hasHeader("Authorization")) {
    AsyncWebServerResponse* r = request->beginResponse(401, "text/plain", "Authentication required");
    r->addHeader("WWW-Authenticate", "Basic realm=\"PoolMaster\", charset=\"UTF-8\"");
    request->send(r);
    return false;
  }
  String hdr = request->header("Authorization");
  if (!hdr.startsWith("Basic ")) {
    request->send(401, "text/plain", "Bad auth scheme");
    return false;
  }
  String b64 = hdr.substring(6);
  b64.trim();
  String decoded = base64Decode(b64);
  int colon = decoded.indexOf(':');
  if (colon <= 0) {
    request->send(401, "text/plain", "Malformed auth");
    return false;
  }
  String user = decoded.substring(0, colon);
  String pwd  = decoded.substring(colon + 1);
  if (user != ADMIN_USER || !checkAdminPassword(pwd)) {
    AsyncWebServerResponse* r = request->beginResponse(401, "text/plain", "Bad credentials");
    r->addHeader("WWW-Authenticate", "Basic realm=\"PoolMaster\"");
    request->send(r);
    return false;
  }
  return true;
}

} // namespace WebAuth
