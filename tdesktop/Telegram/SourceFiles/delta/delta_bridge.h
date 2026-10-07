/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#pragma once

#include "delta/delta_rpc.h"
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
} // namespace Main

namespace MTP {
class Instance;
} // namespace MTP

namespace Delta {

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
[[nodiscard]] std::optional<int> ChannelViewCount(int messageId);
void MessageInfo(int messageId, Fn<void(QString text, QString error)> done);
void DeleteContact(int contactId, bool deleteChat, Fn<void(QString error)> done);
[[nodiscard]] QJsonObject SharedContactData(int messageId);
[[nodiscard]] std::optional<QString> StatusSubtitle(uint64 userId);
[[nodiscard]] bool IsChannel(uint64 userId);
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
[[nodiscard]] bool IsSystemUser(int userId);
[[nodiscard]] rpl::producer<int> Connectivity();
void FetchConnectivityHtml(Fn<void(QString html)> done);
void FetchUrl(const QString &url, Fn<void(QByteArray body, QString error)> done);
[[nodiscard]] QImage PhotoImage(uint64 photoId);

void AddContact(
	const QString &name,
	const QString &email,
	Fn<void(int userId, QString error)> done);

void RememberPendingFile(uint64 peer, int64 msg, const QString &path);
bool SendItemMedia(not_null<HistoryItem*> item, Fn<void(bool)> done);
bool SendItemText(not_null<HistoryItem*> item, const QString &text, Fn<void(bool)> done);
void SendReaction(int messageId, const QJsonArray &reactions);
void SetSelfAvatar(const QString &path, Fn<void(QString)> done);
void SearchMessages(
	int userId,
	const QString &query,
	Fn<void(QVector<MTPMessage>, QVector<MTPUser>)> done);

void LoadInviteQr(
	Fn<void(QString name, QString text, QString svg, QString error)> done);

void ListTransports(Fn<void(QJsonArray transports, QString error)> done);
void UpdateTransport(const QJsonObject &transport, Fn<void(QString error)> done);
void DeleteTransport(const QString &addr, Fn<void(QString error)> done);
void AddTransportFromQr(const QString &qr, Fn<void(QString error)> done);

void EditMessageText(int messageId, const QString &text, Fn<void(QString error)> done);

void SetChatArchived(
	bool self,
	int userId,
	bool archived,
	Fn<void(QString error)> done);

void StartSecondDevice(
	Fn<void(QString svg, QString error)> ready,
	Fn<void(QString error)> finished);
void StopSecondDevice();

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

void ListContacts(
	Fn<void(std::vector<ContactRow> contacts, QString error)> done,
	bool addressOnly = false);
void OpenContact(const ContactRow &contact, Fn<void(int, QString)> done);
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
void RemoveProfile(int accountId, Fn<void(QString error)> done);
void MoveProfileToTop(int accountId, Fn<void(QString error)> done);
void SwitchProfile(int accountId, Fn<void(QString error)> done);
void AddProfile(const QString &name, Fn<void(QString error)> done);

} // namespace Delta
