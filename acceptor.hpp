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

class acceptor {
public:
    typedef boost::shared_ptr<boost::asio::local::stream_protocol::socket> socket_ptr;

    /// Construct a pipe transport acceptor
    /**
     * Construct a pipe transport acceptor and register it with an io_service
     * and path. Once constructed, use the `accept` method to accept new
     * connections
     *
     * @param service The io_service this acceptor will use
     * @param path The Unix domain socket path to accept connections on
     */
    acceptor (boost::asio::io_service & service, std::string path)
      : m_io_service(service)
      , m_endpoint(path)
      , m_acceptor(service,m_endpoint)
    {}

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
        boost::asio::local::stream_protocol::socket socket(m_io_service);
        
        boost::system::error_code ec;
        m_acceptor.accept(socket, ec);
        if (ec) {return ec;}
        
        // generate and exchange pipes for further communication
        ec = endpoint.init_pipes(socket);
        if (ec) {return ec;}
        
        return boost::system::error_code();
    }

    template <typename AcceptHandler>
    void async_accept(endpoint & endpoint, AcceptHandler handler) {
        socket_ptr socket(new boost::asio::local::stream_protocol::socket(m_io_service));

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

    template <typename AcceptHandler>
    void handle_async_accept(socket_ptr socket, endpoint & endpoint, AcceptHandler handler, const boost::system::error_code & accept_ec) {
        if (accept_ec) {
            handler(accept_ec);
            return;
        }

        endpoint.async_init_pipes(socket, handler);
    }
private:
    boost::asio::io_service & m_io_service;
    
    boost::asio::local::stream_protocol::endpoint m_endpoint;
    boost::asio::local::stream_protocol::acceptor m_acceptor;
};

} // namespace asio_pipe_transport

#endif /* defined(_ASIO_PIPE_TRANSPORT_ACCEPTOR_) */
