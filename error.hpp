//
//  error.hpp
//  asio_pipe_transport
//
//  Created by Peter Thorson on 2015-01-07.
//  Copyright (c) 2015 Neomantra. All rights reserved.
//

#ifndef _ASIO_PIPE_TRANSPORT_ERROR_
#define _ASIO_PIPE_TRANSPORT_ERROR_

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

namespace asio_pipe_transport {

/// Library level error codes
namespace error {
enum value {
    /// Catch-all library error
    general = 1,
    
    /// Unknown errno value from system function
    unknown_system_error,
    
    /// Acknowledgement failed
    ack_failed
}; // enum value

class category : public boost::system::error_category {
public:
    category() {}

    char const * name() const noexcept {
        return "asio-pipe-transport";
    }

    std::string message(int value) const {
        switch(value) {
            case error::general:
                return "Generic error";
            case error::unknown_system_error:
                return "Unknown errno value from system function";
            case error::ack_failed:
                return "Pipe establishment acknowledgement failed";
            default:
                return "Unknown";
        }
    }
};

inline const boost::system::error_category & get_category() {
    static category instance;
    return instance;
}

inline boost::system::error_code make_error_code(error::value e) {
    return boost::system::error_code(static_cast<int>(e), get_category());
}

} // namespace error
} // namespace asio_pipe_transport

namespace boost {
namespace system {

template<> struct is_error_code_enum<asio_pipe_transport::error::value>
{
    static bool const value = true;
};

} // system
} // boost

#endif /* defined(_ASIO_PIPE_TRANSPORT_ERROR_) */
