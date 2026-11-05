#pragma once
#include <stdint.h>
#ifdef CAPTURE_BUILD
#define CAPTURE_API __declspec(dllexport)
#else
#define CAPTURE_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* UTF-8 JSON; response ownership belongs to this DLL. Never free across CRTs.
 * One request per context at a time. cancel may be called concurrently.
 * Destroy only after outstanding request returns. ABI version is major.minor. */
CAPTURE_API uint32_t capture_abi_version(void);
CAPTURE_API void *capture_create(void);
CAPTURE_API const char *capture_request(void *context, const char *request);
CAPTURE_API void capture_cancel(void *context);
CAPTURE_API void capture_destroy(void *context);
#ifdef __cplusplus
}
#endif
