#define AP_DECLARE(x)             x
#define AP_DECLARE_NONSTD(x)      x
#define AP_CORE_DECLARE(x)        x
#define AP_CORE_DECLARE_NONSTD(x) x
#define AP_LUA_DECLARE(x)         x
#define APR_DECLARE(x)            x
#define APR_DECLARE_NONSTD(x)     x
#define APU_DECLARE(x)            x
#define APU_DECLARE_NONSTD(x)     x
#define PROXY_DECLARE(x)          x
#define DAV_DECLARE(x)            x
#define APREQ_DECLARE(x)          x
#define APREQ_DECLARE_PARSER(x)   x

#define AP_DECLARE_DATA
#define AP_MODULE_DECLARE_DATA
#define APR_DECLARE_DATA
#define APR_MODULE_DECLARE_DATA
#define APU_DECLARE_DATA
#define DAV_DECLARE_DATA
#define PROXY_DECLARE_DATA

#define AP_DECLARE_MODULE(foo)  module foo##_module

#define BOOL                              int
#define STACK_OF(x)                       x
#define MODSSL_D2I_SSL_SESSION_CONST
#define MODSSL_D2I_ASN1_type_bytes_CONST
#define MODSSL_D2I_PrivateKey_CONST
#define MODSSL_D2I_X509_CONST
#define MODSSL_SSL_CIPHER_CONST
#define MODSSL_SSL_METHOD_CONST
