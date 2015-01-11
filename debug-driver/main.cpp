//
//  main.cpp
//  debug-driver
//
//  Created by Peter Thorson on 2015-01-07.
//  Copyright (c) 2015 Neomantra. All rights reserved.
//

#include "../acceptor.hpp"
#include "../endpoint.hpp"

#include <iostream>
#include <string>

int main(int argc, const char * argv[]) {
    boost::asio::io_service service;
    std::string selection = "server";
    boost::system::error_code ec;

    if (argc > 1) {
        selection = argv[1];
    }

    if (selection == "server") {
        ::unlink("/tmp/foo");
        asio_pipe_transport::acceptor a(service,"/tmp/foo");
        asio_pipe_transport::endpoint e(service);
        
        ec = a.accept(e,"/tmp/foo");
        
        if (ec) {
            std::cout << "accept failed: " << ec.message() << std::endl;
            return 1;
        }
        
        std::cout << "testing a write over the pipe" << std::endl;
        boost::asio::write(e, boost::asio::buffer("foo", 3));
    } else {
        asio_pipe_transport::endpoint e(service);
        
        ec = e.connect("/tmp/foo");
        if (ec) {
            std::cout << "connect failed: " << ec.message() << std::endl;
            return 1;
        }
        
        char data[10];
        size_t read = boost::asio::read(e, boost::asio::buffer(data, 3));
        std::cout << "read " << read << " bytes: " << std::string(data,read) << std::endl;
    }

    return 0;
}
