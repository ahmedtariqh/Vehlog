#ifndef VL_TEXT_H
#define VL_TEXT_H

#include "vl_scan.h"
#include <stddef.h>

#define VL_TEXT_CAPTURE_CAPACITY 192U
#define VL_TEXT_SAMPLE_CAPACITY ((2U * VL_ISOTP_CAPACITY) + 220U)
#define VL_TEXT_END_CAPACITY 96U

/* Successful records include a newline and a terminating NUL. */
bool vl_text_capture(const vl_can_frame_t *frame, const char *interface_name,
                     char *output, size_t capacity);
bool vl_text_sample(const vl_sample_t *sample, char *output, size_t capacity);
bool vl_text_scan_end(vl_scan_status_t result, bool stopped, char *output, size_t capacity);

#endif
