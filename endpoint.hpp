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

#include <iostream>

#include <string>

namespace asio_pipe_transport {

class endpoint {
public:
    endpoint (boost::asio::io_service & service)
      : m_io_service(service)
      , m_input(service)
      , m_output(service)
    {}

    boost::system::error_code connect(std::string path) {
        boost::system::error_code ec;
        
        boost::asio::local::stream_protocol::endpoint ep(path);
        boost::asio::local::stream_protocol::socket socket(m_io_service);
        
        socket.connect(ep,ec);
        if (ec) {
            return ec;
        }
        
        int send_pipe;
        int recv_pipe;
        
        // receive s2c pipe endpoint
        recv_pipe = recv_fd(socket.native_handle(), ec);
        if (ec) {
            return ec;
        }
        // receive c2s pipe endpoint
        send_pipe = recv_fd(socket.native_handle(), ec);
        if (ec) {
            return ec;
        }
        
        // TODO: send ack
        
        // test client input
        m_input.assign(::dup(recv_pipe));
        m_output.assign(::dup(send_pipe));
        
        return boost::system::error_code();
    }

    template <typename Socket>
    boost::system::error_code init_pipes(Socket & socket) {
        // create pipes
        int s2c_pipe[2];
        int c2s_pipe[2];
        
        if (pipe(s2c_pipe) == -1) {
            return process_pipe_error();
        }
        if (pipe(c2s_pipe) == -1) {
            return process_pipe_error();
        }
        
        // client read: s2c_pipe[0] -> send this fd to client for reading
        // server write: s2c_pipe[1] -> keep this fd local to write
        // server read: c2s_pipe[0] -> keep this fd local to read
        // client write: c2s_pipe[1] -> send this fd to client for writing
                
        // This input stream is where we will read data from the client
        // TODO: do we need to duplicate the fds here?
        m_input.assign(::dup(c2s_pipe[0]));
        
        // This output stream is where we will write data to the client
        m_output.assign(::dup(s2c_pipe[1]));
        
        
        // TODO: these may be semi-blocking.
        // send s2c pipe endpoint
        boost::system::error_code ec;
        
        ec = send_fd(socket.native_handle(), s2c_pipe[0]);
        if (ec) {return ec;}
        
        // send c2s pipe endpoint
        ec = send_fd(socket.native_handle(), c2s_pipe[1]);
        if (ec) {return ec;}
        
        // TODO: wait for ack
        
        return boost::system::error_code();
    }

    // forward the appropriate read/write interfaces to behave like an asio
    // sync/async stream
    template<typename MutableBufferSequence>
    std::size_t read_some(const MutableBufferSequence & buffers) {
        return m_input.read_some(buffers);
    }
    
    template<typename MutableBufferSequence>
    std::size_t read_some(const MutableBufferSequence & buffers, boost::system::error_code & ec) {
        return m_input.read_some(buffers,ec);
    }
    
    template<typename ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence & buffers) {
        return m_output.write_some(buffers);
    }
    
    template<typename ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence & buffers, boost::system::error_code & ec) {
        return m_output.write_some(buffers);
    }
private:
    static boost::system::error_code send_fd(int socket, int fd) {
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
            return process_sendmsg_error();
        }
        
        return boost::system::error_code();
    }

    static int recv_fd(int socket, boost::system::error_code & ec) {
        ec = boost::system::error_code();
        
        struct msghdr msg;
        struct iovec iov[1];
        struct cmsghdr *ctrl = NULL;
        
        char ctrl_buf[CMSG_SPACE(sizeof(int))];
        char data[1];
        ssize_t res;
        
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
        
        res = recvmsg(socket, &msg, 0);
        
        if(res < 0) {
            ec = process_recvmsg_error();
            return -1;
        }
        if (res == 0) {
            // TODO: this indicates that the connection was cleanly closed.
            // we aren't expecting that right now though.
        }
        
        for (ctrl = CMSG_FIRSTHDR(&msg); ctrl != NULL; ctrl = CMSG_NXTHDR(&msg,ctrl)) {
            if( (ctrl->cmsg_level == SOL_SOCKET) && (ctrl->cmsg_type == SCM_RIGHTS) ) {
                return *(reinterpret_cast<int *>(CMSG_DATA(ctrl)));
            }
        }
        
        return -1;
    }

    static boost::system::error_code process_sendmsg_error() {
        namespace errc = boost::system::errc;
        
        // separate case because EAGAIN and EWOULDBLOCK sometimes share a value
        // which confuses the switch
        if (errno == EAGAIN) {
            return errc::make_error_code(errc::operation_would_block);
        }
        
        switch(errno) {
            case EACCES:
                return errc::make_error_code(errc::permission_denied);
            case EWOULDBLOCK:
                return errc::make_error_code(errc::operation_would_block);
            case EBADF:
                return errc::make_error_code(errc::bad_file_descriptor);
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
            default:
                return error::make_error_code(error::unknown_system_error);
        }
    }

    static boost::system::error_code process_recvmsg_error() {
        namespace errc = boost::system::errc;
        
        // separate case because EAGAIN and EWOULDBLOCK sometimes share a value
        // which confuses the switch
        if (errno == EAGAIN) {
            return errc::make_error_code(errc::operation_would_block);
        }
        
        switch(errno) {
            case EWOULDBLOCK:
                return errc::make_error_code(errc::operation_would_block);
            case EBADF:
                return errc::make_error_code(errc::bad_file_descriptor);
            case ECONNREFUSED:
                return errc::make_error_code(errc::connection_refused);
            case EFAULT:
                return errc::make_error_code(errc::bad_address);
            case EINTR:
                return errc::make_error_code(errc::interrupted);
            case EINVAL:
                return errc::make_error_code(errc::invalid_argument);
            case ENOMEM:
                return errc::make_error_code(errc::not_enough_memory);
            case ENOTCONN:
                return errc::make_error_code(errc::not_connected);
            case ENOTSOCK:
                return errc::make_error_code(errc::not_a_socket);
            default:
                return error::make_error_code(error::unknown_system_error);
        }
    }

    static boost::system::error_code process_pipe_error() {
        namespace errc = boost::system::errc;
        
        switch (errno) {
            case EFAULT:
                return errc::make_error_code(errc::bad_address);
            case EINVAL:
                return errc::make_error_code(errc::invalid_argument);
            case EMFILE:
                return errc::make_error_code(errc::too_many_files_open);
            case ENFILE:
                return errc::make_error_code(errc::too_many_files_open_in_system);
            default:
                return error::make_error_code(error::unknown_system_error);
        }
    }

    boost::asio::io_service & m_io_service;
    
    boost::asio::posix::stream_descriptor m_input;
    boost::asio::posix::stream_descriptor m_output;
};


} // namespace asio_pipe_transport

#endif /* defined(_ASIO_PIPE_TRANSPORT_ENDPOINT_) */
