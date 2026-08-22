// net/http.h
#ifndef HTTP_H
#define HTTP_H

#include "../include/types.h"

int http_get(const char *url, u8 **response, u32 *resp_len);

#endif
