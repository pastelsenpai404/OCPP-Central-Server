#include "ocpp/security/certificates.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
// clang-format on
#else
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#endif
namespace ocpp {
bool valid_csr(std::string_view pem) {
    constexpr std::string_view begin = "-----BEGIN CERTIFICATE REQUEST-----";
    constexpr std::string_view end = "-----END CERTIFICATE REQUEST-----";
    if (pem.size() > 16384 || !pem.starts_with(begin))
        return false;
    const auto finish = pem.find(end, begin.size());
    if (finish == pem.npos ||
        pem.substr(finish + end.size()).find_first_not_of(" \t\r\n") != pem.npos)
        return false;
    const auto body = pem.substr(begin.size(), finish - begin.size());
    if (body.empty() || std::any_of(body.begin(), body.end(), [](char c) {
            return !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                     c == '+' || c == '/' || c == '=' || c == ' ' || c == '\t' || c == '\r' ||
                     c == '\n');
        }))
        return false;
#ifdef _WIN32
    DWORD size = 0;
    if (!CryptStringToBinaryA(body.data(), static_cast<DWORD>(body.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &size, nullptr,
                              nullptr) ||
        size > 12288 || size < 4)
        return false;
    std::vector<BYTE> der(size);
    if (!CryptStringToBinaryA(body.data(), static_cast<DWORD>(body.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, der.data(), &size, nullptr,
                              nullptr))
        return false;
    // Require one complete DER sequence; CryptoAPI otherwise tolerates trailing bytes.
    if (der[0] != 0x30)
        return false;
    std::size_t content = der[1], header = 2;
    if (content & 0x80) {
        const auto count = content & 0x7f;
        if (!count || count > 4 || count + 2 > der.size() || der[2] == 0)
            return false;
        content = 0;
        header += count;
        for (std::size_t i = 2; i < header; ++i)
            content = (content << 8) | der[i];
        if (content < 128)
            return false;
    }
    if (header + content != der.size())
        return false;
    auto free_local = [](void *p) {
        if (p)
            LocalFree(p);
    };
    CERT_SIGNED_CONTENT_INFO *raw_signed = nullptr;
    DWORD decoded = 0;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_CERT, der.data(), size,
                             CRYPT_DECODE_ALLOC_FLAG, nullptr, &raw_signed, &decoded))
        return false;
    std::unique_ptr<CERT_SIGNED_CONTENT_INFO, decltype(free_local)> signed_info(raw_signed,
                                                                                free_local);
    const std::string oid = signed_info->SignatureAlgorithm.pszObjId;
    if (oid != szOID_RSA_SHA256RSA && oid != szOID_RSA_SHA384RSA && oid != szOID_RSA_SHA512RSA &&
        oid != szOID_ECDSA_SHA256 && oid != szOID_ECDSA_SHA384 && oid != szOID_ECDSA_SHA512)
        return false;
    CERT_REQUEST_INFO *raw_request = nullptr;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_CERT_REQUEST_TO_BE_SIGNED,
                             signed_info->ToBeSigned.pbData, signed_info->ToBeSigned.cbData,
                             CRYPT_DECODE_ALLOC_FLAG, nullptr, &raw_request, &decoded))
        return false;
    std::unique_ptr<CERT_REQUEST_INFO, decltype(free_local)> request(raw_request, free_local);
    const std::string key_oid = request->SubjectPublicKeyInfo.Algorithm.pszObjId;
    const bool rsa = key_oid == szOID_RSA_RSA, ec = key_oid == szOID_ECC_PUBLIC_KEY;
    if (!rsa && !ec)
        return false;
    BCRYPT_KEY_HANDLE key = nullptr;
    if (!CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING, &request->SubjectPublicKeyInfo, 0, nullptr,
                                     &key))
        return false;
    DWORD bits = 0, returned = 0;
    const auto property = BCryptGetProperty(key, BCRYPT_KEY_LENGTH, reinterpret_cast<PUCHAR>(&bits),
                                            sizeof(bits), &returned, 0);
    BCryptDestroyKey(key);
    if (property < 0 || (rsa && (bits < 2048 || bits > 8192)) || (ec && (bits < 256 || bits > 521)))
        return false;
    CRYPT_DATA_BLOB blob{size, der.data()};
    return CryptVerifyCertificateSignatureEx(
               0, X509_ASN_ENCODING, CRYPT_VERIFY_CERT_SIGN_SUBJECT_BLOB, &blob,
               CRYPT_VERIFY_CERT_SIGN_ISSUER_PUBKEY, &request->SubjectPublicKeyInfo,
               CRYPT_VERIFY_CERT_SIGN_DISABLE_MD2_MD4_FLAG, nullptr) != FALSE;
#else
    std::string base64;
    for (const auto c : body)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            base64 += c;
    if (base64.empty() || base64.size() % 4)
        return false;
    const auto padding = base64.ends_with("==") ? 2 : base64.ends_with('=') ? 1 : 0;
    if (base64.substr(0, base64.size() - static_cast<std::size_t>(padding)).find('=') !=
        std::string::npos)
        return false;
    std::vector<unsigned char> der(base64.size());
    const auto bytes =
        EVP_DecodeBlock(der.data(), reinterpret_cast<const unsigned char *>(base64.data()),
                        static_cast<int>(base64.size()));
    if (bytes <= padding)
        return false;
    der.resize(static_cast<std::size_t>(bytes - padding));
    const auto *cursor = der.data();
    std::unique_ptr<X509_REQ, decltype(&X509_REQ_free)> request(
        d2i_X509_REQ(nullptr, &cursor, static_cast<long>(der.size())), X509_REQ_free);
    if (!request || cursor != der.data() + der.size())
        return false;
    const auto signature = X509_REQ_get_signature_nid(request.get());
    if (signature != NID_sha256WithRSAEncryption && signature != NID_sha384WithRSAEncryption &&
        signature != NID_sha512WithRSAEncryption && signature != NID_ecdsa_with_SHA256 &&
        signature != NID_ecdsa_with_SHA384 && signature != NID_ecdsa_with_SHA512)
        return false;
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(X509_REQ_get_pubkey(request.get()),
                                                            EVP_PKEY_free);
    if (!key)
        return false;
    const auto type = EVP_PKEY_base_id(key.get()), bits = EVP_PKEY_get_bits(key.get());
    if (!((type == EVP_PKEY_RSA && bits >= 2048 && bits <= 8192) ||
          (type == EVP_PKEY_EC && bits >= 256 && bits <= 521)))
        return false;
    return X509_REQ_verify(request.get(), key.get()) == 1;
#endif
}
} // namespace ocpp
