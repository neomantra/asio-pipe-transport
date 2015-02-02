//
//  endpoint.hpp
//  asio_pipe_transport
//
//  Created by Peter Thorson on 2015-01-07.
//  Copyright (c) 2015 Neomantra. All rights reserved.
//

#ifndef _ASIO_PIPE_TRANSPORT_ENDPOINT_
#define _ASIO_PIPE_TRANSPORT_ENDPOINT_

#include "detail.hpp"
#include "error.hpp"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/system/error_code.hpp>

#include <string>

namespace asio_pipe_transport {

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
        : m_io_service(&service)
        , m_input(service)
        , m_output(service)
    {}

    endpoint(const endpoint & other) = delete;
    endpoint & operator=(const endpoint * other) = delete;

    endpoint(endpoint && other) = default;
    endpoint & operator=(endpoint && other) = default;

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
        boost::asio::local::stream_protocol::socket socket(get_io_service());
        
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
        socket_ptr socket(new boost::asio::local::stream_protocol::socket(get_io_service()));

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
        return *m_io_service;
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

    boost::asio::io_service * m_io_service;
    
    boost::asio::posix::stream_descriptor m_input;
    boost::asio::posix::stream_descriptor m_output;
}; // class endpoint


} // namespace asio_pipe_transport

#endif /* defined(_ASIO_PIPE_TRANSPORT_ENDPOINT_) */
