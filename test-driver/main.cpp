//
//  main.cpp
//  test-driver
//
//  Created by Peter Thorson on 2015-01-14.
//  Copyright (c) 2015 Neomantra. All rights reserved.
//

#include <bandit/bandit.h>

#include "../acceptor.hpp"
#include "../endpoint.hpp"

#include <thread>

using namespace bandit;
using asio_pipe_transport::acceptor;
using asio_pipe_transport::endpoint;

go_bandit([](){

describe("syncronous asio_pipe_transport", []() {
    std::unique_ptr<acceptor> acceptor;
    std::unique_ptr<endpoint> s_endpoint;
    std::unique_ptr<endpoint> s_endpoint2;
    std::unique_ptr<endpoint> c_endpoint;
    std::unique_ptr<boost::asio::io_service> s_service;
    std::unique_ptr<boost::asio::io_service> c_service;

    before_each([&]() {
        ::unlink("/tmp/test");
        s_service.reset(new boost::asio::io_service());
        c_service.reset(new boost::asio::io_service());
        acceptor.reset(new class acceptor(*s_service,"/tmp/test"));
        s_endpoint.reset(new endpoint(*s_service));
        s_endpoint2.reset(new endpoint(*s_service));
        c_endpoint.reset(new endpoint(*c_service));
    });
    
    after_each([&]() {
        ::unlink("/tmp/test");
        c_endpoint.reset();
        s_endpoint.reset();
        s_endpoint2.reset();
        acceptor.reset();
        s_service.reset();
        c_service.reset();
    });

    describe("syncronously exchange data", [&]() {
        boost::system::error_code s_ec;
        boost::system::error_code c_ec;
        
        boost::system::error_code write_ec;
        boost::system::error_code read_ec;
        
        char data[10];
        size_t sent;
        size_t read;
        
        before_each([&]() {
            std::thread server([&](){
                s_ec = acceptor->accept(*s_endpoint);
            
                if (!s_ec) {
                    sent = boost::asio::write(*s_endpoint, boost::asio::buffer("foo", 3), write_ec);
                }
            });
            std::thread client([&](){
                c_ec = c_endpoint->connect("/tmp/test");
        
                if (!c_ec) {
                    read = boost::asio::read(*c_endpoint, boost::asio::buffer(data, 3), read_ec);
                }
            });
            server.join();
            client.join();
        });

        it("should match", [&]() {
            AssertThat(s_ec, Equals(boost::system::error_code()));
            AssertThat(c_ec, Equals(boost::system::error_code()));
            AssertThat(write_ec, Equals(boost::system::error_code()));
            AssertThat(read_ec, Equals(boost::system::error_code()));
            
            AssertThat(sent, Equals(3));
            AssertThat(read, Equals(3));
            AssertThat(std::string(data,3), Equals("foo"));
        });
    });
    
    describe("connect to file that doesn't exist", [&]() {
        boost::system::error_code ec;
        
        before_each([&]() {
            std::thread client([&](){
                ec = c_endpoint->connect("/tmp/21812770-9d7e-11e4-bd06-0800200c9a66");
            });
            client.join();
        });

        it("should match", [&]() {
            // TODO: is this portible enough?
            AssertThat(ec, Equals(boost::system::error_code( boost::system::errc::no_such_file_or_directory, boost::system::system_category())));
        });
    });
    
    // TODO: accepting while already accepting seems to work.. should it?
    /*describe("accept while already accepting", [&]() {
        boost::system::error_code s_ec1;
        boost::system::error_code s_ec2;
        boost::system::error_code c_ec;
        
        before_each([&]() {
            std::thread server1([&](){
                s_ec1 = acceptor->accept(*s_endpoint);
            });
            std::thread server2([&](){
                s_ec2 = acceptor->accept(*s_endpoint2);
            });
            std::thread client([&](){
                sleep(5);
                c_ec = c_endpoint->connect("/tmp/test");
                c_ec = c_endpoint->connect("/tmp/test");
            });
            server1.join();
            server2.join();
            client.join();
        });

        it("should match", [&]() {
            AssertThat(s_ec1, Equals(boost::system::error_code()));
            AssertThat(s_ec2, Equals(boost::system::error_code()));
            AssertThat(c_ec, Equals(boost::system::error_code()));
        });
    });*/
});

describe("asyncronous asio_pipe_transport", []() {
    std::unique_ptr<acceptor> acceptor;
    std::unique_ptr<endpoint> s_endpoint;
    std::unique_ptr<endpoint> c_endpoint;
    std::unique_ptr<boost::asio::io_service> service;

    before_each([&]() {
        ::unlink("/tmp/test");
        service.reset(new boost::asio::io_service());
        acceptor.reset(new class acceptor(*service,"/tmp/test"));
        s_endpoint.reset(new endpoint(*service));
        c_endpoint.reset(new endpoint(*service));
    });
    
    after_each([&]() {
        ::unlink("/tmp/test");
        c_endpoint.reset();
        s_endpoint.reset();
        acceptor.reset();
        service.reset();
    });

    describe("asyncronously exchange data", [&]() {
        boost::system::error_code s_ec;
        boost::system::error_code c_ec;
        
        boost::system::error_code write_ec;
        boost::system::error_code read_ec;
        
        char data[10];
        size_t sent;
        size_t read;
        
        before_each([&]() {
            acceptor->async_accept(*s_endpoint, [&](const boost::system::error_code & ec) {
                boost::asio::async_write(*s_endpoint, boost::asio::buffer("foo", 3), [&](const boost::system::error_code & ec, std::size_t bytes_transferred) {
                    s_ec = ec;
                    sent = bytes_transferred;
                });
            });

            c_endpoint->async_connect("/tmp/test", [&](const boost::system::error_code & ec) {
                if (ec) {
                    c_ec = ec;
                    return;
                }

                boost::asio::async_read(*c_endpoint, boost::asio::buffer(data, 3), [&](const boost::system::error_code & ec, std::size_t bytes_transferred) {
                    c_ec = ec;
                    read = bytes_transferred;
                });
            });

            service->run();
        });

        it("should match", [&]() {
            AssertThat(s_ec, Equals(boost::system::error_code()));
            AssertThat(c_ec, Equals(boost::system::error_code()));
            AssertThat(write_ec, Equals(boost::system::error_code()));
            AssertThat(read_ec, Equals(boost::system::error_code()));
            
            AssertThat(sent, Equals(3));
            AssertThat(read, Equals(3));
            AssertThat(std::string(data,3), Equals("foo"));
        });
    });
});

});



// listen, connect, send/recv in both directions

// listen when there is already a socket file
// listen when the path is bogus

// read/write before opening
// read/write after close

// accept two connections with one acceptor
// accept connection with already connected endpoint


int main(int argc, char* argv[]) {
    return bandit::run(argc, argv);
}
