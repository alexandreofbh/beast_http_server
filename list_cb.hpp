#ifndef LIST_CB_HPP
#define LIST_CB_HPP

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <list>

namespace http {

template<class Message, class Session>
class list_cb{

public:

    using ptr = boost::shared_ptr<list_cb<Message, Session> >;
    using F = boost::function<void (const Message &, Session &, list_cb &)>;
    using L = std::list<F>;

//    template<class... Callback>
//    explicit self_type(Callback && ... handlers) :
//        invoke_l_{boost::forward<Callback>(handlers)...}, c_iter_(invoke_l_.cbegin())
//    {}

    friend class cb_invoker;

    explicit list_cb(){}

    list_cb(const L & l) :
        invoke_l_(l), c_iter_{invoke_l_.cbegin()}, message_cp{nullptr}, session_p{nullptr}
    {}

    list_cb(L && l) :
        invoke_l_(std::move(l)), c_iter_{invoke_l_.cbegin()}, message_cp{nullptr}, session_p{nullptr}
    {}

    void operator ()(){
        return exec_fwd();
    }

private:

    void exec(const Message & message, Session & session) {
        message_cp = boost::addressof(message);
        session_p = boost::addressof(session);
        return exec_fwd();
    }

    void exec_fwd(){
        if(c_iter_ == invoke_l_.cend())
            return;
        else
            return (*c_iter_++) (*message_cp, *session_p, *this);
    }

    void reset(){
        c_iter_ = invoke_l_.cbegin();
    }

    L invoke_l_;
    typename L::const_iterator c_iter_;
    const Message * message_cp;
    Session * session_p;

}; // list_cb class

} // namespace

#endif // LIST_CB_HPP
