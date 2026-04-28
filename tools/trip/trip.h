#ifndef TRIP_H
#define TRIP_H

#include "ctc_contract.h"

#define TRIP_MAX_RULES CTC_MAX_RULES
#define TRIP_MAX_NAME CTC_MAX_NAME

typedef ctc_level trip_level;
typedef ctc_op trip_op;
typedef ctc_rule trip_rule;
typedef ctc_ctx trip_ctx;

#define TRIP_LEVEL_INFO CTC_INFO
#define TRIP_LEVEL_WARN CTC_WARN
#define TRIP_LEVEL_ERROR CTC_ERROR

#define TRIP_OP_GT CTC_GT
#define TRIP_OP_GTE CTC_GTE
#define TRIP_OP_LT CTC_LT
#define TRIP_OP_LTE CTC_LTE
#define TRIP_OP_EQ CTC_EQ
#define TRIP_OP_NEQ CTC_NEQ

#define TRIP_RULE_LIMIT 0
#define TRIP_RULE_STALE 1

#define trip_init ctc_init
#define trip_add_rule ctc_add_rule
#define trip_add_stale_rule ctc_add_stale_rule
#define trip_load_rules_file ctc_load_rules_file
#define trip_sample ctc_sample
#define trip_level_name ctc_level_name
#define trip_op_name ctc_op_name
#define trip_rule_op_name ctc_rule_op_name
#define trip_parse_level ctc_parse_level
#define trip_parse_op ctc_parse_op
#define trip_rule_matches ctc_rule_matches

#endif /* TRIP_H */
