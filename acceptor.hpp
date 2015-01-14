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
    acceptor (boost::asio::io_service & service, std::string path)
      : m_io_service(service)
      , m_endpoint(path)
      , m_acceptor(service,m_endpoint)
    {}

    
    boost::system::error_code accept(endpoint & endpoint, std::string path) {
        boost::asio::local::stream_protocol::socket socket(m_io_service);
        
        boost::system::error_code ec;
        m_acceptor.accept(socket, ec);
        if (ec) {return ec;}
        
        // generate and exchange pipes for further communication
        ec = endpoint.init_pipes(socket);
        if (ec) {return ec;}
        
        // done with the unix socket now so close it
        socket.shutdown(boost::asio::local::stream_protocol::socket::shutdown_both, ec);
        if (ec) {return ec;}
        
        socket.close(ec);
        if (ec) {return ec;}
        
        return boost::system::error_code();
    }
    
    // TODO: async_accept
private:
    boost::asio::io_service & m_io_service;
    
    boost::asio::local::stream_protocol::endpoint m_endpoint;
    boost::asio::local::stream_protocol::acceptor m_acceptor;
};

} // namespace asio_pipe_transport

#endif /* defined(_ASIO_PIPE_TRANSPORT_ACCEPTOR_) */
