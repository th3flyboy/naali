// For conditions of distribution and use, see copyright notice in LICENSE

#include "StableHeaders.h"
#include "DebugOperatorNew.h"

#include "Server.h"
#include "TundraLogicModule.h"
#include "SyncManager.h"
#include "KristalliProtocolModule.h"
#include "TundraMessages.h"
#include "MsgLogin.h"
#include "MsgLoginReply.h"
#include "MsgClientJoined.h"
#include "MsgClientLeft.h"
#include "UserConnectedResponseData.h"

#include "CoreStringUtils.h"
#include "SceneAPI.h"
#include "ConfigAPI.h"
#include "LoggingFunctions.h"
#include "QScriptEngineHelpers.h"

#include <QtScript>
#include <QDomDocument>
#include <QUuid>

#include "MemoryLeakCheck.h"

Q_DECLARE_METATYPE(UserConnection*);
Q_DECLARE_METATYPE(TundraLogic::SyncManager*);
Q_DECLARE_METATYPE(SceneSyncState*);
Q_DECLARE_METATYPE(StateChangeRequest*);
Q_DECLARE_METATYPE(UserConnectedResponseData*);

using namespace kNet;

namespace TundraLogic
{

Server::Server(TundraLogicModule* owner) :
    owner_(owner),
    framework_(owner->GetFramework()),
    current_port_(-1),
    current_protocol_(""),
    actionsender_(0)
{
    // define sceneID_ with random QUUID.
    // If the /dev/urandom device exists, then the numbers used to construct the UUID
    // will be of cryptographic quality, which will make the UUID unique. Otherwise,
    // the numbers of the UUID will be obtained from the local pseudo-random number generator.
    // On a Windows platform, a GUID is generated, which almost certainly will be unique, on this or any other system, networked or not.
    sceneID_ = QUuid::createUuid().toString().remove(QRegExp("[{}]")).remove("-");
    // for now calculate CRC-16 from QUUID to reduce packet size.
    sceneID_.setNum(qChecksum(sceneID_.toAscii(),16),16);
}

Server::~Server()
{
}

void Server::Update(f64 frametime)
{
}

bool Server::Start(unsigned short port, QString protocol)
{
    if (owner_->IsServer())
    {
        LogDebug("Trying to start server but it's already running.");
        return true; // Already started, don't need to do anything.
    }

    // Protocol is usually defined as a --protocol command line parameter or in config file,
    // but if it's given as a param to this function use it instead.
    if (protocol.isEmpty() && framework_->HasCommandLineParameter("--protocol"))
    {
        QStringList cmdLineParams = framework_->CommandLineParameters("--protocol");
        if (cmdLineParams.size() > 0)
            protocol = cmdLineParams[0];
        else
            ::LogError("--protocol specified without a parameter! Using UDP protocol as default.");
    }
    if (protocol.isEmpty())
        protocol = "udp";

    kNet::SocketTransportLayer transportLayer = kNet::SocketOverUDP; // By default operate over UDP.

    if (protocol.compare("tcp", Qt::CaseInsensitive) == 0)
        transportLayer = kNet::SocketOverTCP;
    else if (protocol.compare("udp", Qt::CaseInsensitive) == 0)
        transportLayer = kNet::SocketOverUDP;
    else
        ::LogError("Invalid server protocol '" + protocol + "' specified! Using UDP protocol as default.");

    // Start server
    if (!owner_->GetKristalliModule()->StartServer(port, transportLayer))
    {
        ::LogError("Failed to start server in port " + ToString<int>(port));
        return false;
    }

    // Store current port and protocol
    current_port_ = (int)port;
    current_protocol_ = (transportLayer == kNet::SocketOverUDP) ? "udp" : "tcp";

    // Create the default server scene
    /// \todo Should be not hard coded like this. Give some unique id (uuid perhaps) that could be returned to the client to make the corresponding named scene in client?
    ScenePtr scene = framework_->Scene()->CreateScene(sceneID_, true, true);
//    framework_->Scene()->SetDefaultScene(scene);
    //owner_->GetSyncManager()->RegisterToScene(scene);

    emit ServerStarted();

    KristalliProtocolModule *kristalli = framework_->GetModule<KristalliProtocolModule>();
    connect(kristalli, SIGNAL(NetworkMessageReceived(kNet::MessageConnection *, kNet::packet_id_t, kNet::message_id_t, const char *, size_t)), 
        this, SLOT(HandleKristalliMessage(kNet::MessageConnection*, kNet::packet_id_t, kNet::message_id_t, const char*, size_t)), Qt::UniqueConnection);

    connect(kristalli, SIGNAL(ClientDisconnectedEvent(UserConnection *)), this, SLOT(HandleUserDisconnected(UserConnection *)), Qt::UniqueConnection);

    return true;
}

void Server::Stop()
{
    if (owner_->IsServer())
    {
        ::LogInfo("Stopped Tundra server. Removing TundraServer scene.");

        owner_->GetKristalliModule()->StopServer();
        framework_->Scene()->RemoveScene(sceneID_);
        
        emit ServerStopped();

        KristalliProtocolModule *kristalli = framework_->GetModule<KristalliProtocolModule>();
        disconnect(kristalli, SIGNAL(NetworkMessageReceived(kNet::MessageConnection *, kNet::packet_id_t, kNet::message_id_t, const char *, size_t)), 
            this, SLOT(HandleKristalliMessage(kNet::MessageConnection*, kNet::packet_id_t, kNet::message_id_t, const char*, size_t)));

        disconnect(kristalli, SIGNAL(ClientDisconnectedEvent(UserConnection *)), this, SLOT(HandleUserDisconnected(UserConnection *)));
    }
}

bool Server::IsRunning() const
{
    return owner_->IsServer();
}

bool Server::IsAboutToStart() const
{
    return framework_->HasCommandLineParameter("--server");
}

int Server::GetPort() const
{
    return current_port_;
}

QString Server::GetProtocol() const
{
    return current_protocol_;
}

UserConnectionList Server::GetAuthenticatedUsers() const
{
    UserConnectionList ret;
    
    UserConnectionList& all = owner_->GetKristalliModule()->GetUserConnections();
    for(UserConnectionList::const_iterator iter = all.begin(); iter != all.end(); ++iter)
    {
        if ((*iter)->properties["authenticated"] == "true")
            ret.push_back(*iter);
    }
    
    return ret;
}

QVariantList Server::GetConnectionIDs() const
{
    QVariantList ret;
    
    UserConnectionList users = GetAuthenticatedUsers();
    for(UserConnectionList::const_iterator iter = users.begin(); iter != users.end(); ++iter)
        ret.push_back(QVariant((*iter)->userID));
    
    return ret;
}

UserConnection* Server::GetUserConnection(int connectionID) const
{
    UserConnectionList users = GetAuthenticatedUsers();
    for(UserConnectionList::const_iterator iter = users.begin(); iter != users.end(); ++iter)
    {
        if ((*iter)->userID == connectionID)
            return ((*iter).get());
    }
    
    return 0;
}

UserConnectionList& Server::GetUserConnections() const
{
    return owner_->GetKristalliModule()->GetUserConnections();
}

UserConnection* Server::GetUserConnection(kNet::MessageConnection* source) const
{
    return owner_->GetKristalliModule()->GetUserConnection(source);
}

void Server::SetActionSender(UserConnection* user)
{
    actionsender_ = user;
}

UserConnection* Server::GetActionSender() const
{
    return actionsender_;
}

kNet::NetworkServer *Server::GetServer() const
{
    if (!owner_ || !owner_->GetKristalliModule())
        return 0;

    return owner_->GetKristalliModule()->GetServer();
}

void Server::HandleKristalliMessage(kNet::MessageConnection* source, kNet::packet_id_t packetId, kNet::message_id_t messageId, const char* data, size_t numBytes)
{
    if (!source)
        return;

    if (!owner_->IsServer())
        return;

    UserConnection *user = GetUserConnection(source);
    if (!user)
    {
        ::LogWarning("Server: dropping message " + ToString(messageId) + " from unknown connection \"" + source->ToString() + "\"");
        return;
    }

    // If we are server, only allow the login message from an unauthenticated user
    if (messageId != cLoginMessage && user->properties["authenticated"] != "true")
    {
        UserConnection* user = GetUserConnection(source);
        if ((!user) || (user->properties["authenticated"] != "true"))
        {
            ::LogWarning("Server: dropping message " + ToString(messageId) + " from unauthenticated user");
            /// \todo something more severe, like disconnecting the user
            return;
        }
    }
    else if (messageId == cLoginMessage)
    {
        MsgLogin msg(data, numBytes);
        HandleLogin(source, msg);
    }

    emit MessageReceived(user, packetId, messageId, data, numBytes);
}

void Server::HandleLogin(kNet::MessageConnection* source, const MsgLogin& msg)
{
    UserConnection* user = GetUserConnection(source);
    if (!user)
    {
        ::LogWarning("Login message from unknown user");
        return;
    }
    
    QDomDocument xml;
    QString loginData = QString::fromStdString(BufferToString(msg.loginData));
    bool success = xml.setContent(loginData);
    if (!success)
        ::LogWarning("Received malformed xml logindata from user " + ToString<int>(user->userID));
    
    // Fill the user's logindata, both in raw format and as keyvalue pairs
    user->loginData = loginData;
    QDomElement rootElem = xml.firstChildElement();
    QDomElement keyvalueElem = rootElem.firstChildElement();
    while(!keyvalueElem.isNull())
    {
        //::LogInfo("Logindata contains keyvalue pair " + keyvalueElem.tagName() + " = " + keyvalueElem.attribute("value);
        user->SetProperty(keyvalueElem.tagName(), keyvalueElem.attribute("value"));
        keyvalueElem = keyvalueElem.nextSiblingElement();
    }
    
    user->properties["authenticated"] = "true";
    emit UserAboutToConnect(user->userID, user);
    if (user->properties["authenticated"] != "true")
    {
        ::LogInfo("User with connection ID " + ToString<int>(user->userID) + " was denied access");
        MsgLoginReply reply;
        reply.success = 0;
        reply.userID = 0;
        QByteArray responseByteData = user->properties["reason"].toAscii();
        reply.loginReplyData.insert(reply.loginReplyData.end(), responseByteData.data(), responseByteData.data() + responseByteData.size());
        user->connection->Send(reply);
        return;
    }
    
    ::LogInfo("User with connection ID " + ToString<int>(user->userID) + " logged in");
    
    // Allow entityactions & EC sync from now on
    MsgLoginReply reply;
    reply.success = 1;
    reply.userID = user->userID;
    // Send unique scene name to client who creates scene with this name.
    reply.uuid = StringToBuffer(sceneID_.toStdString());
    
    // Tell everyone of the client joining (also the user who joined)
    UserConnectionList users = GetAuthenticatedUsers();
    MsgClientJoined joined;
    joined.userID = user->userID;
    for(UserConnectionList::const_iterator iter = users.begin(); iter != users.end(); ++iter)
        (*iter)->connection->Send(joined);
    
    // Advertise the users who already are in the world, to the new user
    for(UserConnectionList::const_iterator iter = users.begin(); iter != users.end(); ++iter)
    {
        if ((*iter)->userID != user->userID)
        {
            MsgClientJoined joined;
            joined.userID = (*iter)->userID;
            user->connection->Send(joined);
        }
    }
    
    // Tell syncmanager of the new user
    owner_->GetSyncManager()->NewUserConnected(user);
    
    // Tell all server-side application code that a new user has successfully connected.
    // Ask them to fill the contents of a UserConnectedResponseData structure. This will
    // be sent to the client so that the scripts and applications on the client system can configure themselves.
    UserConnectedResponseData responseData;
    emit UserConnected(user->userID, user, &responseData);

    QByteArray responseByteData = responseData.responseData.toByteArray(-1);
    reply.loginReplyData.insert(reply.loginReplyData.end(), responseByteData.data(), responseByteData.data() + responseByteData.size());
    user->connection->Send(reply);
}

void Server::HandleUserDisconnected(UserConnection* user)
{
    // Tell everyone of the client leaving
    MsgClientLeft left;
    left.userID = user->userID;
    UserConnectionList users = GetAuthenticatedUsers();
    for(UserConnectionList::const_iterator iter = users.begin(); iter != users.end(); ++iter)
    {
        if ((*iter)->userID != user->userID)
            (*iter)->connection->Send(left);
    }
    
    emit UserDisconnected(user->userID, user);
}

template<typename T>
QScriptValue qScriptValueFromNull(QScriptEngine *engine, const T &v)
{
    return QScriptValue();
}

template<typename T>
void qScriptValueToNull(const QScriptValue &value, T &v)
{
}

void Server::OnScriptEngineCreated(QScriptEngine* engine)
{
    qScriptRegisterQObjectMetaType<UserConnection*>(engine);
    qScriptRegisterQObjectMetaType<SyncManager*>(engine);
    qScriptRegisterQObjectMetaType<SceneSyncState*>(engine);
    qScriptRegisterQObjectMetaType<StateChangeRequest*>(engine);
    ///\todo Write proper serialization and deserialization.
    qScriptRegisterMetaType<UserConnectedResponseData*>(engine, qScriptValueFromNull<UserConnectedResponseData*>, qScriptValueToNull<UserConnectedResponseData*>);
}

}
