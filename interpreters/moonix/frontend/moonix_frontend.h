#ifndef MOONIX_FRONTEND_H
#define MOONIX_FRONTEND_H

#include "moonix.h"

moonix_status moonix_frontend_compile(moonix_state *state,
                                      const char *source,
                                      size_t source_len,
                                      const char *chunk_name,
                                      moonix_chunk *chunk);

#endif
