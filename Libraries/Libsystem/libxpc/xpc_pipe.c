/*
 * Compatibility stubs for private xpc_pipe APIs using regular unix pipes
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <mach/mach.h>
#include <mach/message.h>

#include "xpc/launchd.h"

struct xpc_pipe_state {
    xpc_pipe_t handle;
    int read_fd;
    int write_fd;
    uint64_t flags;
    int valid;
    struct xpc_pipe_state *next;
};

static pthread_mutex_t s_pipe_lock = PTHREAD_MUTEX_INITIALIZER;
static struct xpc_pipe_state *s_pipes;

static struct xpc_pipe_state *
xpc_pipe_state_find_nolock(xpc_pipe_t handle)
{
    struct xpc_pipe_state *it = s_pipes;
    while (it != NULL) {
        if (it->handle == handle)
            return it;
        it = it->next;
    }
    return NULL;
}

xpc_pipe_t
xpc_pipe_create(const char *name, uint64_t flags)
{
    int fds[2];
    struct xpc_pipe_state *state;
    xpc_pipe_t handle;

    (void)name;

    if (pipe(fds) != 0)
        return NULL;

    handle = xpc_dictionary_create(NULL, NULL, 0);
    if (handle == NULL) {
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }

    state = calloc(1, sizeof(*state));
    if (state == NULL) {
        close(fds[0]);
        close(fds[1]);
        xpc_release(handle);
        return NULL;
    }

    state->handle = handle;
    state->read_fd = fds[0];
    state->write_fd = fds[1];
    state->flags = flags;
    state->valid = 1;

    pthread_mutex_lock(&s_pipe_lock);
    state->next = s_pipes;
    s_pipes = state;
    pthread_mutex_unlock(&s_pipe_lock);

    return handle;
}

void
xpc_pipe_invalidate(xpc_pipe_t pipe)
{
    pthread_mutex_lock(&s_pipe_lock);
    struct xpc_pipe_state *prev = NULL;
    struct xpc_pipe_state *state = s_pipes;
    while (state != NULL) {
        if (state->handle == pipe)
            break;
        prev = state;
        state = state->next;
    }
    if (state != NULL) {
        if (prev == NULL)
            s_pipes = state->next;
        else
            prev->next = state->next;

        if (state->valid) {
            state->valid = 0;
            close(state->read_fd);
            close(state->write_fd);
        }
        free(state);
    }
    pthread_mutex_unlock(&s_pipe_lock);
}

int
xpc_pipe_routine(xpc_pipe_t pipe, xpc_object_t msg, xpc_object_t *reply)
{
    struct xpc_pipe_state *state;
    char byte = 0;
    ssize_t n;

    if (reply != NULL)
        *reply = NULL;

    if (pipe == NULL || msg == NULL)
        return EINVAL;

    pthread_mutex_lock(&s_pipe_lock);
    state = xpc_pipe_state_find_nolock(pipe);
    if (state == NULL || !state->valid) {
        pthread_mutex_unlock(&s_pipe_lock);
        return EPIPE;
    }
    int write_fd = state->write_fd;
    int read_fd = state->read_fd;
    pthread_mutex_unlock(&s_pipe_lock);

    /* Check for transport failure */
    n = write(write_fd, &byte, 1);
    if (n != 1)
        return EPIPE;
    n = read(read_fd, &byte, 1);
    if (n != 1)
        return EPIPE;

    if (reply != NULL)
        *reply = xpc_dictionary_create(NULL, NULL, 0);

    return 0;
}

/*
 * launchd's event loop (runtime.c:1155) receives MIG messages on a port set
 * and lets launchd_mig_demux dispatch them.  The private XPC wire protocol is
 * not used on this port set in single-user mode, so handle MIG only; a
 * message the demux does not claim is reported as EINVAL.  A return of 1
 * means "no message" (timeout), which launchd treats as a normal idle pass.
 */
int
xpc_pipe_try_receive(mach_port_t ipc_port_set, xpc_object_t *request,
    mach_port_t *recvp, boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *),
    mach_msg_size_t msg_size, int flags)
{
	mach_msg_header_t *req;
	mach_msg_header_t *rep;
	mach_msg_size_t rcv_size;
	mach_msg_return_t mr;
	int ret = EINVAL;

	(void)flags;

	if (request == NULL || recvp == NULL || demux == NULL)
		return EINVAL;

	*request = NULL;
	*recvp = MACH_PORT_NULL;

	if (msg_size == 0)
		msg_size = 4096;
	rcv_size = msg_size + sizeof(mach_msg_audit_trailer_t);

	req = calloc(1, rcv_size);
	rep = calloc(1, rcv_size);
	if (req == NULL || rep == NULL) {
		free(req);
		free(rep);
		return ENOMEM;
	}

	mr = mach_msg(req, MACH_RCV_MSG | MACH_RCV_TIMEOUT |
	    MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT) |
	    MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0),
	    0, rcv_size, ipc_port_set, 1000, MACH_PORT_NULL);

	if (mr == MACH_RCV_TIMED_OUT || mr == MACH_RCV_INTERRUPTED) {
		ret = 1;
		goto out;
	}
	if (mr != MACH_MSG_SUCCESS) {
		ret = EINVAL;
		goto out;
	}

	if (demux(req, rep)) {
		mr = mach_msg(rep, MACH_SEND_MSG, rep->msgh_size, 0,
		    MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
		ret = (mr == MACH_MSG_SUCCESS) ? 0 : EINVAL;
	} else {
		ret = EINVAL;
	}

out:
	free(req);
	free(rep);
	return ret;
}

/*
 * FIXME: replies for the pipe-stub transport are not implemented; no XPC
 * request reaches xpc_pipe_try_receive() yet.  EPIPE makes launchd skip its
 * error log path (runtime.c:1180).
 */
int
xpc_pipe_routine_reply(xpc_object_t reply)
{
	(void)reply;
	return EPIPE;
}

/*
 * FIXME: real libxpc wakes a thread blocked on a bootstrap request; the
 * pipe-stub transport has no such waiter, so succeed without sending.
 */
kern_return_t
xpc_call_wakeup(mach_port_t port, int error)
{
	(void)port;
	(void)error;
	return KERN_SUCCESS;
}


