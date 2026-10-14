#include <cstring>

#include "check.h"
#include "draz/crypto.h"

using namespace draz::crypto;

namespace {

Bytes bytes(const char* text) { return Bytes(text, text + std::strlen(text)); }

std::string hex(const Digest& d) { return toHex(Bytes(d.begin(), d.end())); }

}  // namespace

// Test vectors from FIPS 180-4 examples, RFC 4231 and RFC 7914.

TEST("sha256 matches the standard test vectors") {
    CHECK_EQ(hex(sha256({})), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(hex(sha256(bytes("abc"))), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(hex(sha256(bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    std::string million(1000000, 'a');
    CHECK_EQ(hex(sha256(Bytes(million.begin(), million.end())))
             , std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

TEST("hmac-sha256 matches RFC 4231") {
    CHECK_EQ(hex(hmacSha256(Bytes(20, 0x0b), bytes("Hi There"))),
             std::string("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
    CHECK_EQ(hex(hmacSha256(bytes("Jefe"), bytes("what do ya want for nothing?"))),
             std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
    // Key longer than one block is hashed first.
    CHECK_EQ(hex(hmacSha256(Bytes(131, 0xaa), bytes("Test Using Larger Than Block-Size Key - Hash Key First"))),
             std::string("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
}

TEST("pbkdf2-sha256 matches RFC 7914 and known vectors") {
    CHECK_EQ(toHex(pbkdf2Sha256("password", bytes("salt"), 1, 32)),
             std::string("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
    CHECK_EQ(toHex(pbkdf2Sha256("password", bytes("salt"), 4096, 32)),
             std::string("c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"));
    CHECK_EQ(toHex(pbkdf2Sha256("passwordPASSWORDpassword", bytes("saltSALTsaltSALTsaltSALTsaltSALTsalt"), 4096, 40)),
             std::string("348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518c7dac47e9"));
    CHECK_EQ(toHex(pbkdf2Sha256("passwd", bytes("salt"), 1, 64)),
             std::string("55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
                         "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783"));
}

TEST("random bytes are the right length and not repeated") {
    Bytes a = randomBytes(16), b = randomBytes(16);
    CHECK_EQ(a.size(), std::size_t(16));
    CHECK(a != b);
    CHECK(randomBytes(0).empty());
}

TEST("hex round trip and invalid hex") {
    Bytes data = {0x00, 0x7f, 0x80, 0xff};
    CHECK_EQ(toHex(data), std::string("007f80ff"));
    CHECK(fromHex("007F80ff") == data);
    CHECK(fromHex("abc").empty());
    CHECK(fromHex("zz").empty());
}

TEST("constant time compare") {
    CHECK(constantTimeEqual(bytes("same"), bytes("same")));
    CHECK(!constantTimeEqual(bytes("same"), bytes("sane")));
    CHECK(!constantTimeEqual(bytes("short"), bytes("shorter")));
}
