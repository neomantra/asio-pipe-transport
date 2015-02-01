//
//  endpoint.h
//  asio_pipe_transport
//
//  Created by Peter Thorson on 2015-01-07.
//  Copyright (c) 2015 Neomantra. All rights reserved.
//

#ifndef _ASIO_PIPE_TRANSPORT_ENDPOINT_
#define _ASIO_PIPE_TRANSPORT_ENDPOINT_

#include "error.hpp"

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <iostream>

#include <string>

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

/// Class that models one of the endpoints of a pipe transport connection
/**
 *
 * General Usage:
 * The pipe transport consists of an acceptor and a pair of endpoints. The
 * acceptor binds to and listens for new connections on a Unix domain socket.
 * It's `accept` and `async_accept` methods take an endpoint and perform the
 * connect it to a second endpoint that connects to the acceptor using the
 * `endpoint::connect` method.
 *
 * Once connected the acceptor generates a pair of anonymous POSIX pipes and
 * exchanges the pipe descriptors with the connecting endpoint. Once this
 * exchange is complete and acknowledged the underlying socket is closed.
 *
 * At this point accept/connect will return and both the server and client
 * endpoints will be ready for reading or writing. All further communication
 * happens over the pipes. The connected endpoint implements the Asio 
 * SyncReadStream, SyncWriteStream, AsyncReadStream, and AsyncWriteStream
 * interfaces so any of the usual asio methods such as boost::asio::read,
 * boost::asio::write, boost::asio::async_read, boost::asio::async_write and 
 * similar methods will work when passing the asio_pipe_transport::endpoint as
 * the stream to read/write.
 */
class endpoint {
public:
    typedef boost::shared_ptr<boost::asio::local::stream_protocol::socket> socket_ptr;

    /// Construct a pipe transport endpoint
    /**
     * Construct a pipe transport endpoint and register it with the io_service
     * that will be used to handle read/write operations.
     *
     * The newly constructed endpoint will be in an uninitialized state suitable
     * for either passing to the `accept` method of an acceptor or calling the
     * endpoint's `connect` method to establish a connection. Once established
     * using one of these two options, read and write operations may be done on
     * the stream using the asio stream I/O free functions.
     *
     * @param service The io_service this endpoint will use
     */
    endpoint (boost::asio::io_service & service)
        : m_io_service(service)
        , m_input(service)
        , m_output(service)
    {}

    // TODO: copy constructor
    // TODO: copy assignment operator
    // TODO: move constructor
    // TODO: move assignment operator

    /// Establish a pipe connection by connecting to a pipe transport acceptor
    /**
     * Connect to a pipe transport acceptor listening at the given Unix domain
     * socket path. Once connected, a pair of pipes will be allocated and
     * exchanged. Once this is successful, the socket will be closed and future
     * reads and writes will occur using these pipes.
     *
     * @param path The path to a Unix domain socket to connect to
     * @return A status code indicating the error that ocurred, if any
     */
    boost::system::error_code connect(std::string path) {
        boost::system::error_code ec;
        boost::system::error_code cec;
        int pipe;
        
        boost::asio::local::stream_protocol::endpoint ep(path);
        boost::asio::local::stream_protocol::socket socket(m_io_service);
        
        socket.connect(ep,ec);
        if (ec) {
            return ec;
        }
        
        // receive s2c pipe endpoint
        detail::recv_msghdr::ptr recv_msghdr(new class detail::recv_msghdr());
        pipe = detail::recv_fd(socket.native_handle(), recv_msghdr, ec);
        if (ec) {
            return ec;
        }
        m_input.assign(pipe);

        recv_msghdr.reset(new class detail::recv_msghdr());

        // receive c2s pipe endpoint
        pipe = detail::recv_fd(socket.native_handle(), recv_msghdr, ec);
        if (ec) {
            // TODO: check cec? Boost docs say that even if there is an error
            // the descriptor will be closed. All we really care about is that
            // the descriptor is cleaned up if it wasn't already.
            m_input.close(cec);
            return ec;
        }
        m_output.assign(pipe);
        
        // send ack
        boost::asio::write(socket, boost::asio::buffer("ack", 3), ec);
        if (ec) {
            // TODO: check cec? Boost docs say that even if there is an error
            // the descriptor will be closed. All we really care about is that
            // the descriptor is cleaned up if it wasn't already.
            m_input.close(cec);
            m_output.close(cec);
            return error::make_error_code(error::ack_failed);
        }
        
        return boost::system::error_code();
    }

    /// Start an asynchronous connect for a new pipe transport connection
    /**
     * This function is used to asynchronously initiate a pipe transport
     * connection. The function call always returns immediately.
     *
     * Connect to a pipe transport acceptor listening at the given Unix domain
     * socket path. Once connected, a pair of pipes will be allocated and
     * exchanged. Once this is successful, the socket will be closed and future
     * reads and writes will occur using these pipes.
     *
     * A sequence of asyncronous actions will be started that connect to a pipe
     * transport acceptor listening at the given Unix domain socket path. Once
     * connected, a pair of pipes will be allocated and exchanged. Once this is 
     * successful, the socket will be closed and future reads and writes will
     * occur using these pipes.
     *
     * When `handler` is called without error, `endpoint` will be in a connected
     * state ready to read and write data over pipes to its associated remote
     * endpoint.
     *
     * @param path The path to a Unix domain socket to connect to
     * @param handler The handler to be called when the connect operation
     *        completes. All typical rules for an Asio asyncronous handler apply
     *        please consult Asio documentation for more details.
     */
    template <typename ConnectHandler>
    void async_connect(std::string path, ConnectHandler handler) {
        boost::asio::local::stream_protocol::endpoint ep(path);
        socket_ptr socket(new boost::asio::local::stream_protocol::socket(m_io_service));

        socket->async_connect(ep,boost::bind(
            &endpoint::handle_connect<ConnectHandler>,
            this,socket,handler,::_1
        ));
    }

    /// Semi-private helper method that initiates the pipe exchange
    /**
     * TODO: should this be private and acceptor be labeled friend?
     *
     * @param socket The socket to use to exchange pipe information
     * @return A status code indicating the error that ocurred, if any
     */
    template <typename Socket>
    boost::system::error_code init_pipes(Socket & socket) {
        boost::system::error_code ec;
        
        try {
            detail::pipe_pair pp;
            
            // Assign copies of the server side pipe endpoints to local stream
            // descriptors
            m_input.assign(::dup(pp.c2s_pipe[0]));
            m_output.assign(::dup(pp.s2c_pipe[1]));
            
            // Send the remaining two pipe endpoints over the socket to the client.
            detail::send_fd(socket.native_handle(), pp.s2c_pipe[0]);
            detail::send_fd(socket.native_handle(), pp.c2s_pipe[1]);
            
            // wait for ack
            size_t read = boost::asio::read(socket, boost::asio::buffer(pp.ack_data, 3), ec);

            if (ec || read != 3 || strncmp(pp.ack_data, "ack", 3) != 0) {
                return error::make_error_code(error::ack_failed);
            }
            
            return boost::system::error_code();
        } catch(boost::system::error_code & ec) {
            return ec;
        }
    }

    /// Semi-private helper method that asynchronously initiates the pipe exchange
    /**
     * TODO: should this be private and acceptor be labeled friend?
     *
     * @param socket The socket to use to exchange pipe information
     * @return A status code indicating the error that ocurred, if any
     */
    template <typename AcceptHandler>
    void async_init_pipes(socket_ptr socket, AcceptHandler handler) {
        try {
            detail::pipe_pair::ptr pp(new detail::pipe_pair());
                    
            // Assign copies of the server side pipe endpoints to local stream
            // descriptors
            m_input.assign(::dup(pp->c2s_pipe[0]));
            m_output.assign(::dup(pp->s2c_pipe[1]));
            
            // TODO: will send_fd block? Will it ever return EAGAIN/Would Block?
            // Send the remaining two pipe endpoints over the socket to the
            // client.
            detail::send_fd(socket->native_handle(), pp->s2c_pipe[0]);
            detail::send_fd(socket->native_handle(), pp->c2s_pipe[1]);
            
            // wait for ack
            boost::asio::async_read(
                *socket,
                boost::asio::buffer(pp->ack_data, 3),
                boost::bind(
                    &endpoint::handle_read_ack<AcceptHandler>,
                    this,socket,pp,handler,::_1,::_2
                )
            );
        } catch (boost::system::error_code & ec) {
            handler(ec);
        }
    }
    
    /// Read some data from the socket
    /**
     * This function is used to read data from the stream socket. The function
     * call will block until one or more bytes of data has been read 
     * successfully, or until an error occurs.
     *
     * As with other asio `read_some` methods, this may not read all the bytes
     * available. Consider `boost::asio::read` to read all.
     *
     * @param buffers One or more buffers into which the data will be read.
     * @return The number of bytes read.
     */
    template<typename MutableBufferSequence>
    std::size_t read_some(const MutableBufferSequence & buffers) {
        return m_input.read_some(buffers);
    }
    
    /// Read some data from the socket (exception free)
    /**
     * This function is used to read data from the stream socket. The function
     * call will block until one or more bytes of data has been read 
     * successfully, or until an error occurs.
     *
     * As with other asio `read_some` methods, this may not read all the bytes
     * available. Consider `boost::asio::read` to read all.
     *
     * @param buffers One or more buffers into which the data will be read.
     * @param ec Set to indicate what error occurred, if any.
     * @return The number of bytes read. Returns 0 if an error occurred.
     */
    template<typename MutableBufferSequence>
    std::size_t read_some(const MutableBufferSequence & buffers, boost::system::error_code & ec) {
        return m_input.read_some(buffers,ec);
    }
    
    /// Write some data to the socket.
    /**
     * This function is used to write data to the stream socket. The function 
     * call will block until one or more bytes of the data has been written 
     * successfully, or until an error occurs.
     *
     * As with other asio `write_some` methods, this may not write all the bytes
     * in the buffer. Consider `boost::asio::write` to write all.
     *
     * @param buffers One or more data buffers to be written to the socket.
     * @return The number of bytes written.
     */
    template<typename ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence & buffers) {
        return m_output.write_some(buffers);
    }
    
    /// Write some data to the socket. (exception free)
    /**
     * This function is used to write data to the stream socket. The function 
     * call will block until one or more bytes of the data has been written 
     * successfully, or until an error occurs.
     *
     * As with other asio `write_some` methods, this may not write all the bytes
     * in the buffer. Consider `boost::asio::write` to write all.
     *
     * @param buffers One or more data buffers to be written to the socket.
     * @param ec Set to indicate what error occurred, if any.
     * @return The number of bytes written. Returns 0 if an error occurred.
     */
    template<typename ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence & buffers, boost::system::error_code & ec) {
        return m_output.write_some(buffers, ec); 
    }
    
    /// Start an asynchronous read.
    /**
     * This function is used to asynchronously read data from the input pipe.
     * The function call always returns immediately.
     *
     * Note: all of the behaviors and restrictions associated with
     * `basic_stream_socket::async_read_some` apply here. Please consult the
     * boost documentation for that method for more details.
     *
     * @param buffers One or more buffers into which the data will be read.
     * @param handler The handler to be called when the read operation completes.
     */
    template<typename MutableBufferSequence, typename ReadHandler>
    void async_read_some(const MutableBufferSequence & buffers, ReadHandler handler) {
        m_input.async_read_some(buffers, handler);
    }
    
    /// Start an asynchronous write.
    /**
     * This function is used to asynchronously write data to the output pipe.
     * The function call always returns immediately.
     *
     * Note: all of the behaviors and restrictions associated with
     * `basic_stream_socket::async_write_some` apply here. Please consult the
     * boost documentation for that method for more details.
     *
     * @param buffers One or more data buffers to be written to the socket.
     * @param handler The handler to be called when the write operation completes.
     */
    template<typename ConstBufferSequence, typename WriteHandler>
    void async_write_some(const ConstBufferSequence & buffers, WriteHandler handler) {
        m_output.async_write_some(buffers, handler);
    }
    
    /// Returns the io_service object being used
    boost::asio::io_service & get_io_service() {
        return m_io_service;
    }
private:
    /// Internal handler for async_connect
    template <typename ConnectHandler>
    void handle_connect(socket_ptr socket, ConnectHandler handler,
        const boost::system::error_code & connect_ec)
    {
        if (connect_ec) {
            handler(connect_ec);
            return;
        }

        boost::system::error_code ec;

        detail::recv_msghdr::ptr msghdr_ptr(new class detail::recv_msghdr());

        // start the task to asyncronously receive the s2c pipe endpoint
        async_recv_fd(msghdr_ptr, socket, handler);
    }

    /// Internal helper for initiating reads into `detail::recv_fd`
    template <typename ConnectHandler>
    void async_recv_fd(detail::recv_msghdr::ptr msghdr_ptr, socket_ptr socket,
        ConnectHandler handler)
    {
        boost::system::error_code ec;
        
        int pipe = detail::recv_fd(socket->native_handle(), msghdr_ptr, ec);
        if (ec) {
            if (ec == boost::system::errc::make_error_code(boost::system::errc::operation_would_block)) {
                // If the error is "would block" push an async task to wait until
                // this socket is ready.
                socket->async_read_some(boost::asio::null_buffers(),boost::bind(
                    &endpoint::handle_recv_fd<ConnectHandler>,
                    this,msghdr_ptr,socket,handler,::_1
                ));
            } else {
                // TODO: close fds? Technically they will be wrapped in stream
                // descriptors that own them. We could potentially close fds
                // a bit sooner than endpoint destruction. Is it worth it?
                handler(ec);
            }
            return;
        }

        // TODO: is `is_open` the best way to test this? State variable instead?
        if (!m_input.is_open()) {
            m_input.assign(pipe);

            // start reading output pipe
            msghdr_ptr.reset(new class detail::recv_msghdr());

            async_recv_fd(msghdr_ptr, socket, handler);
        } else if (!m_output.is_open()) {
            m_output.assign(pipe);

            // send ack
            boost::asio::async_write(
                *socket,
                boost::asio::buffer("ack", 3),
                boost::bind(
                    &endpoint::handle_write_ack<ConnectHandler>,
                    this,
                    socket,
                    handler,
                    ::_1,
                    ::_2
                )
            );
        } else {
            // TODO: close fds? Technically they will be wrapped in stream
            // descriptors that own them. We could potentially close fds
            // a bit sooner than endpoint destruction. Is it worth it?
            handler(make_error_code(error::general));
        }
    }

    /// Internal handler for async_recv_fd
    template <typename ConnectHandler>
    void handle_recv_fd(detail::recv_msghdr::ptr msghdr_ptr, socket_ptr socket,
        ConnectHandler handler, const boost::system::error_code & ec)
    {
        if (ec) {
            // TODO: close fds? Technically they will be wrapped in stream
            // descriptors that own them. We could potentially close fds
            // a bit sooner than endpoint destruction. Is it worth it?
            handler(ec);
        } else {
            // retry
            async_recv_fd(msghdr_ptr, socket, handler);
        }
    }

    /// Internal handler for async_write of acknowledgement messages
    template <typename ConnectHandler>
    void handle_write_ack(socket_ptr socket, ConnectHandler handler,
        const boost::system::error_code & ec, size_t bytes_transferred)
    {
        if (ec) {
            // TODO: close fds? Technically they will be wrapped in stream
            // descriptors that own them. We could potentially close fds
            // a bit sooner than endpoint destruction. Is it worth it?
            handler(error::make_error_code(error::ack_failed));
        } else {
            handler(ec);
        }
    }

    /// Internal helper for async_read of acknowledgement messages
    template <typename AcceptHandler>
    void handle_read_ack(socket_ptr socket, detail::pipe_pair::ptr pp,
        AcceptHandler handler, const boost::system::error_code & ec,
        size_t bytes_transferred)
    {
        if (ec || bytes_transferred != 3 || strncmp(pp->ack_data, "ack", 3) != 0) {
            handler(error::make_error_code(error::ack_failed));
        } else {
            handler(ec);
        }
    }

    boost::asio::io_service & m_io_service;
    
    boost::asio::posix::stream_descriptor m_input;
    boost::asio::posix::stream_descriptor m_output;
}; // class endpoint


} // namespace asio_pipe_transport

#endif /* defined(_ASIO_PIPE_TRANSPORT_ENDPOINT_) */
