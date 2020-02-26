/*
 * Copyright 2016 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 * Copyright 2016 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Author: Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <babeltrace2/babeltrace.h>
#include "compat/bitfield.h"
#include "common/common.h"
#include "common/uuid.h"
#include "compat/time.h"
#include "common/assert.h"
#include <inttypes.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include "pretty.h"

#define NSEC_PER_SEC 1000000000LL

static char color_name[32];
static char color_field_name[32];
static char color_rst[32];
static char color_string_value[32];
static char color_number_value[32];
static char color_enum_mapping_name[32];
static char color_unknown[32];
static char color_event_name[32];
static char color_timestamp[32];

struct timestamp {
	int64_t real_timestamp;	/* Relative to UNIX epoch. */
	uint64_t clock_snapshot;	/* In cycles. */
};

static
int print_field(struct pretty_component *pretty,
		const bt_field *field, bool print_names);

static
void print_name_equal(struct pretty_component *pretty, const char *name)
{
	if (pretty->use_colors) {
		bt_common_g_string_append(pretty->string, color_name);
		bt_common_g_string_append(pretty->string, name);
		bt_common_g_string_append(pretty->string, color_rst);
	} else {
		bt_common_g_string_append(pretty->string, name);
	}
	bt_common_g_string_append(pretty->string, " = ");
}

static
void print_field_name_equal(struct pretty_component *pretty, const char *name)
{
	if (pretty->use_colors) {
		bt_common_g_string_append(pretty->string, color_field_name);
		bt_common_g_string_append(pretty->string, name);
		bt_common_g_string_append(pretty->string, color_rst);
	} else {
		bt_common_g_string_append(pretty->string, name);
	}
	bt_common_g_string_append(pretty->string, " = ");
}

static
void print_timestamp_cycles(struct pretty_component *pretty,
		const bt_clock_snapshot *clock_snapshot, bool update_last)
{
	uint64_t cycles;

	cycles = bt_clock_snapshot_get_value(clock_snapshot);
	bt_common_g_string_append_printf(pretty->string, "%020" PRIu64, cycles);

	if (update_last) {
		if (pretty->last_cycles_timestamp != -1ULL) {
			pretty->delta_cycles = cycles - pretty->last_cycles_timestamp;
		}

		pretty->last_cycles_timestamp = cycles;
	}
}

static
void print_timestamp_wall(struct pretty_component *pretty,
		const bt_clock_snapshot *clock_snapshot, bool update_last)
{
	int ret;
	int64_t ts_nsec = 0;	/* add configurable offset */
	int64_t ts_sec = 0;	/* add configurable offset */
	uint64_t ts_sec_abs, ts_nsec_abs;
	bool is_negative;

	if (!clock_snapshot) {
		bt_common_g_string_append(pretty->string, "??:??:??.?????????");
		return;
	}

	ret = bt_clock_snapshot_get_ns_from_origin(clock_snapshot, &ts_nsec);
	if (ret) {
		// TODO: log, this is unexpected
		bt_common_g_string_append(pretty->string, "Error");
		return;
	}

	if (update_last) {
		if (pretty->last_real_timestamp != -1ULL) {
			pretty->delta_real_timestamp = ts_nsec - pretty->last_real_timestamp;
		}

		pretty->last_real_timestamp = ts_nsec;
	}

	ts_sec += ts_nsec / NSEC_PER_SEC;
	ts_nsec = ts_nsec % NSEC_PER_SEC;

	if (ts_sec >= 0 && ts_nsec >= 0) {
		is_negative = false;
		ts_sec_abs = ts_sec;
		ts_nsec_abs = ts_nsec;
	} else if (ts_sec > 0 && ts_nsec < 0) {
		is_negative = false;
		ts_sec_abs = ts_sec - 1;
		ts_nsec_abs = NSEC_PER_SEC + ts_nsec;
	} else if (ts_sec == 0 && ts_nsec < 0) {
		is_negative = true;
		ts_sec_abs = ts_sec;
		ts_nsec_abs = -ts_nsec;
	} else if (ts_sec < 0 && ts_nsec > 0) {
		is_negative = true;
		ts_sec_abs = -(ts_sec + 1);
		ts_nsec_abs = NSEC_PER_SEC - ts_nsec;
	} else if (ts_sec < 0 && ts_nsec == 0) {
		is_negative = true;
		ts_sec_abs = -ts_sec;
		ts_nsec_abs = ts_nsec;
	} else {	/* (ts_sec < 0 && ts_nsec < 0) */
		is_negative = true;
		ts_sec_abs = -ts_sec;
		ts_nsec_abs = -ts_nsec;
	}

	if (!pretty->options.clock_seconds) {
		struct tm tm;
		time_t time_s = (time_t) ts_sec_abs;

		if (is_negative && !pretty->negative_timestamp_warning_done) {
			// TODO: log instead
			fprintf(stderr, "[warning] Fallback to [sec.ns] to print negative time value. Use --clock-seconds.\n");
			pretty->negative_timestamp_warning_done = true;
			goto seconds;
		}

		if (!pretty->options.clock_gmt) {
			struct tm *res;

			res = bt_localtime_r(&time_s, &tm);
			if (!res) {
				// TODO: log instead
				fprintf(stderr, "[warning] Unable to get localtime.\n");
				goto seconds;
			}
		} else {
			struct tm *res;

			res = bt_gmtime_r(&time_s, &tm);
			if (!res) {
				// TODO: log instead
				fprintf(stderr, "[warning] Unable to get gmtime.\n");
				goto seconds;
			}
		}
		if (pretty->options.clock_date) {
			char timestr[26];
			size_t res;

			/* Print date and time */
			res = strftime(timestr, sizeof(timestr),
					"%Y-%m-%d ", &tm);
			if (!res) {
				// TODO: log instead
				fprintf(stderr, "[warning] Unable to print ascii time.\n");
				goto seconds;
			}

			bt_common_g_string_append(pretty->string, timestr);
		}

		/* Print time in HH:MM:SS.ns */
		bt_common_g_string_append_printf(pretty->string,
			"%02d:%02d:%02d.%09" PRIu64, tm.tm_hour, tm.tm_min,
			tm.tm_sec, ts_nsec_abs);
		goto end;
	}
seconds:
	bt_common_g_string_append_printf(pretty->string, "%s%" PRId64 ".%09" PRIu64,
		is_negative ? "-" : "", ts_sec_abs, ts_nsec_abs);
end:
	return;
}

static
int print_event_timestamp(struct pretty_component *pretty,
		const bt_message *event_msg, bool *start_line)
{
	bool print_names = pretty->options.print_header_field_names;
	int ret = 0;
	const bt_clock_snapshot *clock_snapshot = NULL;

	if (!bt_message_event_borrow_stream_class_default_clock_class_const(
			event_msg)) {
		/* No default clock class: skip the timestamp without an error */
		goto end;
	}

	clock_snapshot = bt_message_event_borrow_default_clock_snapshot_const(event_msg);

	if (print_names) {
		print_name_equal(pretty, "timestamp");
	} else {
		bt_common_g_string_append(pretty->string, "[");
	}
	if (pretty->use_colors) {
		bt_common_g_string_append(pretty->string, color_timestamp);
	}
	if (pretty->options.print_timestamp_cycles) {
		print_timestamp_cycles(pretty, clock_snapshot, true);
	} else {
		print_timestamp_wall(pretty, clock_snapshot, true);
	}
	if (pretty->use_colors) {
		bt_common_g_string_append(pretty->string, color_rst);
	}

	if (!print_names)
		bt_common_g_string_append(pretty->string, "] ");

	if (pretty->options.print_delta_field) {
		if (print_names) {
			bt_common_g_string_append(pretty->string, ", ");
			print_name_equal(pretty, "delta");
		} else {
			bt_common_g_string_append(pretty->string, "(");
		}
		if (pretty->options.print_timestamp_cycles) {
			if (pretty->delta_cycles == -1ULL) {
				bt_common_g_string_append(pretty->string,
					"+??????????\?\?"); /* Not a trigraph. */
			} else {
				bt_common_g_string_append_printf(pretty->string,
					"+%012" PRIu64, pretty->delta_cycles);
			}
		} else {
			if (pretty->delta_real_timestamp != -1ULL) {
				uint64_t delta_sec, delta_nsec, delta;

				delta = pretty->delta_real_timestamp;
				delta_sec = delta / NSEC_PER_SEC;
				delta_nsec = delta % NSEC_PER_SEC;
				bt_common_g_string_append_printf(pretty->string,
					"+%" PRIu64 ".%09" PRIu64,
					delta_sec, delta_nsec);
			} else {
				bt_common_g_string_append(pretty->string, "+?.?????????");
			}
		}
		if (!print_names) {
			bt_common_g_string_append(pretty->string, ") ");
		}
	}
	*start_line = !print_names;

end:
	return ret;
}

static
int print_event_header(struct pretty_component *pretty,
		const bt_message *event_msg)
{
	bool print_names = pretty->options.print_header_field_names;
	int ret = 0;
	const bt_event_class *event_class = NULL;
	const bt_stream *stream = NULL;
	const bt_trace *trace = NULL;
	const bt_event *event = bt_message_event_borrow_event_const(event_msg);
	const char *ev_name;
	int dom_print = 0;
	bt_property_availability prop_avail;

	event_class = bt_event_borrow_class_const(event);
	stream = bt_event_borrow_stream_const(event);
	trace = bt_stream_borrow_trace_const(stream);
	ret = print_event_timestamp(pretty, event_msg, &pretty->start_line);
	if (ret) {
		goto end;
	}
	if (pretty->options.print_trace_field) {
		const char *name;

		name = bt_trace_get_name(trace);
		if (name) {
			if (!pretty->start_line) {
				bt_common_g_string_append(pretty->string, ", ");
			}
			if (print_names) {
				print_name_equal(pretty, "trace");
			}

			bt_common_g_string_append(pretty->string, name);

			if (print_names) {
				bt_common_g_string_append(pretty->string, ", ");
			}
		}
	}
	if (pretty->options.print_trace_hostname_field) {
		const bt_value *hostname_str;

		hostname_str = bt_trace_borrow_environment_entry_value_by_name_const(
			trace, "hostname");
		if (hostname_str) {
			const char *str;

			if (!pretty->start_line) {
				bt_common_g_string_append(pretty->string, ", ");
			}
			if (print_names) {
				print_name_equal(pretty, "trace:hostname");
			}
			str = bt_value_string_get(hostname_str);
			bt_common_g_string_append(pretty->string, str);
			dom_print = 1;
		}
	}
	if (pretty->options.print_trace_domain_field) {
		const bt_value *domain_str;

		domain_str = bt_trace_borrow_environment_entry_value_by_name_const(
			trace, "domain");
		if (domain_str) {
			const char *str;

			if (!pretty->start_line) {
				bt_common_g_string_append(pretty->string, ", ");
			}
			if (print_names) {
				print_name_equal(pretty, "trace:domain");
			} else if (dom_print) {
				bt_common_g_string_append(pretty->string, ":");
			}
			str = bt_value_string_get(domain_str);
			bt_common_g_string_append(pretty->string, str);
			dom_print = 1;
		}
	}
	if (pretty->options.print_trace_procname_field) {
		const bt_value *procname_str;

		procname_str = bt_trace_borrow_environment_entry_value_by_name_const(
			trace, "procname");
		if (procname_str) {
			const char *str;

			if (!pretty->start_line) {
				bt_common_g_string_append(pretty->string, ", ");
			}
			if (print_names) {
				print_name_equal(pretty, "trace:procname");
			} else if (dom_print) {
				bt_common_g_string_append(pretty->string, ":");
			}
			str = bt_value_string_get(procname_str);
			bt_common_g_string_append(pretty->string, str);
			dom_print = 1;
		}
	}
	if (pretty->options.print_trace_vpid_field) {
		const bt_value *vpid_value;

		vpid_value = bt_trace_borrow_environment_entry_value_by_name_const(
			trace, "vpid");
		if (vpid_value) {
			int64_t value;

			if (!pretty->start_line) {
				bt_common_g_string_append(pretty->string, ", ");
			}
			if (print_names) {
				print_name_equal(pretty, "trace:vpid");
			} else if (dom_print) {
				bt_common_g_string_append(pretty->string, ":");
			}
			value = bt_value_integer_signed_get(vpid_value);
			bt_common_g_string_append_printf(pretty->string,
				"(%" PRId64 ")", value);
			dom_print = 1;
		}
	}
	if (pretty->options.print_loglevel_field) {
		static const char *log_level_names[] = {
			[ BT_EVENT_CLASS_LOG_LEVEL_EMERGENCY ] = "TRACE_EMERG",
			[ BT_EVENT_CLASS_LOG_LEVEL_ALERT ] = "TRACE_ALERT",
			[ BT_EVENT_CLASS_LOG_LEVEL_CRITICAL ] = "TRACE_CRIT",
			[ BT_EVENT_CLASS_LOG_LEVEL_ERROR ] = "TRACE_ERR",
			[ BT_EVENT_CLASS_LOG_LEVEL_WARNING ] = "TRACE_WARNING",
			[ BT_EVENT_CLASS_LOG_LEVEL_NOTICE ] = "TRACE_NOTICE",
			[ BT_EVENT_CLASS_LOG_LEVEL_INFO ] = "TRACE_INFO",
			[ BT_EVENT_CLASS_LOG_LEVEL_DEBUG_SYSTEM ] = "TRACE_DEBUG_SYSTEM",
			[ BT_EVENT_CLASS_LOG_LEVEL_DEBUG_PROGRAM ] = "TRACE_DEBUG_PROGRAM",
			[ BT_EVENT_CLASS_LOG_LEVEL_DEBUG_PROCESS ] = "TRACE_DEBUG_PROCESS",
			[ BT_EVENT_CLASS_LOG_LEVEL_DEBUG_MODULE ] = "TRACE_DEBUG_MODULE",
			[ BT_EVENT_CLASS_LOG_LEVEL_DEBUG_UNIT ] = "TRACE_DEBUG_UNIT",
			[ BT_EVENT_CLASS_LOG_LEVEL_DEBUG_FUNCTION ] = "TRACE_DEBUG_FUNCTION",
			[ BT_EVENT_CLASS_LOG_LEVEL_DEBUG_LINE ] = "TRACE_DEBUG_LINE",
			[ BT_EVENT_CLASS_LOG_LEVEL_DEBUG ] = "TRACE_DEBUG",
		};
		bt_event_class_log_level log_level;
		const char *log_level_str = NULL;

		prop_avail = bt_event_class_get_log_level(event_class,
			&log_level);
		if (prop_avail == BT_PROPERTY_AVAILABILITY_AVAILABLE) {
			log_level_str = log_level_names[log_level];
			BT_ASSERT_DBG(log_level_str);

			if (!pretty->start_line) {
				bt_common_g_string_append(pretty->string, ", ");
			}
			if (print_names) {
				print_name_equal(pretty, "loglevel");
			} else if (dom_print) {
				bt_common_g_string_append(pretty->string, ":");
			}

			bt_common_g_string_append(pretty->string, log_level_str);
			bt_common_g_string_append_printf(
				pretty->string, " (%d)", (int) log_level);
			dom_print = 1;
		}
	}
	if (pretty->options.print_emf_field) {
		const char *uri_str;

		uri_str = bt_event_class_get_emf_uri(event_class);
		if (uri_str) {
			if (!pretty->start_line) {
				bt_common_g_string_append(pretty->string, ", ");
			}
			if (print_names) {
				print_name_equal(pretty, "model.emf.uri");
			} else if (dom_print) {
				bt_common_g_string_append(pretty->string, ":");
			}

			bt_common_g_string_append(pretty->string, uri_str);
			dom_print = 1;
		}
	}
	if (dom_print && !print_names) {
		bt_common_g_string_append(pretty->string, " ");
	}
	if (!pretty->start_line) {
		bt_common_g_string_append(pretty->string, ", ");
	}
	pretty->start_line = true;
	if (print_names) {
		print_name_equal(pretty, "name");
	}
	ev_name = bt_event_class_get_name(event_class);
	if (pretty->use_colors) {
		if (ev_name) {
			bt_common_g_string_append(pretty->string,
				color_event_name);
		} else {
			bt_common_g_string_append(pretty->string,
				color_unknown);
		}
	}
	if (ev_name) {
		bt_common_g_string_append(pretty->string, ev_name);
	} else {
		bt_common_g_string_append(pretty->string, "<unknown>");
	}
	if (pretty->use_colors) {
		bt_common_g_string_append(pretty->string, color_rst);
	}
	if (!print_names) {
		bt_common_g_string_append(pretty->string, ": ");
	} else {
		bt_common_g_string_append(pretty->string, ", ");
	}

end:
	return ret;
}

static
int print_integer(struct pretty_component *pretty,
		const bt_field *field)
{
	int ret = 0;
	bt_field_class_integer_preferred_display_base base;
	const bt_field_class *int_fc;
	union {
		uint64_t u;
		int64_t s;
	} v;
	bool rst_color = false;
	bt_field_class_type ft_type;

	int_fc = bt_field_borrow_class_const(field);
	BT_ASSERT_DBG(int_fc);
	ft_type = bt_field_get_class_type(field);
	if (bt_field_class_type_is(ft_type,
			BT_FIELD_CLASS_TYPE_UNSIGNED_INTEGER)) {
		v.u = bt_field_integer_unsigned_get_value(field);
	} else {
		v.s = bt_field_integer_signed_get_value(field);
	}

	if (pretty->use_colors) {
		bt_common_g_string_append(pretty->string, color_number_value);
		rst_color = true;
	}

	base = bt_field_class_integer_get_preferred_display_base(int_fc);
	switch (base) {
	case BT_FIELD_CLASS_INTEGER_PREFERRED_DISPLAY_BASE_BINARY:
	{
		int bitnr, len;

		len = bt_field_class_integer_get_field_value_range(int_fc);
		bt_common_g_string_append(pretty->string, "0b");
		_bt_safe_lshift(v.u, 64 - len);
		for (bitnr = 0; bitnr < len; bitnr++) {
			bt_common_g_string_append_c(pretty->string,
						(v.u & (1ULL << 63)) ? '1' : '0');
			_bt_safe_lshift(v.u, 1);
		}
		break;
	}
	case BT_FIELD_CLASS_INTEGER_PREFERRED_DISPLAY_BASE_OCTAL:
	{
		if (bt_field_class_type_is(ft_type,
				BT_FIELD_CLASS_TYPE_SIGNED_INTEGER)) {
			int len;

			len = bt_field_class_integer_get_field_value_range(
				int_fc);
			if (len < 64) {
			        size_t rounded_len;

				BT_ASSERT_DBG(len != 0);
				/* Round length to the nearest 3-bit */
				rounded_len = (((len - 1) / 3) + 1) * 3;
				v.u &= ((uint64_t) 1 << rounded_len) - 1;
			}
		}

		bt_common_g_string_append_printf(pretty->string, "0%" PRIo64, v.u);
		break;
	}
	case BT_FIELD_CLASS_INTEGER_PREFERRED_DISPLAY_BASE_DECIMAL:
		if (bt_field_class_type_is(ft_type,
				BT_FIELD_CLASS_TYPE_UNSIGNED_INTEGER)) {
			bt_common_g_string_append_printf(pretty->string, "%" PRIu64, v.u);
		} else {
			bt_common_g_string_append_printf(pretty->string, "%" PRId64, v.s);
		}
		break;
	case BT_FIELD_CLASS_INTEGER_PREFERRED_DISPLAY_BASE_HEXADECIMAL:
	{
		int len;

		len = bt_field_class_integer_get_field_value_range(int_fc);
		if (len < 64) {
			/* Round length to the nearest nibble */
			uint8_t rounded_len = ((len + 3) & ~0x3);

			v.u &= ((uint64_t) 1 << rounded_len) - 1;
		}

		bt_common_g_string_append_printf(pretty->string, "0x%" PRIX64, v.u);
		break;
	}
	default:
		ret = -1;
		goto end;
	}
end:
	if (rst_color) {
		bt_common_g_string_append(pretty->string, color_rst);
	}
	return ret;
}

static
void print_escape_string(struct pretty_component *pretty, const char *str)
{
	int i;

	bt_common_g_string_append_c(pretty->string, '"');

	for (i = 0; i < strlen(str); i++) {
		/* Escape sequences not recognized by iscntrl(). */
		switch (str[i]) {
		case '\\':
			bt_common_g_string_append(pretty->string, "\\\\");
			continue;
		case '\'':
			bt_common_g_string_append(pretty->string, "\\\'");
			continue;
		case '\"':
			bt_common_g_string_append(pretty->string, "\\\"");
			continue;
		case '\?':
			bt_common_g_string_append(pretty->string, "\\\?");
			continue;
		}

		/* Standard characters. */
		if (!iscntrl(str[i])) {
			bt_common_g_string_append_c(pretty->string, str[i]);
			continue;
		}

		switch (str[i]) {
		case '\0':
			bt_common_g_string_append(pretty->string, "\\0");
			break;
		case '\a':
			bt_common_g_string_append(pretty->string, "\\a");
			break;
		case '\b':
			bt_common_g_string_append(pretty->string, "\\b");
			break;
		case '\e':
			bt_common_g_string_append(pretty->string, "\\e");
			break;
		case '\f':
			bt_common_g_string_append(pretty->string, "\\f");
			break;
		case '\n':
			bt_common_g_string_append(pretty->string, "\\n");
			break;
		case '\r':
			bt_common_g_string_append(pretty->string, "\\r");
			break;
		case '\t':
			bt_common_g_string_append(pretty->string, "\\t");
			break;
		case '\v':
			bt_common_g_string_append(pretty->string, "\\v");
			break;
		default:
			/* Unhandled control-sequence, print as hex. */
			bt_common_g_string_append_printf(pretty->string, "\\x%02x", str[i]);
			break;
		}
	}

	bt_common_g_string_append_c(pretty->string, '"');
}

static
int print_enum_value_label_unknown(struct pretty_component *pretty)
{
	if (pretty->use_colors) {
		bt_common_g_string_append(pretty->string, color_unknown);
	}
	bt_common_g_string_append(pretty->string, "<unknown>");
	if (pretty->use_colors) {
		bt_common_g_string_append(pretty->string, color_rst);
	}
	return 0;
}

static
int print_enum_value_label_array(struct pretty_component *pretty,
	uint64_t label_count,
	bt_field_class_enumeration_mapping_label_array label_array)
{
	uint64_t i;

	if (label_count > 1) {
		bt_common_g_string_append(pretty->string, "{ ");
	}
	for (i = 0; i < label_count; i++) {
		const char *mapping_name = label_array[i];

		if (i != 0) {
			bt_common_g_string_append(pretty->string, ", ");
		}
		if (pretty->use_colors) {
			bt_common_g_string_append(pretty->string, color_enum_mapping_name);
		}
		print_escape_string(pretty, mapping_name);
		if (pretty->use_colors) {
			bt_common_g_string_append(pretty->string, color_rst);
		}
	}
	if (label_count > 1) {
		bt_common_g_string_append(pretty->string, " }");
	}
	return 0;
}

static
int print_enum_unsigned_get_mapping_labels_for_value(const bt_field_class *fc,
	uint64_t value,
	bt_field_class_enumeration_mapping_label_array *label_array,
	uint64_t *count)
{
	uint64_t mapping_count = bt_field_class_enumeration_get_mapping_count(fc);
	GPtrArray *labels = g_ptr_array_new ();
	uint64_t i;

	*count = 0;
	for (i = 0; i < mapping_count; i++) {
		uint64_t range_i;
		const struct bt_field_class_enumeration_unsigned_mapping *mapping =
			bt_field_class_enumeration_unsigned_borrow_mapping_by_index_const(fc, i);
		const bt_integer_range_set_unsigned *ranges =
			bt_field_class_enumeration_unsigned_mapping_borrow_ranges_const(mapping);

		for (range_i = 0; range_i < bt_integer_range_set_get_range_count(
				bt_integer_range_set_unsigned_as_range_set_const(ranges)); range_i++) {
			const bt_integer_range_unsigned *range =
				bt_integer_range_set_unsigned_borrow_range_by_index_const(ranges, range_i);
			uint64_t lower = bt_integer_range_unsigned_get_lower(range);
			uint64_t higher = bt_integer_range_unsigned_get_upper(range);
	
			/*
			 * Flag is active if this range represents a single value 
			 * (lower == higher) and the lower is the same as the bit
			 * value to test against
			 */
			if ((lower == higher) && (lower == value)) {
				g_ptr_array_add(labels, (void*) bt_field_class_enumeration_mapping_get_label(
					bt_field_class_enumeration_unsigned_mapping_as_mapping_const(mapping)));
				*count = *count + 1;
				break;
			}
		}
	}
	*label_array = (void *) labels->pdata;

	return 0;
}

static
int print_enum_unsigned_try_bit_flags(struct pretty_component *pretty,
		const bt_field *field,
		const bt_field_class *fc,
		uint64_t int_range,
		bt_field_class_enumeration_mapping_label_array label_arrays[],
		uint64_t label_counts[])
{
	int ret = 0;
	uint64_t i;
	uint64_t value = bt_field_integer_unsigned_get_value(field);

	/* Value is 0, if there was a label for it, we would know by now */
	if (value == 0) {
		print_enum_value_label_unknown(pretty);
		ret = -1;
		goto end;
	}

	for (i = 0; i < int_range; i++) {
		uint64_t bit_value = 1ULL << i;

		if ((value & bit_value) != 0) {
			ret = print_enum_unsigned_get_mapping_labels_for_value(
				fc, bit_value, &label_arrays[i], &label_counts[i]);

			if (ret) {
				ret = -1;
				goto end;
			}

			if (label_counts[i] == 0) {
				/*
				 * This bit has no matching label, so this
				 * field is not a bit flag field, print
				 * unknown and return
				 */
				print_enum_value_label_unknown(pretty);
				ret = -1;
				goto end;
			}
		} else {
			label_counts[i] = 0;
		}
	}
end:
	return ret;
}

static
int print_enum_signed_get_mapping_labels_for_value(const bt_field_class *fc,
	uint64_t value,
	bt_field_class_enumeration_mapping_label_array *label_array,
	uint64_t *count)
{
	uint64_t mapping_count = bt_field_class_enumeration_get_mapping_count(fc);
	GPtrArray *labels = g_ptr_array_new ();
	uint64_t i;

	*count = 0;
	for (i = 0; i < mapping_count; i++) {
		uint64_t range_i;
		const struct bt_field_class_enumeration_signed_mapping *mapping =
			bt_field_class_enumeration_signed_borrow_mapping_by_index_const(fc, i);
		const bt_integer_range_set_signed *ranges =
			bt_field_class_enumeration_signed_mapping_borrow_ranges_const(mapping);

		for (range_i = 0; range_i < bt_integer_range_set_get_range_count(
				bt_integer_range_set_signed_as_range_set_const(ranges)); range_i++) {
			const bt_integer_range_signed *range =
				bt_integer_range_set_signed_borrow_range_by_index_const(ranges, range_i);
			uint64_t lower = bt_integer_range_signed_get_lower(range);
			uint64_t higher = bt_integer_range_signed_get_upper(range);
	
			/*
			 * Flag is active if this range represents a single value 
			 * (lower == higher) and the lower is the same as the bit
			 * value to test against
			 */
			if ((lower == higher) && (lower == value)) {
				g_ptr_array_add(labels, (void*) bt_field_class_enumeration_mapping_get_label(
					bt_field_class_enumeration_signed_mapping_as_mapping_const(mapping)));
				*count = *count + 1;
				break;
			}
		}
	}
	*label_array = (void *) labels->pdata;

	return 0;
}

static
int print_enum_signed_try_bit_flags(struct pretty_component *pretty,
		const bt_field *field,
		const bt_field_class *fc,
		uint64_t int_range,
		bt_field_class_enumeration_mapping_label_array label_arrays[],
		uint64_t label_counts[])
{
	int ret = 0;
	uint64_t i;
	uint64_t value = bt_field_integer_signed_get_value(field);

	/* 
	 * Negative value, not a bit flag enum
	 * For 0, if there was a value, we would know by now
	 */
	if (value <= 0) {
		print_enum_value_label_unknown(pretty);
		ret = -1;
		goto end;
	}

	for (i = 0; i < int_range; i++) {
		uint64_t bit_value = 1ULL << i;

		if ((value & bit_value) != 0) {
			ret = print_enum_signed_get_mapping_labels_for_value(
				fc, bit_value, &label_arrays[i], &label_counts[i]);

			if (ret) {
				ret = -1;
				goto end;
			}

			if (label_counts[i] == 0) {
				/*
				 * This bit has no matching label, so this
				 * field is not a bit flag field, print
				 * unknown and return
				 */
				print_enum_value_label_unknown(pretty);
				ret = -1;
				goto end;
			}
		} else {
			label_counts[i] = 0;
		}
	}
end:
	return ret;
}

static
int print_enum_try_bit_flags(struct pretty_component *pretty,
		const bt_field *field,
		const bt_field_class *fc)
{
	int ret = 0;
	uint64_t i;
	uint64_t int_range = bt_field_class_integer_get_field_value_range(fc);
	uint64_t label_counts[int_range];
	bt_field_class_enumeration_mapping_label_array label_arrays[int_range];
	bool first_label = true;

	// Get the mapping labels for the bit value
	switch (bt_field_class_get_type(fc)) {
	case BT_FIELD_CLASS_TYPE_UNSIGNED_ENUMERATION:
		ret = print_enum_unsigned_try_bit_flags(pretty, field,
			fc, int_range, label_arrays, label_counts);
	break;
	case BT_FIELD_CLASS_TYPE_SIGNED_ENUMERATION:
		ret = print_enum_signed_try_bit_flags(pretty, field,
			fc, int_range, label_arrays, label_counts);
		break;
	default:
		bt_common_abort();
	}

	if (ret) {
		ret = -1;
		goto end;
	}

	/* Value is a bit flag, print the labels */
	for (i = 0; i < int_range; i++) {
		if (label_counts[i] > 0) {
			if (!first_label) {
				bt_common_g_string_append(pretty->string, " | ");
			}
			print_enum_value_label_array(pretty,
				label_counts[i], label_arrays[i]);
			first_label = false;
		}
	}
end:
	return ret;
}

static
int print_enum(struct pretty_component *pretty,
		const bt_field *field)
{
	int ret = 0;
	const bt_field_class *enumeration_field_class = NULL;
	bt_field_class_enumeration_mapping_label_array label_array;
	uint64_t label_count;

	enumeration_field_class = bt_field_borrow_class_const(field);
	if (!enumeration_field_class) {
		ret = -1;
		goto end;
	}

	switch (bt_field_get_class_type(field)) {
	case BT_FIELD_CLASS_TYPE_UNSIGNED_ENUMERATION:
		ret = bt_field_enumeration_unsigned_get_mapping_labels(field,
			&label_array, &label_count);
		break;
	case BT_FIELD_CLASS_TYPE_SIGNED_ENUMERATION:
		ret = bt_field_enumeration_signed_get_mapping_labels(field,
			&label_array, &label_count);
		break;
	default:
		bt_common_abort();
	}

	if (ret) {
		ret = -1;
		goto end;
	}

	bt_common_g_string_append(pretty->string, "( ");
	if (label_count != 0) {
		print_enum_value_label_array(pretty, label_count, label_array);
		goto print_container;
	}

	ret = print_enum_try_bit_flags(pretty, field, enumeration_field_class);

print_container:
	bt_common_g_string_append(pretty->string, " : container = ");
	ret = print_integer(pretty, field);
	if (ret != 0) {
		goto end;
	}
	bt_common_g_string_append(pretty->string, " )");
end:
	return ret;
}

static
int print_struct_field(struct pretty_component *pretty,
		const bt_field *_struct,
		const bt_field_class *struct_class,
		uint64_t i, bool print_names, uint64_t *nr_printed_fields)
{
	int ret = 0;
	const char *field_name;
	const bt_field *field = NULL;
	const bt_field_class_structure_member *member;

	field = bt_field_structure_borrow_member_field_by_index_const(_struct, i);
	if (!field) {
		ret = -1;
		goto end;
	}

	member = bt_field_class_structure_borrow_member_by_index_const(
		struct_class, i);
	field_name = bt_field_class_structure_member_get_name(member);

	if (*nr_printed_fields > 0) {
		bt_common_g_string_append(pretty->string, ", ");
	} else {
		bt_common_g_string_append(pretty->string, " ");
	}
	if (print_names) {
		print_field_name_equal(pretty, field_name);
	}
	ret = print_field(pretty, field, print_names);
	*nr_printed_fields += 1;

end:
	return ret;
}

static
int print_struct(struct pretty_component *pretty,
		const bt_field *_struct, bool print_names)
{
	int ret = 0;
	const bt_field_class *struct_class = NULL;
	uint64_t nr_fields, i, nr_printed_fields;

	struct_class = bt_field_borrow_class_const(_struct);
	if (!struct_class) {
		ret = -1;
		goto end;
	}

	nr_fields = bt_field_class_structure_get_member_count(struct_class);

	bt_common_g_string_append(pretty->string, "{");
	pretty->depth++;
	nr_printed_fields = 0;
	for (i = 0; i < nr_fields; i++) {
		ret = print_struct_field(pretty, _struct, struct_class, i,
				print_names, &nr_printed_fields);
		if (ret != 0) {
			goto end;
		}
	}
	pretty->depth--;
	bt_common_g_string_append(pretty->string, " }");

end:
	return ret;
}

static
int print_array_field(struct pretty_component *pretty,
		const bt_field *array, uint64_t i, bool print_names)
{
	const bt_field *field = NULL;

	if (i != 0) {
		bt_common_g_string_append(pretty->string, ", ");
	} else {
		bt_common_g_string_append(pretty->string, " ");
	}
	if (print_names) {
		bt_common_g_string_append_printf(pretty->string, "[%" PRIu64 "] = ", i);
	}

	field = bt_field_array_borrow_element_field_by_index_const(array, i);
	BT_ASSERT_DBG(field);
	return print_field(pretty, field, print_names);
}

static
int print_array(struct pretty_component *pretty,
		const bt_field *array, bool print_names)
{
	int ret = 0;
	const bt_field_class *array_class = NULL;
	uint64_t len;
	uint64_t i;

	array_class = bt_field_borrow_class_const(array);
	if (!array_class) {
		ret = -1;
		goto end;
	}
	len = bt_field_array_get_length(array);
	bt_common_g_string_append(pretty->string, "[");
	pretty->depth++;
	for (i = 0; i < len; i++) {
		ret = print_array_field(pretty, array, i, print_names);
		if (ret != 0) {
			goto end;
		}
	}
	pretty->depth--;
	bt_common_g_string_append(pretty->string, " ]");

end:
	return ret;
}

static
int print_sequence_field(struct pretty_component *pretty,
		const bt_field *seq, uint64_t i, bool print_names)
{
	const bt_field *field = NULL;

	if (i != 0) {
		bt_common_g_string_append(pretty->string, ", ");
	} else {
		bt_common_g_string_append(pretty->string, " ");
	}
	if (print_names) {
		bt_common_g_string_append_printf(pretty->string, "[%" PRIu64 "] = ", i);
	}

	field = bt_field_array_borrow_element_field_by_index_const(seq, i);
	BT_ASSERT_DBG(field);
	return print_field(pretty, field, print_names);
}

static
int print_sequence(struct pretty_component *pretty,
		const bt_field *seq, bool print_names)
{
	int ret = 0;
	uint64_t len;
	uint64_t i;

	len = bt_field_array_get_length(seq);
	bt_common_g_string_append(pretty->string, "[");

	pretty->depth++;
	for (i = 0; i < len; i++) {
		ret = print_sequence_field(pretty, seq, i, print_names);
		if (ret != 0) {
			goto end;
		}
	}
	pretty->depth--;
	bt_common_g_string_append(pretty->string, " ]");

end:
	return ret;
}

static
int print_option(struct pretty_component *pretty,
		const bt_field *option, bool print_names)
{
	int ret = 0;
	const bt_field *field = NULL;

	field = bt_field_option_borrow_field_const(option);
	if (field) {
		bt_common_g_string_append(pretty->string, "{ ");
		pretty->depth++;
		if (print_names) {
			// TODO: find tag's name using field path
			// print_field_name_equal(pretty, tag_choice);
		}
		ret = print_field(pretty, field, print_names);
		if (ret != 0) {
			goto end;
		}
		pretty->depth--;
		bt_common_g_string_append(pretty->string, " }");
	} else {
		bt_common_g_string_append(pretty->string, "<none>");
	}

end:
	return ret;
}

static
int print_variant(struct pretty_component *pretty,
		const bt_field *variant, bool print_names)
{
	int ret = 0;
	const bt_field *field = NULL;

	field = bt_field_variant_borrow_selected_option_field_const(variant);
	BT_ASSERT_DBG(field);
	bt_common_g_string_append(pretty->string, "{ ");
	pretty->depth++;
	if (print_names) {
		// TODO: find tag's name using field path
		// print_field_name_equal(pretty, tag_choice);
	}
	ret = print_field(pretty, field, print_names);
	if (ret != 0) {
		goto end;
	}
	pretty->depth--;
	bt_common_g_string_append(pretty->string, " }");

end:
	return ret;
}

static
int print_field(struct pretty_component *pretty,
		const bt_field *field, bool print_names)
{
	bt_field_class_type class_id;

	class_id = bt_field_get_class_type(field);
	if (class_id == BT_FIELD_CLASS_TYPE_BOOL) {
		bt_bool v;
		const char *text;

		v = bt_field_bool_get_value(field);
		if (pretty->use_colors) {
			bt_common_g_string_append(pretty->string, color_number_value);
		}
		if (v) {
			text = "true";
		} else {
			text = "false";
		}
		bt_common_g_string_append(pretty->string, text);
		if (pretty->use_colors) {
			bt_common_g_string_append(pretty->string, color_rst);
		}
		return 0;
	} else if (class_id == BT_FIELD_CLASS_TYPE_BIT_ARRAY) {
		uint64_t v = bt_field_bit_array_get_value_as_integer(field);

		if (pretty->use_colors) {
			bt_common_g_string_append(pretty->string,
				color_number_value);
		}
		bt_common_g_string_append_printf(pretty->string, "0x%" PRIX64,
			v);
		if (pretty->use_colors) {
			bt_common_g_string_append(pretty->string, color_rst);
		}
		return 0;
	} else if (bt_field_class_type_is(class_id,
			BT_FIELD_CLASS_TYPE_ENUMERATION)) {
		return print_enum(pretty, field);
	} else if (bt_field_class_type_is(class_id,
			BT_FIELD_CLASS_TYPE_INTEGER)) {
		return print_integer(pretty, field);
	} else if (bt_field_class_type_is(class_id,
			BT_FIELD_CLASS_TYPE_REAL)) {
		double v;

		if (class_id == BT_FIELD_CLASS_TYPE_SINGLE_PRECISION_REAL) {
			v = bt_field_real_single_precision_get_value(field);
		} else {
			v = bt_field_real_double_precision_get_value(field);
		}

		if (pretty->use_colors) {
			bt_common_g_string_append(pretty->string, color_number_value);
		}
		bt_common_g_string_append_printf(pretty->string, "%g", v);
		if (pretty->use_colors) {
			bt_common_g_string_append(pretty->string, color_rst);
		}
		return 0;
	} else if (class_id == BT_FIELD_CLASS_TYPE_STRING) {
		const char *str;

		str = bt_field_string_get_value(field);
		if (!str) {
			return -1;
		}

		if (pretty->use_colors) {
			bt_common_g_string_append(pretty->string, color_string_value);
		}
		print_escape_string(pretty, str);
		if (pretty->use_colors) {
			bt_common_g_string_append(pretty->string, color_rst);
		}
		return 0;
	} else if (class_id == BT_FIELD_CLASS_TYPE_STRUCTURE) {
		return print_struct(pretty, field, print_names);
	} else if (bt_field_class_type_is(class_id,
			BT_FIELD_CLASS_TYPE_OPTION)) {
		return print_option(pretty, field, print_names);
	} else if (bt_field_class_type_is(class_id,
			BT_FIELD_CLASS_TYPE_VARIANT)) {
		return print_variant(pretty, field, print_names);
	} else if (class_id == BT_FIELD_CLASS_TYPE_STATIC_ARRAY) {
		return print_array(pretty, field, print_names);
	} else if (bt_field_class_type_is(class_id,
			BT_FIELD_CLASS_TYPE_DYNAMIC_ARRAY)) {
		return print_sequence(pretty, field, print_names);
	} else {
		// TODO: log instead
		fprintf(pretty->err, "[error] Unknown type id: %d\n", (int) class_id);
		return -1;
	}
}

static
int print_stream_packet_context(struct pretty_component *pretty,
		const bt_event *event)
{
	int ret = 0;
	const bt_packet *packet = NULL;
	const bt_field *main_field = NULL;

	packet = bt_event_borrow_packet_const(event);
	if (!packet) {
		goto end;
	}
	main_field = bt_packet_borrow_context_field_const(packet);
	if (!main_field) {
		goto end;
	}
	if (!pretty->start_line) {
		bt_common_g_string_append(pretty->string, ", ");
	}
	pretty->start_line = false;
	if (pretty->options.print_scope_field_names) {
		print_name_equal(pretty, "stream.packet.context");
	}
	ret = print_field(pretty, main_field,
			pretty->options.print_context_field_names);

end:
	return ret;
}

static
int print_stream_event_context(struct pretty_component *pretty,
		const bt_event *event)
{
	int ret = 0;
	const bt_field *main_field = NULL;

	main_field = bt_event_borrow_common_context_field_const(event);
	if (!main_field) {
		goto end;
	}
	if (!pretty->start_line) {
		bt_common_g_string_append(pretty->string, ", ");
	}
	pretty->start_line = false;
	if (pretty->options.print_scope_field_names) {
		print_name_equal(pretty, "stream.event.context");
	}
	ret = print_field(pretty, main_field,
			pretty->options.print_context_field_names);

end:
	return ret;
}

static
int print_event_context(struct pretty_component *pretty,
		const bt_event *event)
{
	int ret = 0;
	const bt_field *main_field = NULL;

	main_field = bt_event_borrow_specific_context_field_const(event);
	if (!main_field) {
		goto end;
	}
	if (!pretty->start_line) {
		bt_common_g_string_append(pretty->string, ", ");
	}
	pretty->start_line = false;
	if (pretty->options.print_scope_field_names) {
		print_name_equal(pretty, "event.context");
	}
	ret = print_field(pretty, main_field,
			pretty->options.print_context_field_names);

end:
	return ret;
}

static
int print_event_payload(struct pretty_component *pretty,
		const bt_event *event)
{
	int ret = 0;
	const bt_field *main_field = NULL;

	main_field = bt_event_borrow_payload_field_const(event);
	if (!main_field) {
		goto end;
	}
	if (!pretty->start_line) {
		bt_common_g_string_append(pretty->string, ", ");
	}
	pretty->start_line = false;
	if (pretty->options.print_scope_field_names) {
		print_name_equal(pretty, "event.fields");
	}
	ret = print_field(pretty, main_field,
			pretty->options.print_payload_field_names);

end:
	return ret;
}

static
int flush_buf(FILE *stream, struct pretty_component *pretty)
{
	int ret = 0;

	if (pretty->string->len == 0) {
		goto end;
	}

	if (fwrite(pretty->string->str, pretty->string->len, 1, stream) != 1) {
		ret = -1;
	}

end:
	return ret;
}

BT_HIDDEN
int pretty_print_event(struct pretty_component *pretty,
		const bt_message *event_msg)
{
	int ret;
	const bt_event *event =
		bt_message_event_borrow_event_const(event_msg);

	BT_ASSERT_DBG(event);
	pretty->start_line = true;
	g_string_assign(pretty->string, "");
	ret = print_event_header(pretty, event_msg);
	if (ret != 0) {
		goto end;
	}

	ret = print_stream_packet_context(pretty, event);
	if (ret != 0) {
		goto end;
	}

	ret = print_stream_event_context(pretty, event);
	if (ret != 0) {
		goto end;
	}

	ret = print_event_context(pretty, event);
	if (ret != 0) {
		goto end;
	}

	ret = print_event_payload(pretty, event);
	if (ret != 0) {
		goto end;
	}

	bt_common_g_string_append_c(pretty->string, '\n');
	if (flush_buf(pretty->out, pretty)) {
		ret = -1;
		goto end;
	}

end:
	return ret;
}

static
int print_discarded_elements_msg(struct pretty_component *pretty,
		const bt_stream *stream,
		const bt_clock_snapshot *begin_clock_snapshot,
		const bt_clock_snapshot *end_clock_snapshot,
		uint64_t count, const char *elem_type)
{
	int ret = 0;
	const bt_stream_class *stream_class = NULL;
	const bt_trace *trace = NULL;
	const char *stream_name;
	const char *trace_name;
	bt_uuid trace_uuid;
	int64_t stream_class_id;
	int64_t stream_id;
	const char *init_msg;

	/* Stream name */
	stream_name = bt_stream_get_name(stream);
	if (!stream_name) {
		stream_name = "(unknown)";
	}

	/* Stream class ID */
	stream_class = bt_stream_borrow_class_const(stream);
	BT_ASSERT(stream_class);
	stream_class_id = bt_stream_class_get_id(stream_class);

	/* Stream ID */
	stream_id = bt_stream_get_id(stream);

	/* Trace name */
	trace = bt_stream_borrow_trace_const(stream);
	BT_ASSERT(trace);
	trace_name = bt_trace_get_name(trace);
	if (!trace_name) {
		trace_name = "(unknown)";
	}

	/* Trace UUID */
	trace_uuid = bt_trace_get_uuid(trace);

	/* Format message */
	g_string_assign(pretty->string, "");

	if (count == UINT64_C(-1)) {
		init_msg = "Tracer may have discarded";
	} else {
		init_msg = "Tracer discarded";
	}

	bt_common_g_string_append_printf(pretty->string,
		"%s%sWARNING%s%s: %s ",
		bt_common_color_fg_yellow(),
		bt_common_color_bold(),
		bt_common_color_reset(),
		bt_common_color_fg_yellow(), init_msg);

	if (count == UINT64_C(-1)) {
		bt_common_g_string_append_printf(pretty->string, "%ss", elem_type);
	} else {
		bt_common_g_string_append_printf(pretty->string,
			"%" PRIu64 " %s%s", count, elem_type,
			count == 1 ? "" : "s");
	}

	bt_common_g_string_append_c(pretty->string, ' ');

	if (begin_clock_snapshot && end_clock_snapshot) {
		bt_common_g_string_append(pretty->string, "between [");
		print_timestamp_wall(pretty, begin_clock_snapshot, false);
		bt_common_g_string_append(pretty->string, "] and [");
		print_timestamp_wall(pretty, end_clock_snapshot, false);
		bt_common_g_string_append(pretty->string, "]");
	} else {
		bt_common_g_string_append(pretty->string, "(unknown time range)");
	}

	bt_common_g_string_append_printf(pretty->string, " in trace \"%s\" ", trace_name);

	if (trace_uuid) {
		bt_common_g_string_append_printf(pretty->string,
			"(UUID: " BT_UUID_FMT ") ",
			BT_UUID_FMT_VALUES(trace_uuid));
	} else {
		bt_common_g_string_append(pretty->string, "(no UUID) ");
	}

	bt_common_g_string_append_printf(pretty->string,
		"within stream \"%s\" (stream class ID: %" PRIu64 ", ",
		stream_name, stream_class_id);

	if (stream_id >= 0) {
		bt_common_g_string_append_printf(pretty->string,
			"stream ID: %" PRIu64, stream_id);
	} else {
		bt_common_g_string_append(pretty->string, "no stream ID");
	}

	bt_common_g_string_append_printf(pretty->string, ").%s\n",
		bt_common_color_reset());

	/*
	 * Print to standard error stream to remain backward compatible
	 * with Babeltrace 1.
	 */
	if (flush_buf(stderr, pretty)) {
		ret = -1;
	}

	return ret;
}

BT_HIDDEN
int pretty_print_discarded_items(struct pretty_component *pretty,
		const bt_message *msg)
{
	const bt_clock_snapshot *begin = NULL;
	const bt_clock_snapshot *end = NULL;
	const bt_stream *stream;
	const bt_stream_class *stream_class;
	uint64_t count = UINT64_C(-1);
	const char *elem_type;

	switch (bt_message_get_type(msg)) {
	case BT_MESSAGE_TYPE_DISCARDED_EVENTS:
		stream = bt_message_discarded_events_borrow_stream_const(msg);

		if (bt_message_discarded_events_get_count(msg, &count) ==
				BT_PROPERTY_AVAILABILITY_NOT_AVAILABLE) {
			count = UINT64_C(-1);
		}

		elem_type = "event";
		break;
	case BT_MESSAGE_TYPE_DISCARDED_PACKETS:
		stream = bt_message_discarded_packets_borrow_stream_const(msg);

		if (bt_message_discarded_packets_get_count(msg, &count) ==
				BT_PROPERTY_AVAILABILITY_NOT_AVAILABLE) {
			count = UINT64_C(-1);
		}

		elem_type = "packet";
		break;
	default:
		bt_common_abort();
	}

	BT_ASSERT(stream);
	stream_class = bt_stream_borrow_class_const(stream);

	switch (bt_message_get_type(msg)) {
	case BT_MESSAGE_TYPE_DISCARDED_EVENTS:
		if (bt_stream_class_discarded_events_have_default_clock_snapshots(
				stream_class)) {
			begin = bt_message_discarded_events_borrow_beginning_default_clock_snapshot_const(
				msg);
			end = bt_message_discarded_events_borrow_end_default_clock_snapshot_const(
				msg);
		}

		break;
	case BT_MESSAGE_TYPE_DISCARDED_PACKETS:
		if (bt_stream_class_discarded_packets_have_default_clock_snapshots(
				stream_class)) {
			begin = bt_message_discarded_packets_borrow_beginning_default_clock_snapshot_const(
				msg);
			end = bt_message_discarded_packets_borrow_end_default_clock_snapshot_const(
				msg);
		}

		break;
	default:
		bt_common_abort();
	}

	print_discarded_elements_msg(pretty, stream, begin, end,
		count, elem_type);
	return 0;
}

BT_HIDDEN
void pretty_print_init(void)
{
	strcpy(color_name, bt_common_color_bold());
	strcpy(color_field_name, bt_common_color_fg_cyan());
	strcpy(color_rst, bt_common_color_reset());
	strcpy(color_string_value, bt_common_color_bold());
	strcpy(color_number_value, bt_common_color_bold());
	strcpy(color_enum_mapping_name, bt_common_color_bold());
	strcpy(color_unknown, bt_common_color_bold());
	strcat(color_unknown, bt_common_color_fg_bright_red());
	strcpy(color_event_name, bt_common_color_bold());
	strcat(color_event_name, bt_common_color_fg_bright_magenta());
	strcpy(color_timestamp, bt_common_color_bold());
	strcat(color_timestamp, bt_common_color_fg_bright_yellow());
}
