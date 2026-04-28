#ifndef DWELL_H
#define DWELL_H

#include "ctc_dwell.h"

typedef ctc_dwell_ctx dwell_ctx;
typedef ctc_dwell_watch_t dwell_watch_t;
typedef ctc_dwell_type dwell_type;
typedef ctc_dwell_reader_fn dwell_reader_fn;
typedef ctc_dwell_event_fn dwell_event_fn;
typedef ctc_dwell_sample_fn dwell_sample_fn;

#define DWELL_I32 CTC_DWELL_I32
#define DWELL_U32 CTC_DWELL_U32
#define DWELL_I64 CTC_DWELL_I64
#define DWELL_U64 CTC_DWELL_U64
#define DWELL_F32 CTC_DWELL_F32
#define DWELL_F64 CTC_DWELL_F64
#define DWELL_BOOL CTC_DWELL_BOOL
#define DWELL_READER CTC_DWELL_READER

#define dwell_init ctc_dwell_init
#define dwell_set_event_callback ctc_dwell_set_event_callback
#define dwell_set_sample_callback ctc_dwell_set_sample_callback
#define dwell_watch ctc_dwell_watch
#define dwell_watch_reader ctc_dwell_watch_reader
#define dwell_watch_i32 ctc_dwell_watch_i32
#define dwell_watch_u32 ctc_dwell_watch_u32
#define dwell_watch_i64 ctc_dwell_watch_i64
#define dwell_watch_u64 ctc_dwell_watch_u64
#define dwell_watch_f32 ctc_dwell_watch_f32
#define dwell_watch_f64 ctc_dwell_watch_f64
#define dwell_watch_bool ctc_dwell_watch_bool
#define dwell_sample_value ctc_dwell_sample_value
#define dwell_tick ctc_dwell_tick
#define dwell_find_watch ctc_dwell_find_watch
#define dwell_type_name ctc_dwell_type_name

#endif /* DWELL_H */
