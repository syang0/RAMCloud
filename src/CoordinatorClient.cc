/* Copyright (c) 2010-2012 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "CoordinatorClient.h"
#include "CoordinatorSession.h"
#include "ShortMacros.h"
#include "ProtoBuf.h"

namespace RAMCloud {

/**
 * Servers call this when they come online. This request tells the coordinator
 * that the server is available and can be assigned work.
 *
 * \param context
 *      Overall information about the RAMCloud server or client.
 * \param replacesId
 *      Server id the calling server used to operate at; the coordinator must
 *      make sure this server is removed from the cluster before enlisting
 *      the calling server.  If !isValid() then this step is skipped; the
 *      enlisting server is simply added.
 * \param serviceMask
 *      Which services are available on the enlisting server. MASTER_SERVICE,
 *      BACKUP_SERVICE, etc.
 * \param localServiceLocator
 *      Describes how other hosts can contact this server.
 * \param readSpeed
 *      Read speed of the backup in MB/s if serviceMask includes BACKUP,
 *      otherwise ignored.
 * \return
 *      A ServerId guaranteed never to have been used before.
 */
ServerId
CoordinatorClient::enlistServer(Context* context, ServerId replacesId,
        ServiceMask serviceMask, string localServiceLocator,
        uint32_t readSpeed)
{
    EnlistServerRpc rpc(context, replacesId, serviceMask, localServiceLocator,
            readSpeed);
    return rpc.wait();
}

/**
 * Constructor for EnlistServerRpc: initiates an RPC in the same way as
 * #CoordinatorClient::enlistServer, but returns once the RPC has been
 * initiated, without waiting for it to complete.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param replacesId
 *      Server id the calling server used to operate at; the coordinator must
 *      make sure this server is removed from the cluster before enlisting
 *      the calling server.  If !isValid() then this step is skipped; the
 *      enlisting server is simply added.
 * \param serviceMask
 *      Which services are available on the enlisting server. MASTER_SERVICE,
 *      BACKUP_SERVICE, etc.
 * \param localServiceLocator
 *      Describes how other hosts can contact this server.
 * \param readSpeed
 *      Read speed of the backup in MB/s if serviceMask includes BACKUP,
 *      otherwise ignored.
 * \return
 *      A ServerId guaranteed never to have been used before.
 */
EnlistServerRpc::EnlistServerRpc(Context* context,
        ServerId replacesId, ServiceMask serviceMask,
        string localServiceLocator, uint32_t readSpeed)
    : CoordinatorRpcWrapper(context,
            sizeof(WireFormat::EnlistServer::Response))
{
    WireFormat::EnlistServer::Request* reqHdr(
            allocHeader<WireFormat::EnlistServer>());
    reqHdr->replacesId = replacesId.getId();
    reqHdr->serviceMask = serviceMask.serialize();
    reqHdr->readSpeed = readSpeed;
    reqHdr->serviceLocatorLength =
        downCast<uint32_t>(localServiceLocator.length() + 1);
    strncpy(new(&request, APPEND) char[reqHdr->serviceLocatorLength],
            localServiceLocator.c_str(),
            reqHdr->serviceLocatorLength);
    send();
}

/**
 * Wait for an enlistServer RPC to complete, and return the same results as
 * #CoordinatorClient::enlistServer.
 */
ServerId
EnlistServerRpc::wait()
{
    waitInternal(context->dispatch);
    const WireFormat::EnlistServer::Response* respHdr(
            getResponseHeader<WireFormat::EnlistServer>());
    if (respHdr->common.status != STATUS_OK)
        ClientException::throwException(HERE, respHdr->common.status);
    return ServerId(respHdr->serverId);
}

/**
 * Return information about all backups currently active in the cluster.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param[out] serverList
 *      Will be filled in with information about all backup servers.
 */
void
CoordinatorClient::getBackupList(Context* context,
        ProtoBuf::ServerList* serverList)
{
    GetServerListRpc rpc(context, {WireFormat::BACKUP_SERVICE});
    rpc.wait(serverList);
}

/**
 * Return information about all masters currently active in the cluster.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param[out] serverList
 *      Will be filled in with information about all masters.
 */
void
CoordinatorClient::getMasterList(Context* context,
        ProtoBuf::ServerList* serverList)
{
    GetServerListRpc rpc(context, {WireFormat::MASTER_SERVICE});
    rpc.wait(serverList);
}

/**
 * Return information about all servers currently active in the cluster.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param[out] serverList
 *      Will be filled in with information about all active servers.
 */
void
CoordinatorClient::getServerList(Context* context,
        ProtoBuf::ServerList* serverList)
{
    GetServerListRpc rpc(context, {WireFormat::MASTER_SERVICE,
            WireFormat::BACKUP_SERVICE});
    rpc.wait(serverList);
}

/**
 * Constructor for GetServerListRpc: initiates an RPC in the same way as
 * #CoordinatorClient::getServerList, but returns once the RPC has been
 * initiated, without waiting for it to complete.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param[in] services
 *      A list of services (typically one or both of MASTER_SERVICE and
 *      BACKUP_SERVICE): the results will contain only servers that offer
 *      at least one of the specified services.
 */
GetServerListRpc::GetServerListRpc(Context* context,
            ServiceMask services)
    : CoordinatorRpcWrapper(context,
            sizeof(WireFormat::GetServerList::Response))
{
    WireFormat::GetServerList::Request* reqHdr(
            allocHeader<WireFormat::GetServerList>());
    reqHdr->serviceMask = services.serialize();
    send();
}

/**
 * Wait for a getServerList RPC to complete, and return the same results as
 * #CoordinatorClient::getServerList.
 *
 * \param[out] serverList
 *      Will be filled in with server information returned from the
 *      coordinator.
 */
void
GetServerListRpc::wait(ProtoBuf::ServerList* serverList)
{
    waitInternal(context->dispatch);
    const WireFormat::GetServerList::Response* respHdr(
            getResponseHeader<WireFormat::GetServerList>());
    if (respHdr->common.status != STATUS_OK)
        ClientException::throwException(HERE, respHdr->common.status);
    ProtoBuf::parseFromResponse(response, sizeof(*respHdr),
                                respHdr->serverListLength, serverList);
}

/**
 * Retrieve tablet configuration information, which indicates the master
 * server that stores each object in the system.  Clients use this to direct
 * requests to the appropriate server.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param[out] tabletMap
 *      Will be filled in with information about all tablets that currently
 *      exist.
 */
void
CoordinatorClient::getTabletMap(Context* context,
        ProtoBuf::Tablets* tabletMap)
{
    GetTabletMapRpc rpc(context);
    rpc.wait(tabletMap);
}

/**
 * Constructor for GetTabletMapRpc: initiates an RPC in the same way as
 * #CoordinatorClient::getTabletMap, but returns once the RPC has been
 * initiated, without waiting for it to complete.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 */
GetTabletMapRpc::GetTabletMapRpc(Context* context)
    : CoordinatorRpcWrapper(context,
            sizeof(WireFormat::GetTabletMap::Response))
{
    allocHeader<WireFormat::GetTabletMap>();
    send();
}

/**
 * Wait for a getTabletMap RPC to complete, and return the same results as
 * #CoordinatorClient::getTabletMap.
 *
 * \param[out] tabletMap
 *      Will be filled in with information about all tablets that currently
 *      exist.
 */
void
GetTabletMapRpc::wait(ProtoBuf::Tablets* tabletMap)
{
    waitInternal(context->dispatch);
    const WireFormat::GetTabletMap::Response* respHdr(
            getResponseHeader<WireFormat::GetTabletMap>());
    if (respHdr->common.status != STATUS_OK)
        ClientException::throwException(HERE, respHdr->common.status);
    ProtoBuf::parseFromResponse(response, sizeof(*respHdr),
                                respHdr->tabletMapLength, tabletMap);
}

/**
 * This method is invoked to notify the coordinator of problems communicating
 * with a particular server, suggesting that the server may have crashed.  The
 * coordinator will perform its own checks to see if the server is alive, and
 * initiate recovery actions if it is dead.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param serverId
 *      Identifies a server that appears to have crashed.
 */
void
CoordinatorClient::hintServerDown(Context* context, ServerId serverId)
{
    HintServerDownRpc rpc(context, serverId);
    rpc.wait();
}

/**
 * Constructor for HintServerDownRpc: initiates an RPC in the same way as
 * #CoordinatorClient::hintServerDown, but returns once the RPC has been
 * initiated, without waiting for it to complete.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param serverId
 *      Identifies a server that appears to have crashed.
 */
HintServerDownRpc::HintServerDownRpc(Context* context,
        ServerId serverId)
    : CoordinatorRpcWrapper(context,
            sizeof(WireFormat::HintServerDown::Response))
{
    WireFormat::HintServerDown::Request* reqHdr(
            allocHeader<WireFormat::HintServerDown>());
    reqHdr->serverId = serverId.getId();
    send();
}

/**
 * This method is invoked after migrating all the data for a tablet to another
 * master; it instructs the coordinator to transfer ownership of that data from
 * this server to \a newOwnerId and alert the new master so that it will
 * begin processing requests on the tablet.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param[in] tableId
 *      TableId of the tablet that was migrated.
 * \param[in] firstKeyHash
 *      First key hash value in the range of the tablet that was migrated.
 * \param[in] lastKeyHash
 *      Last key hash value in the range of the tablet that was migrated.
 * \param[in] newOwnerId
 *      ServerId of the master that we want ownership of the tablet
 *      to be transferred to.
 * \param[in] ctimeSegmentId 
 *      Id of the new owner's head segment immediately before migration begins
 *      and after prepForMigration has completed. This is used to record the
 *      creation time of the migration tablet on the coordinator, which in turn
 *      allows RAMCloud to skip over obsolete data from prior instances of the
 *      tablet during recovery.
 * \param[in] ctimeSegmentOffset
 *      Offset within the head segment prior to migration. See ctimeSegmentId.
 */
void
CoordinatorClient::reassignTabletOwnership(Context* context, uint64_t tableId,
        uint64_t firstKeyHash, uint64_t lastKeyHash, ServerId newOwnerId,
        uint64_t ctimeSegmentId, uint32_t ctimeSegmentOffset)
{
    ReassignTabletOwnershipRpc rpc(context, tableId, firstKeyHash,
            lastKeyHash, newOwnerId, ctimeSegmentId, ctimeSegmentOffset);
    rpc.wait();
}

/**
 * Constructor for ReassignTabletOwnershipRpc: initiates an RPC in the same
 * way as #CoordinatorClient::reassignTabletOwnership, but returns once the
 * RPC has been initiated, without waiting for it to complete.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param[in] tableId
 *      TableId of the tablet that was migrated.
 * \param[in] firstKeyHash
 *      First key hash value in the range of the tablet that was migrated.
 * \param[in] lastKeyHash
 *      Last key hash value in the range of the tablet that was migrated.
 * \param[in] newOwnerId
 *      ServerId of the master that we want ownership of the tablet
 *      to be transferred to.
 * \param[in] ctimeSegmentId 
 *      Id of the new owner's head segment immediately before migration begins
 *      and after prepForMigration has completed. This is used to record the
 *      creation time of the migration tablet on the coordinator, which in turn
 *      allows RAMCloud to skip over obsolete data from prior instances of the
 *      tablet during recovery.
 * \param[in] ctimeSegmentOffset
 *      Offset within the head segment prior to migration. See ctimeSegmentId.
 */
ReassignTabletOwnershipRpc::ReassignTabletOwnershipRpc(Context* context,
        uint64_t tableId, uint64_t firstKeyHash, uint64_t lastKeyHash,
        ServerId newOwnerId, uint64_t ctimeSegmentId,
        uint32_t ctimeSegmentOffset)
    : CoordinatorRpcWrapper(context,
            sizeof(WireFormat::ReassignTabletOwnership::Response))
{
    WireFormat::ReassignTabletOwnership::Request* reqHdr(
            allocHeader<WireFormat::ReassignTabletOwnership>());
    reqHdr->tableId = tableId;
    reqHdr->firstKeyHash = firstKeyHash;
    reqHdr->lastKeyHash = lastKeyHash;
    reqHdr->newOwnerId = newOwnerId.getId();
    reqHdr->ctimeSegmentId = ctimeSegmentId;
    reqHdr->ctimeSegmentOffset = ctimeSegmentOffset;
    send();
}

/**
 * This method is invoked by a recovery master to inform the coordinator that
 * it has completed recovering a partition of a crashed master that was
 * assigned to it (or has failed in trying).
 *
 * \param context
 *      Overall information about this RAMCloud server.
 * \param recoveryId
 *      Identifies the recovery this master has completed a portion of.
 *      This id is received as part of the recover rpc and should simply
 *      be returned as given by the coordinator.
 * \param recoveryMasterId
 *      ServerId of the server invoking this method.
 * \param tablets
 *      The tablets in the partition that was recovered.
 * \param successful
 *      Indicates to the coordinator whether this recovery master succeeded
 *      in recovering its partition of the crashed master. If false the
 *      coordinator will not assign ownership to this master and this master
 *      can clean up any state resulting attempting recovery.
 */
void
CoordinatorClient::recoveryMasterFinished(Context* context, uint64_t recoveryId,
        ServerId recoveryMasterId, const ProtoBuf::Tablets* tablets,
        bool successful)
{
    RecoveryMasterFinishedRpc rpc(context, recoveryId, recoveryMasterId,
            tablets, successful);
    rpc.wait();
}

/**
 * Constructor for RecoveryMasterFinishedRpc: initiates an RPC in the same
 * way as #CoordinatorClient::recoveryMasterFinished, but returns once the
 * RPC has been initiated, without waiting for it to complete.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param recoveryId
 *      Identifies the recovery this master has completed a portion of.
 *      This id is received as part of the recover rpc and should simply
 *      be returned as given by the coordinator.
 * \param recoveryMasterId
 *      ServerId of the server invoking this method.
 * \param tablets
 *      The tablets in the partition that was recovered.
 * \param successful
 *      Indicates to the coordinator whether this recovery master succeeded
 *      in recovering its partition of the crashed master. If false the
 *      coordinator will not assign ownership to this master and this master
 *      can clean up any state resulting attempting recovery.
 */
RecoveryMasterFinishedRpc::RecoveryMasterFinishedRpc(Context* context,
        uint64_t recoveryId, ServerId recoveryMasterId,
        const ProtoBuf::Tablets* tablets, bool successful)
    : CoordinatorRpcWrapper(context,
            sizeof(WireFormat::RecoveryMasterFinished::Response))
{
    WireFormat::RecoveryMasterFinished::Request* reqHdr(
            allocHeader<WireFormat::RecoveryMasterFinished>());
    reqHdr->recoveryId = recoveryId;
    reqHdr->recoveryMasterId = recoveryMasterId.getId();
    reqHdr->tabletsLength = serializeToRequest(&request, tablets);
    reqHdr->successful = successful;
    send();
}

/**
 * Masters invoke this RPC as a way of invalidating obsolete (and potentially
 * inconsistent) segment replicas that were open on backups when they (appear
 * to have) crashed.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param serverId
 *      Identifies a particular server.
 * \param recoveryInfo
 *      Information the coordinator will need to safely recover the master
 *      at \a serverId. The information is opaque to the coordinator other
 *      than its master recovery routines, but, basically, this is used to
 *      prevent inconsistent open replicas from being used during recovery.
 */
void
CoordinatorClient::setMasterRecoveryInfo(
    Context* context,
    ServerId serverId,
    const ProtoBuf::MasterRecoveryInfo& recoveryInfo)
{
    SetMasterRecoveryInfoRpc rpc(context, serverId, recoveryInfo);
    rpc.wait();
}

/**
 * Constructor for SetMasterRecoveryInfoRpc: initiates an RPC in the same way as
 * #CoordinatorClient::setMasterRecoveryInfo, but returns once the RPC has been
 * initiated, without waiting for it to complete.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param serverId
 *      Identifies a particular server.
 * \param recoveryInfo
 *      Information the coordinator will need to safely recover the master
 *      at \a serverId. The information is opaque to the coordinator other
 *      than its master recovery routines, but, basically, this is used to
 *      prevent inconsistent open replicas from being used during recovery.
 */
SetMasterRecoveryInfoRpc::SetMasterRecoveryInfoRpc(
    Context* context,
    ServerId serverId,
    const ProtoBuf::MasterRecoveryInfo& recoveryInfo)
    : CoordinatorRpcWrapper(context,
            sizeof(WireFormat::SetMasterRecoveryInfo::Response))
{
    WireFormat::SetMasterRecoveryInfo::Request* reqHdr(
            allocHeader<WireFormat::SetMasterRecoveryInfo>());
    reqHdr->serverId = serverId.getId();
    reqHdr->infoLength = serializeToRequest(&request, &recoveryInfo);
    send();
}

/**
 * Masters typically invoke this RPC when they suspect that they may
 * no longer be part of the cluster (e.g., they haven't been able to
 * communicate with other servers for a long time, or some other
 * server seems to think this server isn't in its server list). This
 * RPC checks with the coordinator to be sure and commits suicide if
 * the coordinator doesn't recognize us.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param serverId
 *      A server whose membership is in question; typically it's
 *      the id of the server invoking this method.
 * \param suicideOnFailure
 *      True (default) means that the process should exit if it turns
 *      out that we are no longer part of the cluster.
 */
void
CoordinatorClient::verifyMembership(
    Context* context,
    ServerId serverId,
    bool suicideOnFailure)
{
    VerifyMembershipRpc rpc(context, serverId);
    rpc.wait(suicideOnFailure);
}

/**
 * Constructor for VerifyMembershipRpc: initiates an RPC in the same way as
 * #CoordinatorClient::verifyMembership, but returns once the RPC has been
 * initiated, without waiting for it to complete.
 *
 * \param context
 *      Overall information about this RAMCloud server or client.
 * \param serverId
 *      A server whose membership is in question; typically it's
 *      the id of the server invoking this method.
 */
VerifyMembershipRpc::VerifyMembershipRpc(
    Context* context,
    ServerId serverId)
    : CoordinatorRpcWrapper(context,
            sizeof(WireFormat::VerifyMembership::Response))
{
    RAMCLOUD_LOG(WARNING,
            "verifying cluster membership for %s",
            serverId.toString().c_str());
    WireFormat::VerifyMembership::Request* reqHdr(
            allocHeader<WireFormat::VerifyMembership>());
    reqHdr->serverId = serverId.getId();
    send();
}

/**
 * Wait for a verifyMembership RPC to complete, and return the same results as
 * #CoordinatorClient::verifyMembership.
 *
 * \param suicideOnFailure
 *      True (default) means that the process should exit if it turns
 *      out that we are no longer part of the cluster.
 *
 * \throw CallerNotInClusterException
 *      This server is no longer part of the cluster.
 */
void
VerifyMembershipRpc::wait(bool suicideOnFailure)
{
    waitInternal(context->dispatch);
    const WireFormat::VerifyMembership::Response* respHdr(
            getResponseHeader<WireFormat::VerifyMembership>());
    if (respHdr->common.status != STATUS_OK) {
        if ((respHdr->common.status == STATUS_CALLER_NOT_IN_CLUSTER)
                && suicideOnFailure) {
            RAMCLOUD_LOG(WARNING,
                    "server no longer in cluster; committing suicide");
            exit(1);
        }
        ClientException::throwException(HERE, respHdr->common.status);
    }
}

} // namespace RAMCloud
