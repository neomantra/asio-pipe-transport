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

#include <sys/stat.h>

using namespace bandit;
using asio_pipe_transport::acceptor;
using asio_pipe_transport::endpoint;

boost::system::error_code make_error(boost::system::errc::errc_t code) {
    // TODO: is this portible enough?
    return boost::system::error_code(code, boost::system::system_category());
}

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
        acceptor.reset(new asio_pipe_transport::acceptor(*s_service,"/tmp/test"));
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
});

describe("asyncronous asio_pipe_transport", []() {
    std::unique_ptr<acceptor> acceptor;
    std::unique_ptr<endpoint> s_endpoint;
    std::unique_ptr<endpoint> c_endpoint;
    std::unique_ptr<boost::asio::io_service> service;

    before_each([&]() {
        ::unlink("/tmp/test");
        service.reset(new boost::asio::io_service());
        acceptor.reset(new asio_pipe_transport::acceptor(*service,"/tmp/test"));
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

describe("an acceptor", []() {
    std::unique_ptr<acceptor> a;
    std::unique_ptr<boost::asio::io_service> service;
    struct stat buf;
    boost::system::error_code ec;

    before_each([&]() {
        ::unlink("/tmp/test");
        service.reset(new boost::asio::io_service());
    });
    
    after_each([&]() {
        a.reset();
        service.reset();
        ::unlink("/tmp/test");
    });

    it("cleans up its socket file by default", [&]() {
        a.reset(new acceptor(*service,"/tmp/test"));

        AssertThat(stat("/tmp/test", &buf), Equals(0));
        a.reset();
        AssertThat(stat("/tmp/test", &buf), Equals(-1));
        AssertThat(errno, Equals(ENOENT));
    });

    it("leaves the socket file if cleanup is false", [&]() {
        a.reset(new acceptor(*service,"/tmp/test",false));

        AssertThat(stat("/tmp/test", &buf), Equals(0));
        a.reset();
        AssertThat(stat("/tmp/test", &buf), Equals(0));
    });

    // Pending confirmation on "socket already in use" behavior
    /*it("leaves the socket file if it existed already", [&]() {
        try {
            a.reset(new acceptor(*service,"/tmp/test",false));
            a.reset(new acceptor(*service,"/tmp/test",true));
        } catch (std::exception & e) {
            ec = e;
        }

        AssertThat(stat("/tmp/test", &buf), Equals(0));
        a.reset();
        AssertThat(stat("/tmp/test", &buf), Equals(0));
    });*/

    it("fails to bind when the socket path exists", [&]() {
        a.reset(new acceptor(*service,"/tmp/test",false));

        AssertThrows(boost::system::system_error,
            a.reset(new acceptor(*service,"/tmp/test")));

        AssertThat(LastException<boost::system::system_error>().code(),
            Equals(make_error(boost::system::errc::address_in_use)));
    });

    it("fails to bind to bogus paths", [&]() {
        AssertThrows(boost::system::system_error,
            a.reset(new acceptor(*service,"/tmp/ad8db799-b01f-45fa-964d-c540eb4749ec/test")));

        AssertThat(LastException<boost::system::system_error>().code(),
            Equals(make_error(boost::system::errc::no_such_file_or_directory)));
    });

    it("is move constructable", [&]() {
        a.reset(new acceptor(*service,"/tmp/test"));

        AssertThat(stat("/tmp/test", &buf), Equals(0));
        {
            acceptor a2(std::move(*a));
            a.release();

            AssertThat(stat("/tmp/test", &buf), Equals(0));
        }
        AssertThat(stat("/tmp/test", &buf), Equals(-1));
    });

    it("is move assignable", [&]() {
        a.reset(new acceptor(*service,"/tmp/test"));
        acceptor a2(*service,"/tmp/test2");

        AssertThat(stat("/tmp/test", &buf), Equals(0));
        AssertThat(stat("/tmp/test2", &buf), Equals(0));

        a2 = std::move(*a);

        AssertThat(stat("/tmp/test", &buf), Equals(0));
        AssertThat(stat("/tmp/test2", &buf), Equals(-1));
    });
});



describe("an endpoint", []() {
    std::unique_ptr<boost::asio::io_service> service;
    boost::system::error_code ec;

    before_each([&]() {
        ec = boost::system::error_code();
        ::unlink("/tmp/test");
        service.reset(new boost::asio::io_service());
    });

    it("can't be read or written to before being connected", [&]() {
        endpoint e(*service);

        boost::asio::write(e,boost::asio::buffer("foo",3), ec);
        AssertThat(ec, Equals(make_error(boost::system::errc::bad_file_descriptor)));

        char data[3];

        boost::asio::read(e,boost::asio::buffer(data,3), ec);
        AssertThat(ec, Equals(make_error(boost::system::errc::bad_file_descriptor)));
    });

    it("can't connect to socket file that doesn't exist", [&]() {
        endpoint e(*service);

        ec = e.connect("/tmp/21812770-9d7e-11e4-bd06-0800200c9a66");
        AssertThat(ec, Equals(make_error(boost::system::errc::no_such_file_or_directory)));
    });

    // TODO endpoint move constructor / assignment
});



});



// listen, connect, send/recv in both directions

// read/write after close

// accept two connections with one acceptor
// accept connection with already connected endpoint


int main(int argc, char* argv[]) {
    return bandit::run(argc, argv);
}
