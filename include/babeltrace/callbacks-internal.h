#ifndef _BABELTRACE_CALLBACKS_INTERNAL_H
#define _BABELTRACE_CALLBACKS_INTERNAL_H

/*
 * BabelTrace
 *
 * Internal callbacks header
 *
 * Copyright 2010-2011 EfficiOS Inc. and Linux Foundation
 *
 * Author: Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
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
 */

struct bt_callback {
	int prio;		/* Callback order priority. Lower first. Dynamically assigned from dependency graph. */
	void *private_data;
	int flags;
	struct bt_dependencies *depends;
	struct bt_dependencies *weak_depends;
	struct bt_dependencies *provides;
	enum bt_cb_ret (*callback)(struct bt_ctf_data *ctf_data,
				   void *private_data);
};

struct bt_callback_chain {
	GArray *callback;	/* Array of struct bt_callback, ordered by priority */
};

/*
 * per id callbacks need to be per stream class because event ID vs
 * event name mapping can vary from stream to stream.
 */
struct bt_stream_callbacks {
	GArray *per_id_callbacks;	/* Array of struct bt_callback_chain */
};

struct bt_dependencies {
	GArray *deps;			/* Array of GQuarks */
	int refcount;			/* free when decremented to 0 */
};

void process_callbacks(struct bt_iter *iter,
	struct ctf_stream *stream);

#endif /* _BABELTRACE_CALLBACKS_INTERNAL_H */
