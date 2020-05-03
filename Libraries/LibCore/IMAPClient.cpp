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
            else {
                return IMAPResponseStatus::None;
            }
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
}

IMAPClient::~IMAPClient()
{
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
        ByteBuffer response{m_socket->receive(500)};
        dbg() << "Step 2: Login: ";
        const IMAPResponseStatus res_status = get_response_status(response);
        if(res_status == IMAPResponseStatus::Ok) {
            m_state = IMAPConnectionState::Authenticated;
            dbg() << "Logged in successfully";
            return true;
        } else {
            dbg() << "Failed to login";
            return false;
        }
    }
    return false;
}

bool IMAPClient::select(StringView mailbox)
{
    if(m_state != IMAPConnectionState::Selected
       || m_state != IMAPConnectionState::Authenicated) {
        dbg() << "Cannot select: improper current state";
        return false;
    }
    StringBuilder command;
    command.append("select ");
    command.append(mailbox);
    bool status = send_command(command.string_view());
    if(status)
        m_state = IMAPConnectionState::Selected;
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
    if(!status) {
        dbg() << "Failed to send command: " << command << "\n";
    }
    return status;
}

}
