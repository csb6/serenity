#include <LibCore/IMAPClient.h>

namespace Core {

IMAPClient::IMAPClient(StringView address, int port)
{
    m_socket = Core::TCPSocket::construct(this);
    m_socket->connect(address, port);

    ByteBuffer response{m_socket->receive(100)};
    dbg() << "IMAP Server: " << response << '\n';
}

IMAPClient::~IMAPClient()
{
}

bool IMAPClient::login(StringView username, StringView password)
{
    StringBuilder command;
    command.append("login ");
    command.append(username);
    command.append(password);
    bool status = send_command(command.string_view());
    if(status)
        m_state = IMAPConnectionState::Authenticated;
    return status;
}

bool IMAPClient::select(StringView mailbox)
{
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
