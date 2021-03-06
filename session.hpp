#ifndef SESSION_HPP
#define SESSION_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <base.hpp>
#include <list_cb.hpp>
#include <boost/unordered_map.hpp>
#include <boost/regex.hpp>

namespace http {

using resource_regex_t = std::string;
using resource_t = boost::beast::string_view;
using method_t = boost::beast::http::verb;
using s_method_t = boost::beast::string_view;


class cb_invoker{

public:

    template<class Message, class Session, class List_cb>
    void invoke_cb(const Message & message, Session & session, List_cb & l_cb){
        l_cb.reset();
        l_cb.exec(message, session);
    }

};

//###########################################################################

/// \brief session class. Handles an HTTP server connection
/// \tparam Type of body message
template<bool isServer ,class Body>
class session  : private cb_invoker, private boost::noncopyable,
        public boost::enable_shared_from_this<session<true, Body> >
{

public:

    using list_cb_t = list_cb<boost::beast::http::request<Body>, session<true, Body> >;
    using method_map_t = std::map<method_t, boost::unordered_map<resource_regex_t,typename list_cb_t::ptr> >;
    using ptr = boost::shared_ptr< session<true, Body>>;

    template<class Callback>
    static void on_accept(const base::tcp_connection::ptr& connection_p,
                          const typename list_cb_t::ptr & def_all_cb_p,
                          const boost::shared_ptr<method_map_t> & method_map_cb_p,
                          const Callback & handler)
    {
        //auto new_session_p = boost::make_shared<session<true, Body> >(connection_p, def_all_cb_p, method_map_cb_p);
        // session constructor declared private here...
        auto new_session_p = ptr(new session<true, Body>(connection_p, def_all_cb_p, method_map_cb_p));
        handler(*new_session_p);
    }

    void do_read(){

        *req_p_ = {};

        connection_p_->async_read(
                    buffer_,
                    *req_p_,
                    boost::bind(&session<true, Body>::on_read, this->shared_from_this(),
                                boost::asio::placeholders::error,
                                boost::asio::placeholders::bytes_transferred));
    }

    template<class Body2>
    void do_write(boost::beast::http::response<Body2> && msg){

        auto sp = boost::make_shared<boost::beast::http::response<Body2> >(boost::move(msg));
        msg_p_ = sp;

        connection_p_->async_write(*sp,
                                 boost::bind(&session<true, Body>::on_write, this->shared_from_this(),
                                             boost::asio::placeholders::error,
                                             boost::asio::placeholders::bytes_transferred,
                                             sp->need_eof()));
    }

    void do_close()
    {
        connection_p_->shutdown();
    }

private:

    explicit session(){}

    explicit session(const base::tcp_connection::ptr & connection_p,
                     const typename list_cb_t::ptr & def_all_cb_p,
                     const boost::shared_ptr<method_map_t> & method_map_cb_p)
        : connection_p_{connection_p},
          def_all_cb_p_{def_all_cb_p},
          method_map_cb_p_{method_map_cb_p},
          req_p_{boost::make_shared<boost::beast::http::request<Body> >()}
    {}

    void on_read(const boost::system::error_code & ec, std::size_t bytes_transferred){

        boost::ignore_unused(bytes_transferred);

        if(ec == boost::beast::http::error::end_of_stream)
            return do_close();

        if(ec)
            return base::fail(ec, "read");

        process_request();
    }

    void on_write(const boost::system::error_code & ec, std::size_t bytes_transferred, bool close)
    {
        boost::ignore_unused(bytes_transferred);

        if(ec)
            return base::fail(ec, "write");

        if(close)
        {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            return do_close();
        }

        // We're done with the response so delete it
        msg_p_.reset();

        // Read another request
        do_read();
    }

    void process_request()
    {

        resource_t target = req_p_->target();
        method_t method = req_p_->method();

        if(method_map_cb_p_){
            auto method_pos = method_map_cb_p_->find(method);
            if(method_pos != method_map_cb_p_->end()){

                auto & resource_map = method_pos->second;

                for(const auto & value : resource_map){
                    const boost::regex e(value.first, boost::regex::perl | boost::regex::no_except);
                    if(boost::regex_match(std::string(target.data(), target.size()), e)){
                        auto const & cb_p = value.second;

                        if(cb_p)
                            return invoke_cb(boost::cref(*req_p_), boost::ref(*this), *cb_p);

                    }
                }
            }
        }

        if(def_all_cb_p_)
            return invoke_cb(boost::cref(*req_p_), boost::ref(*this), *def_all_cb_p_);

        return do_read();

    }



    base::tcp_connection::ptr  connection_p_;
    typename list_cb_t::ptr  def_all_cb_p_;
    boost::shared_ptr<method_map_t> method_map_cb_p_;
    boost::shared_ptr<boost::beast::http::request<Body> > req_p_;
    boost::shared_ptr<void> msg_p_;
    boost::beast::flat_buffer buffer_;

}; // class session


/// \brief session class. Handles an HTTP client connection
/// \tparam Type of body message
template<class Body>
class session<false, Body>  : private cb_invoker, private boost::noncopyable,
        public boost::enable_shared_from_this<session<false, Body> >{

public:

    using list_cb_t = list_cb<boost::beast::http::response<Body>, session<false, Body> >;
    using ptr = boost::shared_ptr< session<false, Body>>;

    template<class Callback>
    static void on_connect(const base::tcp_connection::ptr& connection_p,
                           const typename list_cb_t::ptr & response_cb_p,
                           const Callback & handler){
        auto new_session_p = ptr(new session<false, Body>(connection_p, response_cb_p));
        handler(*new_session_p);
    }

    void do_read(){
        *res_p_ = {};

        connection_p_->async_read(
                    buffer_,
                    *res_p_,
                    boost::bind(&session<false, Body>::on_read, this->shared_from_this(),
                                boost::asio::placeholders::error,
                                boost::asio::placeholders::bytes_transferred));
    }

    template<class Body2>
    void do_write(boost::beast::http::request<Body2> && msg){
        auto sp = boost::make_shared<boost::beast::http::request<Body2> >(boost::move(msg));
        msg_p_ = sp;

        connection_p_->async_write(*sp,
                                 boost::bind(&session<false, Body>::on_write, this->shared_from_this(),
                                             boost::asio::placeholders::error,
                                             boost::asio::placeholders::bytes_transferred
                                             ));
    }

    void do_close(){
        boost::system::error_code ec;
        // Gracefully close the socket
        connection_p_->socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);

        // not_connected happens sometimes so don't bother reporting it.
        if(ec && ec != boost::system::errc::not_connected)
            return base::fail(ec, "shutdown");
    }

private:

    explicit session(){}

    explicit session(const base::tcp_connection::ptr & connection_p,
                     const typename list_cb_t::ptr & response_cb_p)
        : connection_p_{connection_p},
          response_cb_p_{response_cb_p},
          res_p_{boost::make_shared<boost::beast::http::response<Body> >()}
    {}


    void on_read(const boost::system::error_code & ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if(ec)
            return base::fail(ec, "read");

        if(response_cb_p_)
            invoke_cb(boost::cref(*res_p_), boost::ref(*this), *response_cb_p_);

        // If we get here then the connection is closed gracefully
    }

    void on_write(const boost::system::error_code & ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if(ec)
            return base::fail(ec, "write");

        do_read();
    }

    base::tcp_connection::ptr connection_p_;
    typename list_cb_t::ptr response_cb_p_;
    boost::shared_ptr<boost::beast::http::response<Body> > res_p_;
    boost::shared_ptr<void> msg_p_;
    boost::beast::flat_buffer buffer_;

}; // class session


} // namespace http

#endif // SESSION_HPP
