//
//  acceptor.h
//  asio_pipe_transport
//
//  Created by Peter Thorson on 2015-01-07.
//  Copyright (c) 2015 Neomantra. All rights reserved.
//

#ifndef _ASIO_PIPE_TRANSPORT_ACCEPTOR_
#define _ASIO_PIPE_TRANSPORT_ACCEPTOR_

#include "endpoint.hpp"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/move/utility.hpp>

#ifndef BOOST_ASIO_HAS_LOCAL_SOCKETS
    static_assert("This version of Asio does not support local sockets");
#endif

#ifndef BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR
    static_assert("This version of Asio does not support POSIX stream descriptors");
#endif

#include <algorithm>
#include <iostream>
#include <string>

namespace asio_pipe_transport {

/// Class for accepting new pipe transport connections.
/**
 * The asio_pipe_transport::acceptor class is used for accepting new pipe
 * transport connections.
 *
 * @sa endpoint
 */
class acceptor {
public:
    typedef boost::shared_ptr<boost::asio::local::stream_protocol::socket> socket_ptr;

    /// Construct a pipe transport acceptor
    /**
     * Construct a pipe transport acceptor and register it with an io_service
     * and path. Once constructed, use the `accept` or `async_accept` method to
     * accept new connections.
     *
     * If the takeover parameter is set the acceptor will assume ownership of
     * the domain socket file and will ensure that it is cleaned up when the
     * acceptor is destroyed. This defaults to true.
     *
     * @param service The io_service this acceptor will use
     * @param path The Unix domain socket path to accept connections on
     * @param cleanup_socket Whether or not the acceptor should remove the
     *        socket file when it is done with it.
     */
    acceptor(boost::asio::io_service & s, std::string path, bool cleanup_socket = true)
      : m_io_service(&s)
      , m_endpoint(path)
      , m_acceptor(s,m_endpoint)
      , m_cleanup_socket(cleanup_socket)
    {}

    /// Move constructor
    acceptor(BOOST_RV_REF(acceptor) other)
      : m_io_service(&other.get_service())
      , m_endpoint(other.m_endpoint)
      , m_acceptor(boost::move(other.m_acceptor))
      , m_cleanup_socket(other.m_cleanup_socket)
    {}

    /// Move assignment operator
    acceptor & operator=(BOOST_RV_REF(acceptor) other) {
        if (m_cleanup_socket) {
            ::unlink(m_endpoint.path().c_str());
        }
        m_io_service = &other.get_service();
        m_endpoint = other.m_endpoint;
        m_acceptor = boost::move(other.m_acceptor);
        m_cleanup_socket = other.m_cleanup_socket;

        return *this;
    }

    /// Destructor
    ~acceptor() {
        if (m_cleanup_socket) {
            ::unlink(m_endpoint.path().c_str());
        }
    }

    /// Accept a new pipe transport connection
    /**
     * Waits for a connection on the Unix domain socket at this acceptors path.
     * When a connection is opened, use it to negotiate a pair of anonymous
     * pipes connecting the endpoint supplied with the remote connecting
     * endpoint.
     *
     * When this method returns without error, `endpoint` will be in a connected
     * state ready to read and write data over pipes to its associated remote
     * endpoint.
     *
     * @param endpoint The endpoint to connect
     * @return A status code indicating the error that ocurred, if any
     */
    boost::system::error_code accept(endpoint & endpoint) {
        boost::asio::local::stream_protocol::socket socket(get_service());
        
        boost::system::error_code ec;
        m_acceptor.accept(socket, ec);
        if (ec) {return ec;}
        
        // generate and exchange pipes for further communication
        ec = endpoint.init_pipes(socket);
        if (ec) {return ec;}
        
        return boost::system::error_code();
    }

    /// Start an asynchronous accept for a new pipe transport connection
    /**
     * This function is used to asynchronously initiate a pipe transport
     * connection. The function call always returns immediately.
     *
     * A sequence of asyncronous actions will be started that wait for a 
     * connection on the Unix domain socket at this acceptors path. When a 
     * connection is opened, use it to negotiate a pair of anonymous pipes 
     * connecting the endpoint supplied with the remote connecting endpoint.
     *
     * When `handler` is called without error, `endpoint` will be in a connected
     * state ready to read and write data over pipes to its associated remote
     * endpoint.
     *
     * @param endpoint The endpoint to connect to. Ownership of the endpoint is
     *        retained by the caller, which must guarantee that it is valid
     *        until the handler is called.
     * @param handler The handler to be called when the accept operation
     *        completes. All typical rules for an Asio asyncronous handler apply
     *        please consult Asio documentation for more details.
     */
    template <typename AcceptHandler>
    void async_accept(endpoint & endpoint, AcceptHandler handler) {
        socket_ptr socket(new boost::asio::local::stream_protocol::socket(get_service()));

        m_acceptor.async_accept(
            *socket,
            boost::bind(
                &acceptor::handle_async_accept<AcceptHandler>,
                this,
                socket,
                boost::ref(endpoint),
                handler,
                ::_1
            )
        );
    }
private:
    BOOST_MOVABLE_BUT_NOT_COPYABLE(acceptor)

    /// Internal helper to retrieve the io_service
    boost::asio::io_service & get_service() {
        return *m_io_service;
    }

    /// Internal handler for Asyncronous accept
    template <typename AcceptHandler>
    void handle_async_accept(socket_ptr socket, endpoint & endpoint,
        AcceptHandler handler, const boost::system::error_code & accept_ec)
    {
        if (accept_ec) {
            handler(accept_ec);
            return;
        }

        endpoint.async_init_pipes(socket, handler);
    }

    boost::asio::io_service * m_io_service;
    
    boost::asio::local::stream_protocol::endpoint m_endpoint;
    boost::asio::local::stream_protocol::acceptor m_acceptor;

    bool m_cleanup_socket;
}; // class acceptor

} // namespace asio_pipe_transport

#endif /* defined(_ASIO_PIPE_TRANSPORT_ACCEPTOR_) */
