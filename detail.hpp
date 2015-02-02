//
//  detail.hpp
//  asio_pipe_transport
//
//  Created by Peter Thorson on 2015-02-02.
//  Copyright (c) 2015 Neomantra. All rights reserved.
//

#ifndef _ASIO_PIPE_TRANSPORT_DETAIL_
#define _ASIO_PIPE_TRANSPORT_DETAIL_

#include "error.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/system/error_code.hpp>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

namespace asio_pipe_transport {
namespace detail {

/// Translate errno to `boost::system::error_code`
/**
 * Returns the appropriate associated `boost::system::error_code` to the `error`
 * parameter. Note: presently only works with the subset of errno values that 
 * are typically returned by the system calls used by asio-pipe-transport. 
 * Unrecognized values will be returned as 
 * `asio_pipe_transport::error::unknown_system_error`.
 *
 * @param error The error number to translate
 * @return The `system::error_code` that corresponds to the value of errno
 */
static boost::system::error_code process_errno(int error) {
    namespace errc = boost::system::errc;
    
    // separate case because EAGAIN and EWOULDBLOCK sometimes share a value
    // which confuses the switch
    if (error == EAGAIN) {
        return errc::make_error_code(errc::operation_would_block);
    }
    
    switch(error) {
        case EACCES:
            return errc::make_error_code(errc::permission_denied);
        case EWOULDBLOCK:
            return errc::make_error_code(errc::operation_would_block);
        case EBADF:
            return errc::make_error_code(errc::bad_file_descriptor);
        case ECONNREFUSED:
                return errc::make_error_code(errc::connection_refused);
        case ECONNRESET:
            return errc::make_error_code(errc::connection_reset);
        case EDESTADDRREQ:
            return errc::make_error_code(errc::destination_address_required);
        case EFAULT:
            return errc::make_error_code(errc::bad_address);
        case EINTR:
            return errc::make_error_code(errc::interrupted);
        case EINVAL:
            return errc::make_error_code(errc::invalid_argument);
        case EISCONN:
            return errc::make_error_code(errc::already_connected);
        case EMSGSIZE:
            return errc::make_error_code(errc::message_size);
        case ENOBUFS:
            return errc::make_error_code(errc::no_buffer_space);
        case ENOMEM:
            return errc::make_error_code(errc::not_enough_memory);
        case ENOTCONN:
            return errc::make_error_code(errc::not_connected);
        case ENOTSOCK:
            return errc::make_error_code(errc::not_a_socket);
        case EOPNOTSUPP:
            return errc::make_error_code(errc::operation_not_supported);
        case EPIPE:
            return errc::make_error_code(errc::broken_pipe);
        case EMFILE:
                return errc::make_error_code(errc::too_many_files_open);
        case ENFILE:
            return errc::make_error_code(errc::too_many_files_open_in_system);
        default:
            return error::make_error_code(error::unknown_system_error);
    }
}

/// Translate errno to `boost::system::error_code`
/**
 * Inspects errno following a system call and returns the appropriate associated
 * `boost::system::error_code`. Note: presently only works with the subset of
 * errno values that are typically returned by the system calls used by
 * asio-pipe-transport. Unrecognized values will be returned as
 * `asio_pipe_transport::error::unknown_system_error`.
 *
 * @return The `system::error_code` that corresponds to the value of global errno
 */
static boost::system::error_code process_errno() {
    return process_errno(errno);
}

/// Wrapper for the posix msghdr struct and other associated data
class recv_msghdr {
public:
    typedef boost::shared_ptr<recv_msghdr> ptr;
    
    recv_msghdr()
      : ctrl(NULL)
    {
        memset(&msg, 0, sizeof(struct msghdr));
        memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

        iov[0].iov_base = data;
        iov[0].iov_len = sizeof(data);

        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        msg.msg_controllen =  CMSG_SPACE(sizeof(int));
        msg.msg_control = ctrl_buf;
    }

    struct msghdr msg;
    struct iovec iov[1];
    struct cmsghdr *ctrl;
    
    char ctrl_buf[CMSG_SPACE(sizeof(int))];
    char data[1];
};

/// RAII wrapper for a pair of POSIX pipes
/**
 * Note: Error handling for closing file descriptors in the POSIX standard is
 * not well defined. This implementation assumes that the `close()` system call
 * will close the file descriptor in the case that it was interrupted by a
 * signal. This is the case on Linux and BSD-like systems but may not be true on
 * other POSIX systems.
 */
class pipe_pair {
public:
    typedef boost::shared_ptr<pipe_pair> ptr;

    int s2c_pipe[2];
    int c2s_pipe[2];
    char ack_data[3];
    
    pipe_pair() {
        if (pipe(s2c_pipe) == -1) {
            throw detail::process_errno();
        }
        if (pipe(c2s_pipe) == -1) {
            int pipe_err = errno;
            ::close(s2c_pipe[0]);
            ::close(s2c_pipe[1]);
            throw detail::process_errno(pipe_err);
        }
        
        memset(ack_data, 0, 3);
    }
    
    ~pipe_pair() {
        // Not checking close errno here as there aren't really any failure
        // modes that we care about or could do anything about.
        //
        // EBADF: almost certainly ignore all the time
        // EINTR (signal interrupt): On linux, BSD we don't need to retry. POSIX
        //       does not guarantee this behavior though and other operating
        //       systems may not work this way.
        // EIO (IO error): unclear if this can actually be triggered via pipes
        ::close(s2c_pipe[0]);
        ::close(s2c_pipe[1]);
        ::close(c2s_pipe[0]);
        ::close(c2s_pipe[1]);
    }
};

/// Serialize and send a file descriptor over a socket
/**
 * @param socket The socket descriptor to write to
 * @param fd The file descriptor to send
 */
static void send_fd(int socket, int fd) {
    struct msghdr msg;
    struct iovec iov[1];
    struct cmsghdr *ctrl = NULL;
    
    char ctrl_buf[CMSG_SPACE(sizeof(int))];
    char data[1];
    ssize_t res;
    
    memset(&msg, 0, sizeof(struct msghdr));
    memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));
    
    data[0] = ' ';
    iov[0].iov_base = data;
    iov[0].iov_len = sizeof(data);
    
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_controllen =  CMSG_SPACE(sizeof(int));
    msg.msg_control = ctrl_buf;
    
    ctrl = CMSG_FIRSTHDR(&msg);
    ctrl->cmsg_level = SOL_SOCKET;
    ctrl->cmsg_type = SCM_RIGHTS;
    ctrl->cmsg_len = CMSG_LEN(sizeof(int));
    
    *(reinterpret_cast<int *>(CMSG_DATA(ctrl))) = fd;
    
    res = sendmsg(socket, &msg, 0);
    
    if (res == -1) {
        throw detail::process_errno();
    }
}

/// Receive and unserialize a file descriptor over a socket
/**
 * Depending on the socket's blocking mode, this function may return an error
 * indicator (-1) and set ec to `errc::operation_would_block` if there are no
 * bytes to be read in the socket. In this case the recv_fd function should be
 * called again when more data is available until it returns cleanly or
 * encounters a more serious error.
 *
 * @param socket The socket descriptor to read from
 * @param msg_ptr A pointer to the recv_msghdr object to write into
 * @param ec A status code indicating the error that ocurred, if any. Set if the
 *        function returns -1
 * @return The received file descriptor or -1 in the case of an error.
 */
static int recv_fd(int socket, recv_msghdr::ptr msg_ptr, boost::system::error_code & ec) {
    ec = boost::system::error_code();
    ssize_t res;
    
    res = recvmsg(socket, &msg_ptr->msg, 0);
    
    if(res < 0) {
        ec = detail::process_errno();
        return -1;
    }
    if (res == 0) {
        // this indicates that the connection was cleanly closed.
        // we aren't expecting that right now though.
        ec = error::make_error_code(error::early_close);
        return -1;
    }
    
    for (msg_ptr->ctrl = CMSG_FIRSTHDR(&msg_ptr->msg); msg_ptr->ctrl != NULL; msg_ptr->ctrl = CMSG_NXTHDR(&msg_ptr->msg,msg_ptr->ctrl)) {
        if( (msg_ptr->ctrl->cmsg_level == SOL_SOCKET) && (msg_ptr->ctrl->cmsg_type == SCM_RIGHTS) ) {
            return *(reinterpret_cast<int *>(CMSG_DATA(msg_ptr->ctrl)));
        }
    }
    
    return -1;
}

} // namespace detail
} // namespace asio_pipe_transport

#endif /* defined(_ASIO_PIPE_TRANSPORT_ENDPOINT_) */
