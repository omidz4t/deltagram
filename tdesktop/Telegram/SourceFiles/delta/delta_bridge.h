/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#pragma once

#include "delta/delta_rpc.h"
#include "delta/draft_store.h"
#include "delta/reply_store.h"
#include "delta/compose_queue.h"
#include "delta/compose_batch.h"
#include "delta/history_mutations.h"
#include "delta/contact_blocking.h"
#include "delta/full_message.h"
#include "mtproto/core_types.h"
#include "rpl/producer.h"

#include <QByteArray>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <memory>
#include <optional>
#include <vector>

class HistoryItem;

namespace Main {
class Account;
class Session;
} // namespace Main

namespace MTP {
class Instance;
} // namespace MTP

namespace Delta {

class PreparedSend;
// Holds the composer/quit reservation until preparation is abandoned or Core
// confirms ownership. Carries no protocol data into the preparation workers.
[[nodiscard]] std::shared_ptr<PreparedSend> BeginPreparedSend(int userId);
[[nodiscard]] std::shared_ptr<ComposeBatch> BeginPreparedBatch(int userId,
	std::vector<ComposeDraft> selection, Fn<void(bool)> done,
	std::vector<ForwardSource> forwards = {});
[[nodiscard]] std::optional<std::vector<ForwardSource>> CollectForwardSources(
	const std::vector<not_null<HistoryItem*>> &items);
[[nodiscard]] std::shared_ptr<ComposeBatch> PendingPreparedBatch(int userId);
void CheckNewAttachmentBatch(int userId, Fn<void(QString)> done);

void AnswerRequest(
	not_null<MTP::Instance*> instance,
	mtpRequestId requestId,
	const mtpBuffer &request);

void StartSession(
	not_null<Main::Account*> account,
	std::shared_ptr<DeltaRpc> rpc,
	int accountId,
	const QString &displayName);

[[nodiscard]] bool Active();
[[nodiscard]] int AccountGeneration();
[[nodiscard]] std::function<bool()> CaptureAccountGuard();
[[nodiscard]] rpl::producer<int> AccountViewResets();
void SetChatMute(int userId, int durationSeconds);
void SendNotificationReply(uint64 userId, QString text);
void LoadSavedReplies(Fn<void(std::vector<SavedReply>, QString)> done);
void SendSavedReply(SavedReply reviewed, Fn<void(QString)> done);
void DiscardSavedReply(SavedReply reviewed, Fn<void(QString)> done);
[[nodiscard]] rpl::producer<int> ComposerChanges();
[[nodiscard]] bool ComposerWritable(int userId);
[[nodiscard]] std::optional<bool> ComposerSendKnown(int userId);
[[nodiscard]] bool ComposerSendAllowed(int userId);
[[nodiscard]] std::optional<bool> ComposerContactRequest(int userId);
[[nodiscard]] bool ComposerDeclineIsBlock(int userId);
[[nodiscard]] bool ComposerCanDeclineContactRequest(int userId);
[[nodiscard]] bool ComposerDeclineNeedsDeletionConfirm(int userId);
[[nodiscard]] bool ContactRequestBusy(int userId);
void AcceptContactRequest(int userId, Fn<void(QString)> done);
void DeclineContactRequest(int userId, bool confirmed, Fn<void(QString)> done);
[[nodiscard]] QString ComposerSendUnavailable(int userId);
void WatchComposerChat(int userId);
void ObserveComposerChat(int userId);
[[nodiscard]] bool PreparingSendPreventsQuit();
[[nodiscard]] bool BackupExportInProgress();
void SendComposerText(int userId, const QString &text, int quotedMessageId,
	Fn<void(bool)> done);
void SendComposerAttachment(int userId, ComposeDraft draft, Fn<void(bool)> done);
void DiscardDraftAttachment(int userId, const QString &expectedFile, Fn<void(bool)> done);
[[nodiscard]] QString DraftAttachmentName(int userId);
void SaveDraft(int userId, const QString &text, int quotedMessageId);
void LoadDraft(int userId, Fn<void(ComposeDraft)> done);
void LoadComposeQueue(int userId, Fn<void(ComposeQueue, QString)> done);
void SendComposeQueue(int userId, ComposeQueue reviewed, Fn<void(bool)> done);
void DiscardComposeQueueEntry(int userId, ComposeQueue reviewed, Fn<void(bool)> done);
void RequestMessageData(int userId, int messageId, Fn<void()> done);
enum class DeliveryState { Pending, Failed, Sent, Read };
[[nodiscard]] std::optional<DeliveryState> MessageDelivery(int messageId);
[[nodiscard]] bool CanEditMessage(int messageId);
void ApplyMessageData(not_null<HistoryItem*> item);
[[nodiscard]] QString MessageAuthorOverride(not_null<HistoryItem*> item);
[[nodiscard]] bool MessageShowsCoreAuthor(not_null<HistoryItem*> item);
[[nodiscard]] QString QuoteAuthorName(
	not_null<HistoryItem*> quoting,
	HistoryItem *resolved);
[[nodiscard]] bool CanDownloadMessage(int messageId);
void DownloadMessage(int messageId);
[[nodiscard]] bool OwnsSession(not_null<Main::Session*> session);
[[nodiscard]] bool HasFullMessage(not_null<HistoryItem*> item);
void LoadFullMessage(
	not_null<Main::Session*> session,
	int messageId,
	const AccountScope &scope,
	Fn<void(FullMessageResult)> done);
[[nodiscard]] std::optional<int> ChannelViewCount(int messageId);
void MessageInfo(int messageId, Fn<void(QString text, QString error)> done);
void DeleteContact(int contactId, bool deleteChat, Fn<void(QString error)> done);
void ListBlockedContacts(Fn<void(std::vector<BlockedContact>, QString)> done);
void SetContactBlocked(int contactId, bool blocked, Fn<void(QString)> done);
[[nodiscard]] QJsonObject SharedContactData(int messageId);
[[nodiscard]] std::optional<QString> StatusSubtitle(uint64 userId);
[[nodiscard]] bool IsChannel(uint64 userId);
[[nodiscard]] bool IsGroup(uint64 userId);
[[nodiscard]] bool IsDirectChat(uint64 userId);
[[nodiscard]] bool IsReadOnlyChannel(uint64 userId);
[[nodiscard]] rpl::producer<bool> ChannelWriteAccess(uint64 userId);
[[nodiscard]] rpl::producer<QString> ChannelStatusValue(uint64 userId);
void ChannelRequest(int userId, const QString &method, const QJsonArray &args,
	Fn<void(QJsonValue result, QString error)> done);
void AccountRequest(const QString &method, const QJsonArray &args,
	Fn<void(QJsonValue result, QString error)> done);
void JoinInviteChat(const QString &qr, Fn<void(int chatId, QString error)> done);
[[nodiscard]] std::shared_ptr<DeltaRpc> SharedRpc();
[[nodiscard]] QString SelfAddress();
[[nodiscard]] bool IsSystemUser(uint64 userId);
[[nodiscard]] rpl::producer<int> Connectivity(
	not_null<Main::Session*> session);
void FetchConnectivityHtml(
	not_null<Main::Session*> session,
	Fn<void(QString html)> done);
void FetchUrl(const QString &url, Fn<void(QByteArray body, QString error)> done);
[[nodiscard]] QImage PhotoImage(uint64 photoId);

void AddContact(
	const QString &name,
	const QString &email,
	Fn<void(int userId, QString error)> done);

void RememberPendingFile(uint64 peer, int64 msg, const QString &path,
	const QString &filename, std::shared_ptr<void> owner, bool forceFile,
	std::shared_ptr<PreparedSend> preparation = {});
void ForgetPendingFile(uint64 peer, int64 msg);
bool SendItemMedia(not_null<HistoryItem*> item, Fn<void(bool)> done);
[[nodiscard]] bool CanRetryPreparedAttachment(not_null<HistoryItem*> item);
void RetryPreparedAttachment(not_null<HistoryItem*> item, Fn<void(bool)> done);
bool SendItemText(not_null<HistoryItem*> item, const QString &text, Fn<void(bool)> done);
void SendReaction(int messageId, const QJsonArray &reactions);
void SetSelfAvatar(const QString &path, Fn<void(QString)> done);
void FindMessageIds(
	int userId,
	const QString &query,
	Fn<void(QVector<int> ids, QString error)> done);
void LoadSearchResults(
	const QVector<int> &ids,
	Fn<void(QVector<MTPMessage>, QVector<MTPUser>, QString error)> done);

void LoadInviteQr(
	Fn<void(QString name, QString text, QString svg, QString error)> done);

void ExportProfileBackup(
	not_null<Main::Session*> session,
	const QString &destination,
	const QString &passphrase,
	Fn<void(QString directory, QString error)> done);
void ListTransports(
	not_null<Main::Session*> session,
	Fn<void(QJsonArray transports, QString error)> done);
void UpdateTransport(const QJsonObject &transport, Fn<void(QString error)> done);
void DeleteTransport(const QString &addr, Fn<void(QString error)> done);
void AddTransportFromQr(const QString &qr, Fn<void(QString error)> done);

void EditMessageText(int messageId, const QString &text, Fn<void(QString error)> done);
void DeleteMessages(const QVector<int> &ids, bool revoke, Fn<void()> done);
void DeleteHistory(bool self, int userId, bool justClear, bool revoke,
	Fn<void(HistoryDeletionResult)> done,
	std::optional<HistoryDateRange> dates = std::nullopt);
[[nodiscard]] std::optional<bool> MessageUnread(int messageId);

void SetChatArchived(
	bool self,
	int userId,
	bool archived,
	Fn<void(QString error)> done);

[[nodiscard]] Fn<void()> StartSecondDevice(
	Fn<void(QString svg, QString error)> ready,
	Fn<void(QString error)> finished);

void JoinInviteQr(const QString &qr, Fn<void(QString error)> done);

void SetSelfName(const QString &name, Fn<void(QString error)> done);
void SetSelfAbout(const QString &about, Fn<void(QString error)> done);

struct ContactRow {
	int id = 0;
	QString name;
	QString address;
	QString avatar;
	qint64 lastSeen = 0;
};
[[nodiscard]] std::optional<ContactRow> ContactForPeer(uint64 userId);

void ListContacts(
	Fn<void(std::vector<ContactRow> contacts, QString error)> done,
	bool addressOnly = false);
void OpenContact(const ContactRow &contact, Fn<void(int, QString)> done);
void InspectGroupMember(
	int chatId,
	int contactId,
	Fn<void(ContactRow contact, uint64 userId, QString error)> done);
// Mint kContactUserIdBase+contactId into SenderContacts. Self is refused.
[[nodiscard]] bool RegisterContactForProfile(const ContactRow &contact);
void ShareContact(int contactId, int peerUserId, Fn<void(QString)> done);

void CreateGroup(
	const QString &name,
	const QString &description,
	const std::vector<int> &contactIds,
	const QString &imagePath,
	Fn<void(int chatId, QString error)> done);
void CreateChannel(const QString &name, Fn<void(int chatId, QString error)> done);
void CreateChannel(const QString &name, const QString &description,
	const QString &imagePath, Fn<void(int chatId, QString error)> done);
void SendChannelInvite(int channelUserId, int recipientUserId, Fn<void(QString error)> done);
void LoadChannelInvite(int userId, Fn<void(QString text, QString svg, QString error)> done);

void ListProfiles(Fn<void(QJsonArray accounts)> done);
[[nodiscard]] rpl::producer<int> ProfileUnreadCountValue(int accountId);
[[nodiscard]] int TotalProfileUnreadCount();
[[nodiscard]] int InactiveProfileUnreadCount();
[[nodiscard]] rpl::producer<> ProfileActivityChanges();
void RemoveProfile(int accountId, Fn<void(QString error)> done);
void MoveProfileToTop(int accountId, Fn<void(QString error)> done);
void SwitchProfile(int accountId, Fn<void(QString error)> done);
void AddProfile(const QString &name, Fn<void(QString error)> done);

} // namespace Delta
