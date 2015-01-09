#ifndef DRBD_TRANSPORT_H
#define DRBD_TRANSPORT_H

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/socket.h>

/* Whenever touch this file in a non-trivial way, increase the
   DRBD_TRANSPORT_API_VERSION
   So that transport compiled against an older version of this
   header will no longer load in a module that assumes a newer
   version. */
#define DRBD_TRANSPORT_API_VERSION 3

/* MSG_MSG_DONTROUTE and MSG_PROBE are not used by DRBD. I.e.
   we can reuse these flags for our purposes */
#define CALLER_BUFFER  MSG_DONTROUTE
#define GROW_BUFFER    MSG_PROBE


struct drbd_resource;
struct drbd_connection;
struct drbd_peer_device;

enum drbd_stream {
	DATA_STREAM,
	CONTROL_STREAM
};

enum drbd_tr_hints {
	CORK,
	UNCORK,
	NODELAY,
	NOSPACE,
	QUICKACK
};

enum { /* bits in the flags word */
	NET_CONGESTED,		/* The data socket is congested */
	RESOLVE_CONFLICTS,	/* Set on one node, cleared on the peer! */
};

enum drbd_tr_free_op {
	CLOSE_CONNECTION,
	DESTROY_TRANSPORT
};

/* Each transport implementation should embed a struct drbd_transport
   into it's instance data structure.
   The transport implementation should only access the connection
   only for reading (connection config, etc...) */
struct drbd_transport {
	struct drbd_transport_ops *ops;
	struct drbd_connection *connection;

	/* These members are intended to be updated by the transport: */
	unsigned int ko_count;
	unsigned long flags;
};

struct drbd_transport_stats {
	int unread_received;
	int unacked_send;
	int send_buffer_size;
	int send_buffer_used;
};

struct drbd_transport_ops {
	void (*free)(struct drbd_transport *, enum drbd_tr_free_op free_op);
	int (*connect)(struct drbd_transport *);
	int (*send)(struct drbd_transport *, enum drbd_stream, void *buf, size_t size, unsigned msg_flags);

/**
 * recv() - Receive data via the transport
 * @transport:	The transport to use
 * @stream:	The stream within the transport to use. Ether DATA_STREAM or CONTROL_STREAM
 * @buf:	The function will place here the pointer to the data area
 * @size:	Number of byte to receive
 * @msg_flags:	Bitmask of CALLER_BUFFER, GROW_BUFFER and MSG_DONTWAIT
 *
 * recv() returns the requests data in a buffer (owned by the transport).
 * You may pass MSG_DONTWAIT as flags.  Usually with the next call to recv()
 * or recv_pages() on the same stream, the buffer may no longer be accessed
 * by the caller. I.e. it is reclaimed by the transport.
 *
 * If the transport was not capable of fulfilling the complete "wish" of the
 * caller (that means it returned a smaller size that size), the caller may
 * call recv() again with the flag GROW_BUFFER, and *buf as returned by the
 * previous call.
 * Note1: This can happen if MSG_DONTWAIT was used, or if a receive timeout
 *	was we with set_rcvtimeo().
 * Note2: recv() is free to re-locate the buffer in such a call. I.e. to
 *	modify *buf. Then it copies the content received so far to the new
 *	memory location.
 *
 * Last not least the caller may also pass an arbitrary pointer in *buf with
 * the CALLER_BUFFER flag. This is expected to be used for small amounts
 * of data only
 *
 * Upon success the function returns the bytes read. Upon error the return
 * code is negative. A 0 indicates that the socket was closed by the remote
 * side.
 */
	int (*recv)(struct drbd_transport *, enum drbd_stream, void **buf, size_t size, int flags);

/**
 * recv_pages() - Receive bulk data via the transport's DATA_STREAM
 * @peer_device: Identify the transport and the device
 * @page:	Here recv_pages() will place the pointer to the first page
 * @size:	Number of bytes to receive
 *
 * recv_pages() will return the requested amount of data from DATA_STREAM,
 * and place it into pages allocated with drbd_alloc_pages().
 *
 * Upon success the function returns 0. Upon error the function returns a
 * negative value
 */
	int (*recv_pages)(struct drbd_peer_device *, struct page **page, size_t size);

	void (*stats)(struct drbd_transport *, struct drbd_transport_stats *stats);
	void (*set_rcvtimeo)(struct drbd_transport *, enum drbd_stream, long timeout);
	long (*get_rcvtimeo)(struct drbd_transport *, enum drbd_stream);
	int (*send_page)(struct drbd_transport *, enum drbd_stream, struct page *,
			 int offset, size_t size, unsigned msg_flags);
	bool (*stream_ok)(struct drbd_transport *, enum drbd_stream);
	bool (*hint)(struct drbd_transport *, enum drbd_stream, enum drbd_tr_hints hint);
};

struct drbd_transport_class {
	const char *name;
	struct drbd_transport *(*create)(struct drbd_connection *);
	struct list_head list;
};


/* An "abstract base class" for transport implementations. I.e. it
   should be embedded into a transport specific representation of a
   listening "socket" */
struct drbd_listener {
	struct kref kref;
	struct drbd_resource *resource;
	struct list_head list; /* link for resource->listeners */
	struct list_head waiters; /* list head for waiter structs*/
	int pending_accepts;
	struct sockaddr_storage listen_addr;
	void (*destroy)(struct drbd_listener *);
};

/* This represents a drbd receiver thread that is waiting for an
   incoming connection attempt. Again, should be embedded into a
   implementation object */
struct drbd_waiter {
	struct drbd_connection *connection;
	wait_queue_head_t wait;
	struct list_head list;
	struct drbd_listener *listener;
};

extern int drbd_register_transport_class(struct drbd_transport_class *transport_class, int api_version);
extern void drbd_unregister_transport_class(struct drbd_transport_class *transport_class);
extern struct drbd_transport *drbd_create_transport(const char *name, struct drbd_connection *);

extern int drbd_get_listener(struct drbd_waiter *waiter,
			     int (*create_fn)(struct drbd_connection *, struct drbd_listener **));
extern void drbd_put_listener(struct drbd_waiter *waiter);
extern struct drbd_waiter *drbd_find_waiter_by_addr(struct drbd_listener *, struct sockaddr_storage *);

#endif
