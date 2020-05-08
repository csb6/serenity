#include <LibCore/IMAPClient.h>
#include <ctype.h>

namespace Core {

static IMAPResponseStatus get_response_status(StringView response)
{
    if(response.is_empty())
        return IMAPResponseStatus::None;

    for(size_t i = 0; i < response.length(); ++i) {
        if(isspace(response[i])) {
            // Found the char right before the status (if there is one)
            if(i+3 >= response.length())
                return IMAPResponseStatus::None;
            else if(response.substring_view(i+1, 2) == "OK")
                return IMAPResponseStatus::Ok;
            else if(response.substring_view(i+1, 2) == "NO")
                return IMAPResponseStatus::No;
            else if(i+4 < response.length() && response.substring_view(i+1, 3) == "BAD")
                return IMAPResponseStatus::Bad;
            else
                return IMAPResponseStatus::None;
        }
    }
    return IMAPResponseStatus::None;
}


IMAPClient::IMAPClient(StringView address, int port)
{
    m_socket = Core::TCPSocket::construct(this);
    bool connected = m_socket->connect(address, port);
    dbg() << "Step 1: connection: ";
    if(connected)
        dbg() << "Connected successfully";
    else
        dbg() << "Failed to connect";

    // Setup thread for receiving/queuing server responses
    m_receive_thread = LibThread::Thread::construct(
        [this] {
            while(true) {
                const ByteBuffer response{m_socket->receive(1000)};
                if(response.is_empty())
                    // For some reason servers sometimes send empty messages
                    continue;
                dbg() << "Got a response: " << response;
                m_queue_lock.lock();
                m_message_queue.append(response);
                m_queue_lock.unlock();
            }
            return 0;
        });
    m_receive_thread->start();
}

IMAPClient::~IMAPClient()
{
    m_receive_thread->quit();
}

bool IMAPClient::login(StringView username, StringView password)
{
    StringBuilder command;
    command.append("login ");
    command.append(username);
    command.append(" ");
    command.append(password);
    bool status = send_command(command.string_view());
    if(status) {
        ByteBuffer response{receive_response()};
        dbg() << "Step 2: Login: ";
        const IMAPResponseStatus res_status = get_response_status(response);
        if(res_status == IMAPResponseStatus::Ok) {
            m_state = IMAPConnectionState::Authenticated;
            dbg() << "Logged in successfully";
            return true;
        } else {
            dbg() << "Failed to login";
        }
    }
    return false;
}

bool IMAPClient::select(StringView mailbox)
{
    if(m_state != IMAPConnectionState::Selected
       && m_state != IMAPConnectionState::Authenticated) {
        dbg() << "Cannot select: improper current state";
        return false;
    }
    StringBuilder command;
    command.append("select ");
    command.append(mailbox);
    bool status = send_command(command.string_view());
    if(status) {
        m_state = IMAPConnectionState::Selected;
        ByteBuffer response{receive_response()};
        dbg() << "Step 3: Selected mailbox: " << mailbox;
        const IMAPResponseStatus res_status = get_response_status(response);
        if(res_status == IMAPResponseStatus::Ok) {
            m_state = IMAPConnectionState::Authenticated;
            dbg() << "Selected mailbox successfully";
            return true;
        } else {
            dbg() << "Failed to select mailbox";
            dbg() << response;
        }
    }
    return status;
}

StringBuilder IMAPClient::new_message_id()
{
    StringBuilder id;
    // Each message has a unique alphanumeric identifier (e.g. "a001")
    id.appendf("a%03d", m_message_id);
    ++m_message_id;
    return id;
}

bool IMAPClient::send_command(StringView command)
{
    StringBuilder message{new_message_id()};
    message.append(" ");
    message.append(command);
    message.append("\r\n");
    bool status = m_socket->send(message.to_byte_buffer());
    dbg() << "Sent: " << command;
    if(!status) {
        dbg() << "Failed to send command: " << command << "\n";
    }
    return status;
}

ByteBuffer IMAPClient::receive_response()
{
    ByteBuffer response;
    while(true) {
        m_queue_lock.lock();
        if(!m_message_queue.is_empty()) {
            response = m_message_queue.take_last();
            m_queue_lock.unlock();
            return response;
        }
        m_queue_lock.unlock();
    }
    // Should never reach here
    return response;
}

}
