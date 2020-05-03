#pragma once

#include <LibCore/TCPSocket.h>
#include <AK/ByteBuffer.h>
#include <AK/String.h>
#include <AK/StringBuilder.h>
#include <AK/Vector.h>
#include <AK/StringView.h>
#include <stdio.h>

namespace Core {

enum class IMAPResponsePrefix : char {
    // Command starts with a '*'
    Untagged,
    // Begins with some identifier (e.g. a001)
    Tagged,
    // Begins with a '+' (responses marked with '+' aren't really commands,
    // but they are pretty close)
    Continuation
};

enum class IMAPResponseStatus : char {
    Ok, // Command succeeded
    No, // Command failed
    Bad, // Command is unrecognized/has syntax error
    None
};

enum class IMAPSystemFlag : char {
    Seen, Answered,
    Flagged, Deleted,
    Draft, Recent
};

enum class IMAPConnectionState : char {
    // Usually default state; in this state when client hasn't
    // supplied any credentials yet
    NotAuthenticated,
    // User has logged in, but not selected anything;
    // can't do any message-related operations
    Authenticated,
    // User has successfully chosen a mailbox
    Selected
};

struct IMAPMessage {
    // id representing a message; can't change within a session
    u32 uid;
    // Standardized flags
    Vector<IMAPSystemFlag> system_flags;
    // User-created flags; can't start with '/'
    Vector<String> keywords;
    // Date format is undefined in IMAP spec
    String date;
    // The header of the message as found in RFC-2822
    String envelope;
    // The body of the message as found in MIME-IMB
    String body;
    // The content of the message
    String text;
};


class IMAPClient final : public Core::Object {
    C_OBJECT(IMAPClient)
public:
    IMAPClient(StringView address, int port = 143);
    virtual ~IMAPClient() override;
    bool login(StringView username, StringView password);
    bool select(StringView mailbox);
private:
    StringBuilder new_message_id();
    bool send_command(StringView command);

    RefPtr<Core::TCPSocket> m_socket;
    int m_message_id = 1;
    IMAPConnectionState m_state = IMAPConnectionState::NotAuthenticated;
};

}
