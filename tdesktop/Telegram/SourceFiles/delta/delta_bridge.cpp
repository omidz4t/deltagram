/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#include "delta/delta_bridge.h"
#include "delta/avatar_image.h"

#include "base/weak_ptr.h"
#include "core/file_location.h"
#include "data/data_document.h"
#include "data/data_changes.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_main_list.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "mtproto/details/mtproto_serialized_request.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_response.h"
#include "rpl/variable.h"
#include "ui/image/image_location.h"
#include "ui/image/image_prepare.h"

#include <QBuffer>
#include <QDateTime>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QTemporaryFile>

#include <algorithm>
#include <limits>
#include <memory>

namespace Delta {
namespace {

constexpr auto kSelfUserId = 1'000'000;
constexpr auto kPhotoIdBase = qint64(0x110000000);
constexpr auto kDocumentIdBase = qint64(0x120000000);

[[nodiscard]] bool HasError(const QJsonObject &reply) {
	return reply.contains(u"error"_q);
}

[[nodiscard]] QString ErrorText(const QJsonObject &reply) {
	const auto error = reply.value(u"error"_q);
	if (error.isObject()) {
		const auto message = error.toObject().value(u"message"_q).toString();
		if (!message.isEmpty()) {
			return message;
		}
	}
	return u"Request failed."_q;
}

QHash<qint64, QString> AvatarFiles;
QHash<qint64, QByteArray> AvatarPng;

struct PeerSubtitle {
	qint64 lastSeen = 0;
	QString line;
};

QHash<int, PeerSubtitle> PeerSubtitles;
QHash<int, QJsonObject> ChannelInfos;
QHash<int, int> ChannelViews;
QHash<int, QJsonObject> SharedContacts;
rpl::variable<int> ChannelRevision = 0;

void RememberSubtitle(int userId, qint64 lastSeen, const QString &line) {
	auto &slot = PeerSubtitles[userId];
	slot.lastSeen = lastSeen;
	slot.line = line;
}

[[nodiscard]] MTPUserProfilePhoto MakeProfilePhoto(const QString &path) {
	if (path.isEmpty() || !QFileInfo::exists(path)) {
		return MTP_userProfilePhotoEmpty();
	}
	const auto info = QFileInfo(path);
	const auto photoId = qint64(qHash(
		path + QString::number(info.lastModified().toMSecsSinceEpoch())
			+ QString::number(info.size())))
		+ 1;
	AvatarFiles.insert(photoId, path);
	return MTP_userProfilePhoto(
		MTP_flags(0),
		MTP_long(photoId),
		MTPbytes(),
		MTP_int(2));
}

[[nodiscard]] MTPUser MakeUser(
		int id,
		const QString &name,
		bool self,
		const QString &avatar = QString()) {
	auto flags = MTPDuser::Flags(MTPDuser::Flag::f_first_name);
	if (self) {
		flags |= MTPDuser::Flag::f_self;
	} else {
		flags |= MTPDuser::Flag::f_contact;
	}
	const auto photo = MakeProfilePhoto(avatar);
	if (photo.type() == mtpc_userProfilePhoto) {
		flags |= MTPDuser::Flag::f_photo;
	}
	const auto known = PeerSubtitles.constFind(id);
	const auto lastSeen = (!self && known != PeerSubtitles.cend())
		? known->lastSeen
		: qint64(0);
	if (lastSeen > 0) {
		flags |= MTPDuser::Flag::f_status;
	}
	const auto wasOnline = int(std::min(
		lastSeen,
		qint64(std::numeric_limits<int>::max())));
	return MTP_user(
		MTP_flags(flags),
		MTP_long(id),
		MTPlong(), // access_hash
		MTP_string(name.isEmpty() ? u"Unknown"_q : name),
		MTPstring(), // last_name
		MTPstring(), // username
		MTPstring(), // phone
		photo,
		(lastSeen > 0)
			? MTP_userStatusOffline(MTP_int(wasOnline))
			: MTPUserStatus(),
		MTPint(), // bot_info_version
		MTPVector<MTPRestrictionReason>(),
		MTPstring(), // bot_inline_placeholder
		MTPstring(), // lang_code
		MTPEmojiStatus(),
		MTPVector<MTPUsername>(),
		MTPRecentStory(),
		MTPPeerColor(), // color
		MTPPeerColor(), // profile_color
		MTPint(), // bot_active_users
		MTPlong(), // bot_verification_icon
		MTPlong(), // send_paid_messages_stars
		MTPlong()); // linked_community_id
}

QHash<qint64, QString> PhotoFiles;
QHash<qint64, QString> DocumentFiles;
QHash<int, QJsonObject> CoreMessages;
QHash<int, int> CoreReadReceipts;
QHash<QString, QString> PendingUploads;

[[nodiscard]] QString PendingKey(uint64 peer, int64 msg) {
	return QString::number(peer) + ':' + QString::number(msg);
}

[[nodiscard]] bool LooksLikeImage(
		const QString &kind,
		const QString &mime,
		const QString &path) {
	if (kind == u"Image"_q || kind == u"Sticker"_q || kind == u"Gif"_q) {
		return true;
	}
	if (mime.startsWith(u"image/"_q, Qt::CaseInsensitive)) {
		return true;
	}
	const auto lower = path.toLower();
	return lower.endsWith(u".jpg"_q)
		|| lower.endsWith(u".jpeg"_q)
		|| lower.endsWith(u".png"_q)
		|| lower.endsWith(u".webp"_q)
		|| lower.endsWith(u".gif"_q)
		|| lower.endsWith(u".bmp"_q);
}

[[nodiscard]] bool LooksLikeVideo(
		const QString &kind,
		const QString &mime,
		const QString &path) {
	if (kind == u"Video"_q || mime.startsWith(u"video/"_q, Qt::CaseInsensitive)) {
		return true;
	}
	const auto lower = path.toLower();
	return lower.endsWith(u".mp4"_q)
		|| lower.endsWith(u".m4v"_q)
		|| lower.endsWith(u".mov"_q)
		|| lower.endsWith(u".webm"_q)
		|| lower.endsWith(u".mkv"_q)
		|| lower.endsWith(u".avi"_q);
}

[[nodiscard]] bool LooksLikeVoice(
		const QString &kind,
		const QString &mime,
		const QString &path) {
	if (kind == u"Voice"_q || kind == u"Audio"_q) {
		return true;
	}
	if (mime.startsWith(u"audio/"_q, Qt::CaseInsensitive)) {
		return true;
	}
	const auto lower = path.toLower();
	return lower.endsWith(u".ogg"_q)
		|| lower.endsWith(u".oga"_q)
		|| lower.endsWith(u".opus"_q)
		|| lower.endsWith(u".m4a"_q)
		|| lower.endsWith(u".mp3"_q);
}

[[nodiscard]] QString VoiceMime(const QString &mime, const QString &path) {
	if (mime.startsWith(u"audio/"_q, Qt::CaseInsensitive)) {
		return mime;
	}
	const auto lower = path.toLower();
	if (lower.endsWith(u".mp3"_q)) {
		return u"audio/mpeg"_q;
	}
	if (lower.endsWith(u".m4a"_q)) {
		return u"audio/mp4"_q;
	}
	return u"audio/ogg"_q;
}

[[nodiscard]] MTPMessageMedia MakePhotoMedia(
		int id,
		const QString &path,
		int width,
		int height,
		qint64 bytes,
		int date) {
	auto w = width;
	auto h = height;
	if (w <= 0 || h <= 0) {
		const auto image = QImage(path);
		if (!image.isNull()) {
			w = image.width();
			h = image.height();
		}
	}
	const auto photoId = kPhotoIdBase + id;
	PhotoFiles.insert(photoId, path);
	auto read = Images::Read({
		.path = path,
		.maxSize = QSize(1280, 1280),
		.returnContent = true,
	});
	auto shown = std::move(read.image);
	auto fileBytes = std::move(read.content);
	if (fileBytes.isEmpty() && bytes > 0 && bytes <= 8 * 1024 * 1024) {
		auto file = QFile(path);
		if (file.open(QIODevice::ReadOnly)) {
			fileBytes = file.readAll();
		}
	}
	if (!shown.isNull()) {
		auto ready = fileBytes;
		if (ready.isEmpty()) {
			auto buffer = QBuffer(&ready);
			shown.save(&buffer, "JPG", 85);
		}
		KeepReadyPhoto(PhotoId(photoId), shown, ready);
		if (fileBytes.isEmpty()) {
			fileBytes = ready;
		}
		if (w <= 0) {
			w = shown.width();
		}
		if (h <= 0) {
			h = shown.height();
		}
	}
	const auto size = fileBytes.isEmpty()
		? MTP_photoSize(
			MTP_string("y"),
			MTP_int(w > 0 ? w : 800),
			MTP_int(h > 0 ? h : 600),
			MTP_int(int(bytes)))
		: MTP_photoCachedSize(
			MTP_string("y"),
			MTP_int(w > 0 ? w : 800),
			MTP_int(h > 0 ? h : 600),
			MTP_bytes(fileBytes));
	return MTP_messageMediaPhoto(
		MTP_flags(MTPDmessageMediaPhoto::Flags(
			MTPDmessageMediaPhoto::Flag::f_photo)),
		MTP_photo(
			MTP_flags(0),
			MTP_long(photoId),
			MTP_long(1),
			MTP_bytes(QByteArray()),
			MTP_int(date),
			MTP_vector<MTPPhotoSize>(1, size),
			MTPVector<MTPVideoSize>(),
			MTP_int(2)),
		MTPint(),
		MTPDocument());
}

[[nodiscard]] MTPMessageMedia MakeVoiceMedia(
		int id,
		const QString &path,
		const QString &mime,
		const QString &name,
		int durationMs,
		qint64 bytes,
		int date) {
	const auto documentId = kDocumentIdBase + id;
	DocumentFiles.insert(documentId, path);
	const auto seconds = (durationMs > 0)
		? std::max(1, (durationMs + 999) / 1000)
		: 0;
	auto fileName = name.isEmpty() ? QFileInfo(path).fileName() : name;
	auto attributes = QVector<MTPDocumentAttribute>();
	attributes.push_back(MTP_documentAttributeAudio(
		MTP_flags(MTPDdocumentAttributeAudio::Flag::f_voice),
		MTP_int(seconds),
		MTPstring(),
		MTPstring(),
		MTPbytes()));
	attributes.push_back(MTP_documentAttributeFilename(MTP_string(fileName)));
	return MTP_messageMediaDocument(
		MTP_flags(MTPDmessageMediaDocument::Flags(
			MTPDmessageMediaDocument::Flag::f_document
			| MTPDmessageMediaDocument::Flag::f_voice)),
		MTP_document(
			MTP_flags(0),
			MTP_long(documentId),
			MTP_long(1),
			MTP_bytes(QByteArray()),
			MTP_int(date),
			MTP_string(VoiceMime(mime, path)),
			MTP_long(bytes > 0 ? bytes : QFileInfo(path).size()),
			MTPVector<MTPPhotoSize>(),
			MTPVector<MTPVideoSize>(),
			MTP_int(2),
			MTP_vector<MTPDocumentAttribute>(attributes)),
		MTPVector<MTPDocument>(),
		MTPPhoto(),
		MTPint(),
		MTPint());
}

[[nodiscard]] MTPMessage MakeMessage(
		int id,
		int peerUser,
		bool out,
		const QString &text,
		int date,
		const MTPMessageMedia &media = MTP_messageMediaEmpty(),
		const QString &forwardName = QString(),
		const MTPMessageReactions &reactions = MTPMessageReactions(),
		int replyId = 0,
		const QString &quoteText = QString()) {
	auto flags = MTPDmessage::Flags(MTPDmessage::Flag::f_from_id);
	if (out) {
		flags |= MTPDmessage::Flag::f_out;
	}
	if (media.type() != mtpc_messageMediaEmpty) {
		flags |= MTPDmessage::Flag::f_media;
	}
	if (reactions.type() == mtpc_messageReactions) {
		flags |= MTPDmessage::Flag::f_reactions;
	}
	auto reply = MTPMessageReplyHeader();
	if (replyId > 0 || !quoteText.isEmpty()) {
		flags |= MTPDmessage::Flag::f_reply_to;
		using Flag = MTPDmessageReplyHeader::Flag;
		auto header = MTPDmessageReplyHeader::Flags(0);
		if (replyId > 0) {
			header |= Flag::f_reply_to_msg_id;
		}
		if (!quoteText.isEmpty()) {
			header |= Flag::f_quote | Flag::f_quote_text;
		}
		reply = MTP_messageReplyHeader(
			MTP_flags(header),
			replyId > 0 ? MTP_int(replyId) : MTPint(),
			MTPPeer(),
			MTPMessageFwdHeader(),
			MTPMessageMedia(),
			MTPint(),
			MTP_string(quoteText),
			MTPVector<MTPMessageEntity>(),
			MTPint(),
			MTPint(),
			MTPbytes());
	}
	auto forward = MTPMessageFwdHeader();
	if (!forwardName.isEmpty()) {
		flags |= MTPDmessage::Flag::f_fwd_from;
		forward = MTP_messageFwdHeader(
			MTP_flags(MTPDmessageFwdHeader::Flag::f_from_name),
			MTPPeer(),
			MTP_string(forwardName),
			MTP_int(date),
			MTPint(),
			MTPstring(),
			MTPPeer(),
			MTPint(),
			MTPPeer(),
			MTPstring(),
			MTPint(),
			MTPstring());
	}
	return MTP_message(
		MTP_flags(flags),
		MTP_int(id),
		MTP_peerUser(MTP_long(out ? kSelfUserId : peerUser)),
		MTPint(), // from_boosts_applied
		MTPstring(), // from_rank
		MTP_peerUser(MTP_long(peerUser)),
		MTPPeer(), // saved_peer_id
		forward,
		MTPlong(), // via_bot_id
		MTPlong(), // via_business_bot_id
		MTPPeer(), // guestchat_via_from
		reply,
		MTP_int(date),
		MTP_string(text),
		media,
		MTPReplyMarkup(),
		MTP_vector<MTPMessageEntity>(),
		MTPint(), // views
		MTPint(), // forwards
		MTPMessageReplies(),
		MTPint(), // edit_date
		MTPstring(), // post_author
		MTPlong(), // grouped_id
		reactions,
		MTPVector<MTPRestrictionReason>(),
		MTPint(), // ttl_period
		MTPint(), // quick_reply_shortcut_id
		MTPlong(), // effect
		MTPFactCheck(),
		MTPint(), // report_delivery_until_date
		MTPlong(), // paid_message_stars
		MTPSuggestedPost(),
		MTPint(), // schedule_repeat_period
		MTPstring(), // summary_from_language
		MTPRichMessage());
}

[[nodiscard]] MTPMessage MakeServiceMessage(
		int id,
		int peerUser,
		bool out,
		const QString &text,
		int date) {
	auto flags = MTPDmessageService::Flags(MTPDmessageService::Flag::f_from_id);
	if (out) {
		flags |= MTPDmessageService::Flag::f_out;
	}
	return MTP_messageService(
		MTP_flags(flags),
		MTP_int(id),
		MTP_peerUser(MTP_long(out ? kSelfUserId : peerUser)),
		MTP_peerUser(MTP_long(peerUser)),
		MTPPeer(),
		MTPMessageReplyHeader(),
		MTP_int(date),
		MTP_messageActionCustomAction(MTP_string(text)),
		MTPMessageReactions(),
		MTPint());
}

[[nodiscard]] MTPMessageReactions ReactionsFromJson(const QJsonObject &data) {
	const auto list = data.value(u"reactions"_q).toObject()
		.value(u"reactions"_q).toArray();
	auto counts = QVector<MTPReactionCount>();
	for (const auto &value : list) {
		const auto one = value.toObject();
		const auto emoji = one.value(u"emoji"_q).toString();
		const auto count = one.value(u"count"_q).toInt();
		if (emoji.isEmpty() || count < 1) {
			continue;
		}
		const auto mine = one.value(u"isFromSelf"_q).toBool();
		counts.push_back(MTP_reactionCount(
			MTP_flags(mine
				? MTPDreactionCount::Flag::f_chosen_order
				: MTPDreactionCount::Flags(0)),
			mine ? MTP_int(0) : MTPint(),
			MTP_reactionEmoji(MTP_string(emoji)),
			MTP_int(count)));
	}
	if (counts.isEmpty()) {
		return MTPMessageReactions();
	}
	return MTP_messageReactions(
		MTP_flags(MTPDmessageReactions::Flag::f_can_see_list),
		MTP_vector<MTPReactionCount>(counts),
		MTPVector<MTPMessagePeerReaction>(),
		MTPVector<MTPMessageReactor>());
}

[[nodiscard]] MTPMessage MessageFromJson(
		int peerUser,
		const QJsonObject &data) {
	CoreMessages.insert(data.value(u"id"_q).toInt(), data);
	auto text = data.value(u"text"_q).toString();
	const auto kind = data.value(u"viewType"_q).toString();
	if (kind == u"Vcard"_q) {
		auto contact = data.value(u"vcardContact"_q).toObject();
		if (!contact.value(u"addr"_q).toString().isEmpty()) {
			contact.insert(u"file"_q, data.value(u"file"_q));
			SharedContacts.insert(data.value(u"id"_q).toInt(), contact);
			text.clear();
		}
	}
	const auto id = data.value(u"id"_q).toInt();
	const auto date = int(data.value(u"timestamp"_q).toInteger());
	if (data.value(u"isInfo"_q).toBool()) {
		return MakeServiceMessage(
			id,
			peerUser,
			data.value(u"fromId"_q).toInt() == 1,
			text,
			date);
	}
	const auto path = data.value(u"file"_q).toString();
	const auto mime = data.value(u"fileMime"_q).toString();
	auto media = MTPMessageMedia(MTP_messageMediaEmpty());
	if (!path.isEmpty()
		&& QFileInfo::exists(path)
		&& LooksLikeImage(kind, mime, path)) {
		media = MakePhotoMedia(
			id,
			path,
			data.value(u"dimensionsWidth"_q).toInt(),
			data.value(u"dimensionsHeight"_q).toInt(),
			QFileInfo(path).size(),
			date);
	} else if (!path.isEmpty()
		&& QFileInfo::exists(path)
		&& LooksLikeVoice(kind, mime, path)) {
		const auto named = data.value(u"fileBytes"_q).toInteger();
		media = MakeVoiceMedia(
			id,
			path,
			mime,
			data.value(u"fileName"_q).toString(),
			data.value(u"duration"_q).toInt(),
			named > 0 ? named : QFileInfo(path).size(),
			date);
	} else if (text.isEmpty() && !kind.isEmpty() && kind != u"Text"_q) {
		const auto name = data.value(u"fileName"_q).toString();
		text = name.isEmpty() ? u"[%1]"_q.arg(kind) : u"[%1] %2"_q.arg(kind, name);
	}
	if (media.type() != mtpc_messageMediaEmpty) {
		const auto mark = text.lastIndexOf(u" ["_q);
		const auto start = (mark >= 0)
			? mark + 1
			: (text.startsWith('[') ? 0 : -1);
		if (start >= 0
			&& text.endsWith(']')
			&& text.mid(start).contains(u" – "_q)) {
			text = text.left(start).trimmed();
		}
	}
	auto forwardName = QString();
	if (data.value(u"isForwarded"_q).toBool()) {
		forwardName = data.value(u"overrideSenderName"_q).toString();
		if (forwardName.isEmpty()) {
			forwardName = data.value(u"sender"_q).toObject()
				.value(u"displayName"_q).toString();
		}
		if (forwardName.isEmpty()) {
			forwardName = u"Forwarded"_q;
		}
	}
	const auto quote = data.value(u"quote"_q).toObject();
	const auto quoteText = quote.value(u"text"_q).toString();
	// Core's parentId is email threading, not an explicit quote.
	const auto replyId = quote.value(u"messageId"_q).toInt();
	return MakeMessage(
		id,
		peerUser,
		data.value(u"fromId"_q).toInt() == 1,
		text,
		date,
		media,
		forwardName,
		ReactionsFromJson(data),
		replyId,
		quoteText);
}

[[nodiscard]] MTPDialog MakeDialog(
		int peerUser,
		int topMessage,
		int unread,
		bool pinned,
		bool muted) {
	auto flags = MTPDdialog::Flags(0);
	if (pinned) {
		flags |= MTPDdialog::Flag::f_pinned;
	}
	const auto readInbox = unread > 0 ? topMessage - unread : topMessage;
	return MTP_dialog(
		MTP_flags(flags),
		MTP_peerUser(MTP_long(peerUser)),
		MTP_int(topMessage),
		MTP_int(std::max(readInbox, 0)),
		MTP_int(topMessage),
		MTP_int(unread),
		MTP_int(0), // unread_mentions_count
		MTP_int(0), // unread_reactions_count
		MTP_int(0), // unread_poll_votes_count
		MTP_peerNotifySettings(
			MTP_flags(muted
				? MTPDpeerNotifySettings::Flags(
					MTPDpeerNotifySettings::Flag::f_mute_until)
				: MTPDpeerNotifySettings::Flags(0)),
			MTPBool(),
			MTPBool(),
			muted ? MTP_int(0x7fffffff) : MTPint(),
			MTPNotificationSound(),
			MTPNotificationSound(),
			MTPNotificationSound(),
			MTPBool(),
			MTPBool(),
			MTPNotificationSound(),
			MTPNotificationSound(),
			MTPNotificationSound()),
		MTPint(), // pts
		MTPDraftMessage(),
		MTPint(), // folder_id
		MTPint()); // ttl_period
}

class WordReader {
public:
	explicit WordReader(const mtpBuffer &buffer)
	: _bytes(reinterpret_cast<const uchar*>(buffer.constData()))
	, _size(buffer.size() * 4)
	, _position(
		MTP::details::SerializedRequest::kMessageBodyPosition * 4) {
	}

	[[nodiscard]] bool ok() const {
		return _ok;
	}
	[[nodiscard]] uint32 u32() {
		if (_position + 4 > _size) {
			_ok = false;
			return 0;
		}
		const auto result = uint32(_bytes[_position])
			| (uint32(_bytes[_position + 1]) << 8)
			| (uint32(_bytes[_position + 2]) << 16)
			| (uint32(_bytes[_position + 3]) << 24);
		_position += 4;
		return result;
	}
	[[nodiscard]] int32 i32() {
		return int32(u32());
	}
	[[nodiscard]] int64 i64() {
		const auto low = uint64(u32());
		const auto high = uint64(u32());
		return int64(low | (high << 32));
	}
	[[nodiscard]] QString string() {
		if (_position >= _size) {
			_ok = false;
			return QString();
		}
		auto length = uint32(_bytes[_position]);
		auto header = 1;
		if (length >= 254) {
			if (_position + 4 > _size) {
				_ok = false;
				return QString();
			}
			length = uint32(_bytes[_position + 1])
				| (uint32(_bytes[_position + 2]) << 8)
				| (uint32(_bytes[_position + 3]) << 16);
			header = 4;
		}
		if (_position + header + length > _size) {
			_ok = false;
			return QString();
		}
		const auto result = QString::fromUtf8(
			reinterpret_cast<const char*>(_bytes + _position + header),
			int(length));
		const auto total = header + int(length);
		_position += (total + 3) & ~3;
		return result;
	}

private:
	const uchar *_bytes = nullptr;
	int _size = 0;
	int _position = 0;
	bool _ok = true;

};

[[nodiscard]] int ReadInputPeerUser(WordReader &reader) {
	const auto type = reader.u32();
	if (type == mtpc_inputPeerSelf) {
		return kSelfUserId;
	} else if (type == mtpc_inputPeerUser) {
		const auto id = reader.i64();
		reader.i64();
		return int(id);
	} else if (type == mtpc_inputPeerChat) {
		return int(reader.i64());
	}
	return 0;
}

class Bridge;
Bridge *GlobalBridge = nullptr;
QString GlobalSelfAddress;
QColor GlobalSelfAvatarColor;
QSet<int> GlobalSystemUsers;

class Bridge final {
public:
	Bridge(
		not_null<Main::Session*> session,
		std::shared_ptr<DeltaRpc> rpc,
		int accountId,
		const QString &selfName)
	: _session(session)
	, _rpc(std::move(rpc))
	, _accountId(accountId)
	, _selfName(selfName) {
		GlobalBridge = this;
		_reload.setSingleShot(true);
		_reload.setInterval(150);
		QObject::connect(&_reload, &QTimer::timeout, [=] {
			loadDialogs();
		});
	}
	~Bridge() {
		if (GlobalBridge == this) {
			GlobalBridge = nullptr;
			GlobalSelfAvatarColor = QColor();
			CoreMessages.clear();
			CoreReadReceipts.clear();
		}
	}

	void start() {
		_rpc->setEventHandler([=](QJsonObject) {});
		const auto guard = _alive;
		_rpc->call(
			u"get_config"_q,
			{ _accountId, u"configured_addr"_q },
			[=](QJsonObject reply) {
				if (!guard.expired() && !HasError(reply)) {
					GlobalSelfAddress = reply.value(u"result"_q).toString();
				}
			});
		loadDialogs();
		loadSelfAvatar();
		refreshConnectivity();
		pump();
	}

	void loadSelfAvatar() {
		const auto guard = _alive;
		const auto accountId = _accountId;
		const auto revision = ++_selfAvatarRevision;
		_rpc->call(
			u"get_account_info"_q,
			{ accountId },
			[=](QJsonObject reply) {
				if (guard.expired() || accountId != _accountId
					|| revision != _selfAvatarRevision || HasError(reply)) {
					return;
				}
				GlobalSelfAvatarColor = QColor(reply.value(u"result"_q)
					.toObject().value(u"color"_q).toString());
				_session->changes().peerUpdated(
					_session->user(), Data::PeerUpdate::Flag::Photo);
			});
		_rpc->call(
			u"get_config"_q,
			{ accountId, u"selfavatar"_q },
			[=](QJsonObject reply) {
				if (guard.expired() || accountId != _accountId
					|| revision != _selfAvatarRevision || HasError(reply)) {
					return;
				}
				const auto path = reply.value(u"result"_q).toString();
				_selfAvatar = path;
				showSelfAvatar();
			});
	}

	void showSelfAvatar() {
		_session->data().processUsers(MTP_vector<MTPUser>(
			QVector<MTPUser>{ selfUser() }));
		_session->changes().peerUpdated(
			_session->user(), Data::PeerUpdate::Flag::Photo);
	}

	void refreshConnectivity() {
		const auto guard = _alive;
		_rpc->call(
			u"get_connectivity"_q,
			{ _accountId },
			[=](QJsonObject reply) {
				if (guard.expired() || HasError(reply)) {
					return;
				}
				_connectivity = reply.value(u"result"_q).toInt();
			});
	}

	rpl::producer<int> connectivity() const {
		return _connectivity.value();
	}

	void fetchUrl(const QString &url, Fn<void(QByteArray, QString)> done) {
		const auto guard = _alive;
		_rpc->call(
			u"get_http_response"_q,
			{ _accountId, url },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				if (HasError(reply)) {
					done(QByteArray(), ErrorText(reply));
					return;
				}
				auto encoded = reply.value(u"result"_q).toObject().value(u"blob"_q).toString().toLatin1();
				while (encoded.size() % 4) {
					encoded.append('=');
				}
				done(QByteArray::fromBase64(encoded), QString());
			});
	}

	void fetchConnectivityHtml(Fn<void(QString)> done) {
		const auto guard = _alive;
		_rpc->call(
			u"get_connectivity_html"_q,
			{ _accountId },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				done(HasError(reply)
					? QString()
					: reply.value(u"result"_q).toString());
			});
	}

	void fetchMessage(int msgId, Fn<void(QJsonObject)> done) {
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		const auto accountId = _accountId;
		const auto tries = std::make_shared<int>(0);
		const auto run = std::make_shared<Fn<void()>>();
		*run = [=] {
			_rpc->call(
				u"get_message"_q,
				{ accountId, msgId },
				[=](QJsonObject one) {
					if (guard.expired()) {
						return;
					}
					if (epoch != _dialogsEpoch || _deletedMessages.contains(msgId)) {
						done(QJsonObject{{ u"error"_q, u"Message no longer available"_q }});
						return;
					}
					if (!HasError(one)) {
						const auto data = one.value(u"result"_q).toObject();
						const auto path = data.value(u"file"_q).toString();
						const auto kind = data.value(u"viewType"_q).toString();
						const auto mime = data.value(u"fileMime"_q).toString();
						const auto image = LooksLikeImage(kind, mime, path);
						const auto voice = LooksLikeVoice(kind, mime, path);
						const auto state = data.value(u"downloadState"_q).toString();
						const auto pending = (state == u"Available"_q)
							|| (state == u"InProgress"_q);
						const auto ready = !pending
							&& !path.isEmpty()
							&& QFileInfo::exists(path)
							&& QFileInfo(path).size() > 0;
						const auto limit = 8;
						if ((image || voice || LooksLikeVideo(kind, mime, path)) && !ready && *tries < limit) {
							++(*tries);
							_rpc->call(
								u"download_full_message"_q,
								{ _accountId, msgId },
								[=](QJsonObject) {
									if (!guard.expired()) {
										(*run)();
									}
								});
							return;
						}
					}
					const auto data = one.value(u"result"_q).toObject();
					const auto chatId = data.value(u"chatId"_q).toInt();
					if (!HasError(one) && !data.value(u"isInfo"_q).toBool()
						&& (ChannelInfos.value(chatId).value(u"chatType"_q).toString() == u"OutBroadcast"_q
							|| (data.value(u"fromId"_q).toInt() == 1
								&& data.value(u"state"_q).toInt() >= 26))) {
						_rpc->call(u"get_message_read_receipt_count"_q, { accountId, msgId }, [=](QJsonObject count) {
							if (guard.expired() || epoch != _dialogsEpoch || _deletedMessages.contains(msgId)) return;
							if (!HasError(count)) {
								CoreReadReceipts.insert(msgId, count.value(u"result"_q).toInt());
								if (ChannelInfos.value(chatId).value(u"chatType"_q).toString() == u"OutBroadcast"_q) {
									ChannelViews.insert(msgId, count.value(u"result"_q).toInt());
								}
							}
							done(one);
							// Refresh after the callback has applied Core's new state.
							if (const auto item = _session->data().message(
									peerFromUser(UserId(peerUserForChat(chatId))), MsgId(msgId))) {
								_session->data().notifyItemDataChange(item);
								_session->data().requestItemViewRefresh(item);
							}
						});
					} else {
						done(one);
					}
				});
		};
		(*run)();
	}

	void answer(
			not_null<MTP::Instance*> instance,
			mtpRequestId requestId,
			const mtpBuffer &request) {
		auto reader = WordReader(request);
		const auto type = reader.u32();
		switch (type) {
		case mtpc_messages_getHistory:
			return answerHistory(instance, requestId, reader);
		case mtpc_messages_sendMessage:
			return answerSend(instance, requestId, reader);
		case mtpc_messages_forwardMessages:
			return answerForward(instance, requestId, reader);
		case mtpc_messages_readHistory:
			return answerRead(instance, requestId, reader);
		case mtpc_upload_getFile:
			return answerFile(instance, requestId, reader);
		case mtpc_messages_search:
			return answerSearch(instance, requestId, reader, false);
		case mtpc_messages_searchGlobal:
			return answerSearch(instance, requestId, reader, true);
		case mtpc_contacts_search:
			return answerContacts(instance, requestId, reader);
		case mtpc_messages_deleteMessages:
			return answerDelete(instance, requestId, reader);
		}
		fail(instance, requestId, u"DELTA_NOT_SUPPORTED"_q);
	}

	void answerDelete(
			not_null<MTP::Instance*> instance,
			mtpRequestId requestId,
			WordReader &reader) {
		const auto flags = reader.u32();
		const auto revoke = (flags & 1) != 0;
		auto count = reader.i32();
		if (uint32(count) == mtpc_vector) {
			count = reader.i32();
		}
		if (count < 0 || count > 1000) {
			return fail(instance, requestId, u"DELTA_BAD_REQUEST"_q);
		}
		auto ids = QJsonArray();
		for (auto i = 0; i < count; ++i) {
			ids.append(reader.i32());
		}
		if (!reader.ok()) {
			return fail(instance, requestId, u"DELTA_BAD_REQUEST"_q);
		}
		const auto boxed = MTPmessages_AffectedMessages(
			MTP_messages_affectedMessages(MTP_int(0), MTP_int(0)));
		reply(instance, requestId, [=](mtpBuffer &to) {
			boxed.write(to);
		});
		_rpc->call(
			revoke ? u"delete_messages_for_all"_q : u"delete_messages"_q,
			{ _accountId, ids },
			[](QJsonObject) {});
	}

	void sendText(
			int userId,
			const QString &text,
			int quotedId,
			Fn<void(int, QString)> done) {
		auto html = text.toHtmlEscaped();
		html.replace(u"\n"_q, u"<br>"_q);
		QJsonObject data;
		data.insert(u"text"_q, text);
		data.insert(u"html"_q, html);
		if (quotedId > 0) {
			data.insert(u"quotedMessageId"_q, quotedId);
		}
		const auto guard = _alive;
		_rpc->call(
			u"send_msg"_q,
			{ _accountId, chatForPeerUser(userId), data },
			[=](QJsonObject result) {
				if (guard.expired()) {
					return;
				}
				if (HasError(result)) {
					done(0, ErrorText(result));
					return;
				}
				const auto id = result.value(u"result"_q).toInt();
				CoreMessages.insert(id, QJsonObject{{ u"state"_q, 20 }});
				done(id, QString());
				addIncoming(chatForPeerUser(userId), id);
			});
	}

	void sendFile(
			int userId,
			const QString &path,
			const QString &caption,
			const QString &viewType,
			int quotedId,
			Fn<void(int, QString)> done) {
		QJsonObject data;
		if (!caption.isEmpty()) {
			data.insert(u"text"_q, caption);
		}
		if (quotedId > 0) {
			data.insert(u"quotedMessageId"_q, quotedId);
		}
		data.insert(u"file"_q, path);
		if (!viewType.isEmpty()) {
			data.insert(u"viewtype"_q, viewType);
		}
		const auto guard = _alive;
		_rpc->call(
			u"send_msg"_q,
			{ _accountId, chatForPeerUser(userId), data },
			[=](QJsonObject result) {
				if (guard.expired()) {
					return;
				}
				if (HasError(result)) {
					done(0, ErrorText(result));
					return;
				}
				const auto id = result.value(u"result"_q).toInt();
				CoreMessages.insert(id, QJsonObject{{ u"state"_q, 20 }});
				done(id, QString());
				addIncoming(chatForPeerUser(userId), id);
			});
	}

	void sendReaction(int messageId, const QJsonArray &reactions) {
		if (messageId <= 0 || IsClientMsgId(MsgId(messageId))) {
			return;
		}
		const auto guard = _alive;
		_rpc->call(
			u"send_reaction"_q,
			{ _accountId, messageId, reactions },
			[=](QJsonObject) {
				if (guard.expired()) {
					return;
				}
			});
	}

	void startSecondDevice(
			Fn<void(QString, QString)> ready,
			Fn<void(QString)> finished) {
		const auto guard = _alive;
		_rpc->call(
			u"provide_backup"_q,
			{ _accountId },
			[=](QJsonObject result) {
				if (!guard.expired() && finished) {
					finished(HasError(result) ? ErrorText(result) : QString());
				}
			});
		_rpc->call(
			u"get_backup_qr"_q,
			{ _accountId },
			[=](QJsonObject result) {
				if (guard.expired()) {
					return;
				}
				if (HasError(result)) {
					if (ready) {
						ready(QString(), ErrorText(result));
					}
					return;
				}
				const auto qr = result.value(u"result"_q).toString();
				_rpc->call(
					u"create_qr_svg"_q,
					{ qr },
					[=](QJsonObject svg) {
						if (guard.expired() || !ready) {
							return;
						}
						ready(
							HasError(svg)
								? QString()
								: svg.value(u"result"_q).toString(),
							HasError(svg) ? ErrorText(svg) : QString());
					});
			});
	}

	void listTransports(Fn<void(QJsonArray, QString)> done) {
		const auto guard = _alive;
		_rpc->call(
			u"list_transports"_q,
			{ _accountId },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				done(
					HasError(reply) ? QJsonArray() : reply.value(u"result"_q).toArray(),
					HasError(reply) ? ErrorText(reply) : QString());
			});
	}

	void updateTransport(const QJsonObject &transport, Fn<void(QString)> done) {
		const auto guard = _alive;
		_rpc->call(
			u"add_or_update_transport"_q,
			{ _accountId, transport },
			[=](QJsonObject reply) {
				if (!guard.expired() && done) {
					done(HasError(reply) ? ErrorText(reply) : QString());
				}
			});
	}

	void deleteTransport(const QString &addr, Fn<void(QString)> done) {
		const auto guard = _alive;
		_rpc->call(
			u"delete_transport"_q,
			{ _accountId, addr },
			[=](QJsonObject reply) {
				if (!guard.expired() && done) {
					done(HasError(reply) ? ErrorText(reply) : QString());
				}
			});
	}

	void setChatArchived(
			bool self,
			int userId,
			bool archived,
			Fn<void(QString)> done) {
		const auto guard = _alive;
		const auto id = self ? kSelfUserId : userId;
		_rpc->call(
			u"set_chat_visibility"_q,
			{
				_accountId,
				chatForPeerUser(id),
				archived ? u"Archived"_q : u"Normal"_q,
			},
			[=](QJsonObject reply) {
				if (!guard.expired() && done) {
					done(HasError(reply) ? ErrorText(reply) : QString());
				}
			});
	}

	void editMessageText(int messageId, const QString &text, Fn<void(QString)> done) {
		const auto guard = _alive;
		_rpc->call(
			u"send_edit_request"_q,
			{ _accountId, messageId, text },
			[=](QJsonObject reply) {
				if (!guard.expired() && done) {
					done(HasError(reply) ? ErrorText(reply) : QString());
				}
			});
	}

	void addTransportFromQr(const QString &qr, Fn<void(QString)> done) {
		const auto guard = _alive;
		_rpc->call(
			u"add_transport_from_qr"_q,
			{ _accountId, qr },
			[=](QJsonObject reply) {
				if (!guard.expired() && done) {
					done(HasError(reply) ? ErrorText(reply) : QString());
				}
			});
	}

	void stopSecondDevice() {
		_rpc->call(
			u"stop_ongoing_process"_q,
			{ _accountId },
			[](QJsonObject) {});
	}

	void setSelfAvatar(const QString &path, Fn<void(QString)> done) {
		const auto guard = _alive;
		const auto accountId = _accountId;
		const auto epoch = _dialogsEpoch;
		++_selfAvatarRevision;
		_rpc->call(
			u"set_config"_q,
			{ accountId, u"selfavatar"_q, path },
			[=](QJsonObject saved) {
				if (guard.expired()) {
					return;
				}
				if (HasError(saved)) {
					done(ErrorText(saved));
					return;
				}
				_rpc->call(
					u"get_config"_q,
					{ accountId, u"selfavatar"_q },
					[=](QJsonObject reply) {
						if (guard.expired()) {
							return;
						}
						if (HasError(reply)) {
							done(ErrorText(reply));
							return;
						}
						const auto savedPath = reply.value(u"result"_q).toString();
						if (!path.isEmpty() && (savedPath.isEmpty()
							|| Images::Read({ .path = savedPath }).image.isNull())) {
							done(u"The saved profile photo could not be loaded."_q);
							return;
						}
						if (accountId == _accountId && epoch == _dialogsEpoch) {
							_selfAvatar = savedPath;
							showSelfAvatar();
						}
						done(QString());
					});
			});
	}

	void searchMessages(
			int userId,
			const QString &query,
			Fn<void(QVector<MTPMessage>, QVector<MTPUser>)> done) {
		const auto chat = userId
			? QJsonValue(chatForPeerUser(userId))
			: QJsonValue(QJsonValue::Null);
		const auto guard = _alive;
		_rpc->call(
			u"search_messages"_q,
			{ _accountId, query, chat },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				if (HasError(reply)) {
					done({}, {});
					return;
				}
				auto ids = std::vector<int>();
				for (const auto &value : reply.value(u"result"_q).toArray()) {
					ids.push_back(value.toInt());
					if (ids.size() >= 50) {
						break;
					}
				}
				loadSearchHits(userId, ids, std::move(done));
			});
	}

	void loadInviteQr(
			Fn<void(QString, QString, QString, QString)> done) {
		const auto guard = _alive;
		_rpc->call(
			u"get_chat_securejoin_qr_code"_q,
			{ _accountId, QJsonValue::Null },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				if (HasError(reply)) {
					done(_selfName, QString(), QString(), ErrorText(reply));
					return;
				}
				const auto text = reply.value(u"result"_q).toString();
				_rpc->call(
					u"create_qr_svg"_q,
					{ text },
					[=](QJsonObject svg) {
						if (guard.expired()) {
							return;
						}
						if (HasError(svg)) {
							done(_selfName, text, QString(), ErrorText(svg));
							return;
						}
						done(
							_selfName,
							text,
							svg.value(u"result"_q).toString(),
							QString());
					});
			});
	}

	void removeProfile(int accountId, Fn<void(QString)> done) {
		const auto guard = _alive;
		listProfiles([=](QJsonArray accounts) {
			if (guard.expired()) {
				return;
			}
			auto ids = QVector<int>();
			for (const auto &value : accounts) {
				ids.push_back(value.toObject().value(u"id"_q).toInt());
			}
			if (ids.size() < 2) {
				if (done) {
					done(u"Keep at least one profile."_q);
				}
				return;
			}
			auto next = ids.front();
			if (next == accountId) {
				next = ids[1];
			}
			const auto wasCurrent = (accountId == _accountId);
			const auto remove = [=] {
				_ignoreEvents = true;
				_rpc->call(
					u"remove_account"_q,
					{ accountId },
					[=](QJsonObject reply) {
						if (guard.expired()) {
							return;
						}
						_ignoreEvents = false;
						if (done) {
							done(HasError(reply) ? ErrorText(reply) : QString());
						}
					});
			};
			if (!wasCurrent) {
				remove();
				return;
			}
			switchProfile(next, [=](QString error) {
				if (guard.expired()) {
					return;
				}
				if (!error.isEmpty()) {
					if (done) {
						done(error);
					}
					return;
				}
				remove();
			});
		});
	}

	void moveProfileToTop(int accountId, Fn<void(QString)> done) {
		const auto guard = _alive;
		listProfiles([=](QJsonArray accounts) {
			if (guard.expired()) {
				return;
			}
			auto order = QJsonArray();
			order.append(accountId);
			for (const auto &value : accounts) {
				const auto id = value.toObject().value(u"id"_q).toInt();
				if (id != accountId) {
					order.append(id);
				}
			}
			_rpc->call(
				u"set_accounts_order"_q,
				QJsonArray{ QJsonValue(order) },
				[=](QJsonObject reply) {
					if (!guard.expired() && done) {
						done(HasError(reply) ? ErrorText(reply) : QString());
					}
				});
		});
	}

	void listProfiles(Fn<void(QJsonArray)> done) {
		const auto guard = _alive;
		_rpc->call(u"get_all_accounts"_q, {}, [=](QJsonObject reply) {
			if (guard.expired()) {
				return;
			}
			auto accounts = QJsonArray();
			if (!HasError(reply)) {
				for (const auto &value : reply.value(u"result"_q).toArray()) {
					auto account = value.toObject();
					account.insert(u"selected"_q, account.value(u"id"_q).toInt() == _accountId);
					accounts.append(account);
				}
			}
			done(accounts);
		});
	}

	void resetAccountView() {
		++_dialogsEpoch;
		++_selfAvatarRevision;
		_deletedMessages.clear();
		_names.clear();
		_avatars.clear();
		_selfChat = 0;
		_selfAvatar.clear();
		GlobalSelfAvatarColor = QColor();
		GlobalSystemUsers.clear();
		PeerSubtitles.clear();
		ChannelInfos.clear();
		ChannelViews.clear();
		SharedContacts.clear();
		CoreMessages.clear();
		CoreReadReceipts.clear();
		ChannelRevision = ChannelRevision.current() + 1;
		auto users = std::vector<not_null<PeerData*>>();
		_session->data().enumerateUsers([&](not_null<UserData*> user) {
			users.push_back(user);
		});
		for (const auto &user : users) {
			_session->data().deleteConversationLocally(user);
		}
		_session->data().chatsList()->clear();
		_session->data().chatsList()->setLoaded(false);
	}

	[[nodiscard]] MTPUser selfUser() const {
		return MakeUser(kSelfUserId, _selfName, true, _selfAvatar);
	}

	void switchProfile(int accountId, Fn<void(QString)> done) {
		const auto guard = _alive;
		_rpc->call(u"select_account"_q, { accountId }, [=](QJsonObject selected) {
			if (guard.expired()) {
				return;
			}
			if (HasError(selected)) {
				done(ErrorText(selected));
				return;
			}
			_accountId = accountId;
			resetAccountView();
			const auto epoch = _dialogsEpoch;
			_rpc->call(u"start_io"_q, { accountId }, [=](QJsonObject) {});
			struct Self {
				QString name;
				QString avatar;
				QColor color;
				int left = 3;
			};
			const auto self = std::make_shared<Self>();
			const auto finish = [=] {
				if (guard.expired() || accountId != _accountId
					|| epoch != _dialogsEpoch || --self->left) {
					return;
				}
				_selfName = self->name.isEmpty() ? u"Me"_q : self->name;
				_selfAvatar = self->avatar;
				GlobalSelfAvatarColor = self->color;
				showSelfAvatar();
				loadDialogs();
				done(QString());
			};
			_rpc->call(
				u"get_account_info"_q,
				{ accountId },
				[=](QJsonObject reply) {
					if (!guard.expired() && !HasError(reply)) {
						self->color = QColor(reply.value(u"result"_q)
							.toObject().value(u"color"_q).toString());
					}
					finish();
				});
			_rpc->call(
				u"get_config"_q,
				{ accountId, u"displayname"_q },
				[=](QJsonObject reply) {
					if (!guard.expired() && !HasError(reply)) {
						self->name = reply.value(u"result"_q).toString();
					}
					finish();
				});
			_rpc->call(
				u"get_config"_q,
				{ accountId, u"selfavatar"_q },
				[=](QJsonObject reply) {
					if (!guard.expired() && !HasError(reply)) {
						self->avatar = reply.value(u"result"_q).toString();
					}
					finish();
				});
		});
	}

	void addProfile(const QString &name, Fn<void(QString)> done) {
		const auto guard = _alive;
		_rpc->call(u"add_account"_q, {}, [=](QJsonObject created) {
			if (guard.expired()) {
				return;
			}
			if (HasError(created)) {
				done(ErrorText(created));
				return;
			}
			const auto id = created.value(u"result"_q).toInt();
			_rpc->call(
				u"set_config"_q,
				{ id, u"displayname"_q, name },
				[=](QJsonObject named) {
					if (guard.expired()) {
						return;
					}
					if (HasError(named)) {
						done(ErrorText(named));
						return;
					}
					_rpc->call(
						u"init_transports"_q,
						{ id, QJsonValue() },
						[=](QJsonObject ready) {
							if (guard.expired()) {
								return;
							}
							if (HasError(ready)) {
								done(ErrorText(ready));
								return;
							}
							switchProfile(id, std::move(done));
						});
				});
		});
	}

	void createNamedChat(
			bool channel,
			const QString &name,
			Fn<void(int, QString)> done) {
		const auto guard = _alive;
		const auto params = channel
			? QJsonArray{ _accountId, name }
			: QJsonArray{ _accountId, name, false };
		_rpc->call(
			channel ? u"create_broadcast"_q : u"create_group_chat"_q,
			params,
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				if (HasError(reply)) {
					done(0, ErrorText(reply));
					return;
				}
				const auto chatId = reply.value(u"result"_q).toInt();
				_names.insert(chatId, name);
				_session->data().processUsers(MTP_vector<MTPUser>(
					QVector<MTPUser>{ MakeUser(chatId, name, false) }));
				loadDialogs();
				done(chatId, QString());
			});
	}

	void setConfig(
			const QString &key,
			const QString &value,
			Fn<void(QString)> done) {
		const auto guard = _alive;
		const auto accountId = _accountId;
		_rpc->call(
			u"set_config"_q,
			{ _accountId, key, value },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				if (!HasError(reply) && accountId == _accountId
					&& key == u"displayname"_q) {
					_selfName = value;
					_session->data().processUsers(MTP_vector<MTPUser>(
						QVector<MTPUser>{ selfUser() }));
				}
				done(HasError(reply) ? ErrorText(reply) : QString());
			});
	}

	void joinInviteQr(const QString &qr, Fn<void(QString)> done) {
		const auto guard = _alive;
		_rpc->call(
			u"check_qr"_q,
			{ _accountId, qr },
			[=](QJsonObject checked) {
				if (guard.expired()) {
					return;
				}
				if (HasError(checked)) {
					done(ErrorText(checked));
					return;
				}
				_rpc->call(
					u"secure_join"_q,
					{ _accountId, qr },
					[=](QJsonObject joined) {
						if (guard.expired()) {
							return;
						}
						done(HasError(joined) ? ErrorText(joined) : QString());
					});
			});
	}

	void addContact(
			const QString &name,
			const QString &email,
			Fn<void(int, QString)> done) {
		const auto guard = _alive;
		_rpc->call(
			u"create_contact"_q,
			{ _accountId, email, name.isEmpty() ? QJsonValue::Null : QJsonValue(name) },
			[=](QJsonObject contact) {
				if (guard.expired()) {
					return;
				}
				if (HasError(contact)) {
					done(0, ErrorText(contact));
					return;
				}
				const auto contactId = contact.value(u"result"_q).toInt();
				_rpc->call(
					u"create_chat_by_contact_id"_q,
					{ _accountId, contactId },
					[=](QJsonObject chat) {
						if (guard.expired()) {
							return;
						}
						if (HasError(chat)) {
							done(0, ErrorText(chat));
							return;
						}
						const auto chatId = chat.value(u"result"_q).toInt();
						const auto shown = name.isEmpty() ? email : name;
						_names.insert(chatId, shown);
						_session->data().processUsers(MTP_vector<MTPUser>(
							QVector<MTPUser>{ MakeUser(chatId, shown, false) }));
						loadDialogs();
						done(chatId, QString());
					});
			});
	}

	void listContacts(Fn<void(std::vector<ContactRow>, QString)> done, bool addressOnly) {
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		_rpc->call(
			u"get_contacts"_q,
			{ _accountId, addressOnly ? 0x04 : 0x02, QJsonValue() },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				if (epoch != _dialogsEpoch) {
					done({}, u"Account changed."_q);
					return;
				}
				if (HasError(reply)) {
					done({}, ErrorText(reply));
					return;
				}
				auto rows = std::vector<ContactRow>();
				for (const auto &value : reply.value(u"result"_q).toArray()) {
					const auto contact = value.toObject();
					auto row = ContactRow();
					row.id = contact.value(u"id"_q).toInt();
					row.address = contact.value(u"address"_q).toString();
					row.avatar = contact.value(u"profileImage"_q).toString();
					row.lastSeen = contact.value(u"lastSeen"_q).toVariant().toLongLong();
					row.name = contact.value(u"displayName"_q).toString();
					if (row.name.isEmpty()) {
						row.name = contact.value(u"name"_q).toString();
					}
					if (row.name.isEmpty()) {
						row.name = contact.value(u"address"_q).toString();
					}
					if (row.id > 0 && !row.name.isEmpty()) {
						rows.push_back(std::move(row));
					}
				}
				done(std::move(rows), QString());
			});
	}

	void openContact(const ContactRow &contact, Fn<void(int, QString)> done) {
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		_rpc->call(u"create_chat_by_contact_id"_q, { _accountId, contact.id },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				if (epoch != _dialogsEpoch || HasError(reply)) {
					done(0, HasError(reply) ? ErrorText(reply) : u"Account changed."_q);
					return;
				}
				const auto chatId = reply.value(u"result"_q).toInt();
				_names.insert(chatId, contact.name);
				_session->data().processUsers(MTP_vector<MTPUser>(
					QVector<MTPUser>{ MakeUser(chatId, contact.name, false) }));
				loadDialogs();
				done(chatId, QString());
			});
	}

	void shareContact(int contactId, int userId, Fn<void(QString)> done) {
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		_rpc->call(u"make_vcard"_q, { _accountId, QJsonArray{ contactId } },
			[=](QJsonObject reply) {
				if (guard.expired()) return;
				if (epoch != _dialogsEpoch || HasError(reply)) {
					done(HasError(reply) ? ErrorText(reply) : u"Account changed."_q);
					return;
				}
				const auto file = std::make_shared<QTemporaryFile>(QDir::tempPath() + u"/delta-contact-XXXXXX.vcf"_q);
				const auto card = reply.value(u"result"_q).toString().toUtf8();
				if (card.isEmpty() || !file->open() || file->write(card) != card.size() || !file->flush()) {
					done(u"Could not prepare the contact card."_q);
					return;
				}
				sendFile(userId, file->fileName(), QString(), u"Vcard"_q, 0,
					[=](int id, QString error) {
						if (guard.expired()) return;
						if (epoch != _dialogsEpoch) { done(u"Account changed."_q); return; }
						if (error.isEmpty() && id > 0) {
							addIncoming(chatForPeerUser(userId), id);
							loadDialogs();
						}
						// Keep the file alive until Core has copied it into its blob store.
						(void)file;
						done(error);
					});
			});
	}

	void accountRequest(const QString &method, const QJsonArray &args,
			Fn<void(QJsonValue, QString)> done) {
		auto params = QJsonArray{ _accountId };
		for (const auto &arg : args) params.append(arg);
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		_rpc->call(method, params, [=](QJsonObject reply) {
			if (guard.expired()) return;
			if (epoch != _dialogsEpoch || HasError(reply)) {
				done({}, HasError(reply) ? ErrorText(reply) : u"Account changed."_q);
				return;
			}
			done(reply.value(u"result"_q), QString());
		});
	}

	void joinInviteChat(const QString &qr, Fn<void(int, QString)> done) {
		const auto guard = _alive;
		accountRequest(u"secure_join"_q, { qr }, [=](QJsonValue result, QString error) {
			if (guard.expired()) return;
			const auto chatId = result.toInt();
			if (!error.isEmpty() || chatId <= 0) {
				done(0, error.isEmpty() ? u"Could not join this invitation."_q : error);
				return;
			}
			channelRequest(chatId, u"get_full_chat_by_id"_q, {}, [=](QJsonValue value, QString failure) {
				if (guard.expired()) return;
				if (!failure.isEmpty()) { done(0, failure); return; }
				const auto name = value.toObject().value(u"name"_q).toString();
				_names.insert(chatId, name);
				_session->data().user(UserId(chatId))->setName(name, QString(), QString(), QString());
				loadDialogs();
				done(chatId, QString());
			});
		});
	}

	void channelRequest(int userId, const QString &method, const QJsonArray &args,
			Fn<void(QJsonValue, QString)> done) {
		auto params = QJsonArray{ _accountId, chatForPeerUser(userId) };
		for (const auto &arg : args) params.append(arg);
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		_rpc->call(method, params, [=](QJsonObject reply) {
			if (guard.expired()) return;
			if (epoch != _dialogsEpoch || HasError(reply)) {
				done({}, HasError(reply) ? ErrorText(reply) : u"Account changed."_q);
				return;
			}
			const auto result = reply.value(u"result"_q);
			if (method == u"get_full_chat_by_id"_q) {
				ChannelInfos.insert(userId, result.toObject());
				ChannelRevision = ChannelRevision.current() + 1;
				_session->changes().peerUpdated(_session->data().user(UserId(userId)), Data::PeerUpdate::Flag::OnlineStatus);
			} else if (!method.startsWith(u"get_"_q)) {
				_reload.start();
			}
			done(result, QString());
		});
	}

	void sendChannelInvite(int channelUserId, int recipientUserId, Fn<void(QString)> done) {
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		channelRequest(channelUserId, u"get_chat_securejoin_qr_code"_q, {}, [=](QJsonValue value, QString error) {
			if (guard.expired()) return;
			if (!error.isEmpty()) { done(error); return; }
			const auto text = u"Join %1\n%2"_q.arg(_names.value(chatForPeerUser(channelUserId)), value.toString());
			sendText(recipientUserId, text, 0, [=](int id, QString failure) {
				if (guard.expired()) return;
				if (epoch != _dialogsEpoch) { done(u"Account changed."_q); return; }
				if (failure.isEmpty() && id > 0) {
					addIncoming(chatForPeerUser(recipientUserId), id);
					_reload.start();
				}
				done(failure);
			});
		});
	}

	void loadChannelInvite(int userId, Fn<void(QString, QString, QString)> done) {
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		channelRequest(userId, u"get_chat_securejoin_qr_code"_q, {}, [=](QJsonValue value, QString error) {
			if (guard.expired()) return;
			if (!error.isEmpty()) { done({}, {}, error); return; }
			const auto text = value.toString();
			_rpc->call(u"create_qr_svg"_q, { text }, [=](QJsonObject reply) {
				if (guard.expired()) return;
				if (epoch != _dialogsEpoch || HasError(reply)) {
					done({}, {}, HasError(reply) ? ErrorText(reply) : u"Account changed."_q);
					return;
				}
				done(text, reply.value(u"result"_q).toString(), QString());
			});
		});
	}

	void createGroup(
			const QString &name,
			const QString &description,
			const std::vector<int> &contactIds,
			const QString &imagePath,
			Fn<void(int, QString)> done,
			bool channel = false) {
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		_rpc->call(
			channel ? u"create_broadcast"_q : u"create_group_chat"_q,
			channel ? QJsonArray{ _accountId, name } : QJsonArray{ _accountId, name, false },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				if (HasError(reply)) {
					done(0, ErrorText(reply));
					return;
				}
				if (epoch != _dialogsEpoch) { done(0, u"Account changed."_q); return; }
				const auto chatId = reply.value(u"result"_q).toInt();
				if (channel) {
					ChannelInfos.insert(chatId, QJsonObject{
						{ u"chatType"_q, u"OutBroadcast"_q },
						{ u"name"_q, name },
						{ u"contactIds"_q, QJsonArray() },
					});
					ChannelRevision = ChannelRevision.current() + 1;
				}
				struct Step {
					std::vector<QString> methods;
					std::vector<QJsonArray> params;
					int index = 0;
				};
				const auto steps = std::make_shared<Step>();
				if (!description.isEmpty()) {
					steps->methods.push_back(u"set_chat_description"_q);
					steps->params.push_back({
						_accountId,
						chatId,
						description,
					});
				}
				if (!imagePath.isEmpty()) {
					steps->methods.push_back(u"set_chat_profile_image"_q);
					steps->params.push_back({
						_accountId,
						chatId,
						imagePath,
					});
				}
				for (const auto contactId : contactIds) {
					if (contactId <= 1) {
						continue;
					}
					steps->methods.push_back(u"add_contact_to_chat"_q);
					steps->params.push_back({
						_accountId,
						chatId,
						contactId,
					});
				}
				const auto next = std::make_shared<Fn<void()>>();
				const auto weakNext = std::weak_ptr<Fn<void()>>(next);
				*next = [=] {
					if (guard.expired() || epoch != _dialogsEpoch) {
						return;
					}
					if (steps->index >= int(steps->methods.size())) {
						_names.insert(chatId, name);
						_session->data().processUsers(MTP_vector<MTPUser>(
							QVector<MTPUser>{ MakeUser(chatId, name, false) }));
						loadDialogs();
						done(chatId, QString());
						return;
					}
					const auto index = steps->index++;
					const auto continuation = weakNext.lock();
					_rpc->call(
						steps->methods[index],
						steps->params[index],
						[=](QJsonObject one) {
							if (guard.expired() || epoch != _dialogsEpoch) {
								return;
							}
							if (HasError(one)) {
								if (channel) loadDialogs();
								done(channel ? chatId : 0, ErrorText(one));
								return;
							}
							if (continuation) (*continuation)();
						});
				};
				(*next)();
			});
	}

private:
	[[nodiscard]] int peerUserForChat(int chatId) const {
		return (chatId == _selfChat) ? kSelfUserId : chatId;
	}
	[[nodiscard]] int chatForPeerUser(int userId) const {
		return (userId == kSelfUserId) ? _selfChat : userId;
	}

	void reply(
			not_null<MTP::Instance*> instance,
			mtpRequestId requestId,
			Fn<void(mtpBuffer&)> write) {
		const auto weak = base::make_weak(_session);
		crl::on_main([=] {
			const auto session = weak.get();
			if (!session || &session->mtp() != instance.get()) {
				return;
			}
			if (!instance->hasCallback(requestId)) {
				return;
			}
			auto response = MTP::Response();
			response.requestId = requestId;
			write(response.reply);
			instance->processCallback(response);
		});
	}

	void fail(
			not_null<MTP::Instance*> instance,
			mtpRequestId requestId,
			const QString &type) {
		reply(instance, requestId, [=](mtpBuffer &to) {
			MTPRpcError(MTP_rpc_error(
				MTP_int(400),
				MTP_string(type)
			)).write(to);
		});
	}

	void answerFile(
			not_null<MTP::Instance*> instance,
			mtpRequestId requestId,
			WordReader &reader) {
		reader.u32(); // flags
		const auto location = reader.u32();
		auto photoId = qint64(0);
		auto path = QString();
		if (location == mtpc_inputPhotoFileLocation
			|| location == mtpc_inputDocumentFileLocation) {
			photoId = reader.i64();
			reader.i64(); // access_hash
			reader.string(); // file_reference
			reader.string(); // thumb_size
			path = PhotoFiles.value(photoId);
			if (path.isEmpty()) {
				path = DocumentFiles.value(photoId);
			}
		} else if (location == mtpc_inputPeerPhotoFileLocation) {
			reader.u32(); // location flags
			ReadInputPeerUser(reader);
			photoId = reader.i64();
			path = AvatarFiles.value(photoId);
		} else {
			return fail(instance, requestId, u"DELTA_FILE_UNSUPPORTED"_q);
		}
		const auto offset = reader.i64();
		const auto limit = reader.i32();
		if (!reader.ok() || path.isEmpty()) {
			return fail(instance, requestId, u"DELTA_FILE_MISSING"_q);
		}
		const auto peerPhoto = (location == mtpc_inputPeerPhotoFileLocation);
		auto bytes = QByteArray();
		auto png = false;
		if (peerPhoto) {
			auto encoded = AvatarPng.value(photoId);
			if (encoded.isEmpty()) {
				const auto image = ::Images::Read({
					.path = path,
					.maxSize = QSize(256, 256),
				}).image;
				if (!image.isNull()) {
					auto buffer = QBuffer(&encoded);
					buffer.open(QIODevice::WriteOnly);
					image.save(&buffer, "PNG");
					if (!encoded.isEmpty()) {
						AvatarPng.insert(photoId, encoded);
					}
				}
			}
			if (!encoded.isEmpty()) {
				png = true;
				if (offset < encoded.size()) {
					const auto chunk = (limit > 0) ? int(limit) : encoded.size();
					bytes = encoded.mid(int(offset), chunk);
				}
			}
		}
		if (!png) {
			auto file = QFile(path);
			if (!file.open(QIODevice::ReadOnly) || !file.seek(offset)) {
				return fail(instance, requestId, u"DELTA_FILE_MISSING"_q);
			}
			const auto chunk = (limit > 0) ? qint64(limit) : qint64(1 << 20);
			bytes = file.read(chunk);
		}
		const auto lower = path.toLower();
		const auto audio = lower.endsWith(u".ogg"_q)
			|| lower.endsWith(u".oga"_q)
			|| lower.endsWith(u".opus"_q)
			|| lower.endsWith(u".m4a"_q)
			|| lower.endsWith(u".mp3"_q);
		const auto type = png
			? MTPstorage_FileType(MTP_storage_filePng())
			: lower.endsWith(u".png"_q)
			? MTPstorage_FileType(MTP_storage_filePng())
			: lower.endsWith(u".gif"_q)
			? MTPstorage_FileType(MTP_storage_fileGif())
			: lower.endsWith(u".webp"_q)
			? MTPstorage_FileType(MTP_storage_fileWebp())
			: audio
			? MTPstorage_FileType(MTP_storage_fileMp3())
			: MTPstorage_FileType(MTP_storage_fileJpeg());
		const auto boxed = MTPupload_File(MTP_upload_file(
			type,
			MTP_int(int(QFileInfo(path).lastModified().toSecsSinceEpoch())),
			MTP_bytes(bytes)));
		this->reply(instance, requestId, [=](mtpBuffer &to) {
			boxed.write(to);
		});
	}

	void loadSearchHits(
			int fallbackUser,
			const std::vector<int> &ids,
			Fn<void(QVector<MTPMessage>, QVector<MTPUser>)> done) {
		struct State {
			QVector<MTPMessage> messages;
			QSet<int> users;
			int remaining = 0;
		};
		const auto state = std::make_shared<State>();
		state->remaining = int(ids.size());
		state->messages.resize(int(ids.size()));
		if (ids.empty()) {
			done({}, {});
			return;
		}
		const auto guard = _alive;
		for (auto i = 0; i != int(ids.size()); ++i) {
				fetchMessage(ids[i],
				[=](QJsonObject one) {
					if (guard.expired()) {
						return;
					}
					if (!HasError(one)) {
						const auto data = one.value(u"result"_q).toObject();
						auto peerUser = fallbackUser;
						if (!peerUser) {
							peerUser = peerUserForChat(
								data.value(u"chatId"_q).toInt());
						}
						state->messages[i] = MessageFromJson(peerUser, data);
						state->users.insert(peerUser);
					}
					if (--state->remaining == 0) {
						auto users = QVector<MTPUser>();
						users.push_back(selfUser());
						for (const auto userId : state->users) {
							if (userId && userId != kSelfUserId) {
								const auto chatId = chatForPeerUser(userId);
								users.push_back(MakeUser(
									userId,
									_names.value(chatId),
									false,
									_avatars.value(chatId)));
							}
						}
						auto messages = QVector<MTPMessage>();
						for (const auto &message : state->messages) {
							if (message.type() == mtpc_message) {
								messages.push_back(message);
							}
						}
						done(std::move(messages), std::move(users));
					}
				});
		}
	}

	void answerSearch(
			not_null<MTP::Instance*> instance,
			mtpRequestId requestId,
			WordReader &reader,
			bool global) {
		const auto flags = reader.u32();
		auto userId = 0;
		auto query = QString();
		if (global) {
			if (flags & 1) {
				reader.i32();
			}
			if (flags & 16) {
				const auto channel = reader.u32();
				if (channel != mtpc_inputChannelEmpty) {
					reader.i64();
					reader.i64();
				}
			}
			query = reader.string();
		} else {
			userId = ReadInputPeerUser(reader);
			query = reader.string();
		}
		if (!reader.ok()) {
			return fail(instance, requestId, u"DELTA_BAD_REQUEST"_q);
		}
		searchMessages(userId, query, [=](
				QVector<MTPMessage> messages,
				QVector<MTPUser> users) {
			const auto boxed = MTPmessages_Messages(MTP_messages_messages(
				MTP_vector<MTPMessage>(messages),
				MTP_vector<MTPForumTopic>(),
				MTP_vector<MTPChat>(),
				MTP_vector<MTPUser>(users)));
			this->reply(instance, requestId, [=](mtpBuffer &to) {
				boxed.write(to);
			});
		});
	}

	void answerContacts(
			not_null<MTP::Instance*> instance,
			mtpRequestId requestId,
			WordReader &reader) {
		reader.u32();
		const auto query = reader.string().trimmed();
		if (!reader.ok()) {
			return fail(instance, requestId, u"DELTA_BAD_REQUEST"_q);
		}
		auto users = QVector<MTPUser>();
		auto peers = QVector<MTPPeer>();
		users.push_back(selfUser());
		if (query.isEmpty()
			|| _selfName.contains(query, Qt::CaseInsensitive)
			|| GlobalSelfAddress.contains(query, Qt::CaseInsensitive)) {
			peers.push_back(MTP_peerUser(MTP_long(kSelfUserId)));
		}
		for (auto i = _names.constBegin(); i != _names.constEnd(); ++i) {
			const auto userId = peerUserForChat(i.key());
			if (userId == kSelfUserId) {
				continue;
			}
			if (i.value().contains(query, Qt::CaseInsensitive)) {
				users.push_back(MakeUser(
					userId,
					i.value(),
					false,
					_avatars.value(i.key())));
				peers.push_back(MTP_peerUser(MTP_long(userId)));
			}
		}
		const auto boxed = MTPcontacts_Found(MTP_contacts_found(
			MTP_vector<MTPPeer>(peers),
			MTP_vector<MTPPeer>(),
			MTP_vector<MTPChat>(),
			MTP_vector<MTPUser>(users)));
		this->reply(instance, requestId, [=](mtpBuffer &to) {
			boxed.write(to);
		});
	}

	void answerHistory(
			not_null<MTP::Instance*> instance,
			mtpRequestId requestId,
			WordReader &reader) {
		const auto userId = ReadInputPeerUser(reader);
		const auto offsetId = reader.i32();
		reader.i32(); // offset_date
		const auto addOffset = reader.i32();
		const auto limit = std::max(reader.i32(), 1);
		if (!reader.ok() || !userId) {
			return fail(instance, requestId, u"DELTA_BAD_REQUEST"_q);
		}
		const auto chatId = chatForPeerUser(userId);
		const auto guard = _alive;
		_rpc->call(
			u"get_message_ids"_q,
			{ _accountId, chatId, false, false },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				if (HasError(reply)) {
					return fail(instance, requestId, u"DELTA_RPC"_q);
				}
				auto ids = std::vector<int>();
				for (const auto &value : reply.value(u"result"_q).toArray()) {
					ids.push_back(value.toInt());
				}
				std::sort(ids.begin(), ids.end(), std::greater<int>());
				auto start = 0;
				if (offsetId > 0) {
					start = int(std::find_if(
						ids.begin(),
						ids.end(),
						[&](int id) { return id < offsetId; }) - ids.begin());
				}
				start = std::clamp(start + addOffset, 0, int(ids.size()));
				const auto end = std::min(start + limit, int(ids.size()));
				struct State {
					std::vector<MTPMessage> messages;
					int remaining = 0;
				};
				const auto state = std::make_shared<State>();
				const auto count = end - start;
				state->remaining = count;
				state->messages.resize(count);
				const auto finish = [=] {
					auto messages = QVector<MTPMessage>();
					for (auto i = 0; i != count; ++i) {
						if (!_deletedMessages.contains(ids[start + i])) {
							messages.push_back(state->messages[i]);
						}
					}
					auto users = QVector<MTPUser>();
					users.push_back(selfUser());
					if (userId != kSelfUserId) {
						users.push_back(MakeUser(
							userId,
							_names.value(chatId),
							false,
							_avatars.value(chatId)));
					}
					const auto boxed = MTPmessages_Messages(MTP_messages_messages(
						MTP_vector<MTPMessage>(messages),
						MTP_vector<MTPForumTopic>(),
						MTP_vector<MTPChat>(),
						MTP_vector<MTPUser>(users)));
					this->reply(instance, requestId, [=](mtpBuffer &to) {
						boxed.write(to);
					});
				};
				if (!count) {
					return finish();
				}
				for (auto i = 0; i != count; ++i) {
					fetchMessage(
						ids[start + i],
						[=](QJsonObject one) {
							if (guard.expired()) {
								return;
							}
							state->messages[i] = HasError(one)
								? MakeMessage(ids[start + i], userId, false, QString(), 0)
								: MessageFromJson(
									userId,
									one.value(u"result"_q).toObject());
							if (--state->remaining == 0) {
								finish();
							}
						});
				}
			});
	}

	void answerSend(
			not_null<MTP::Instance*> instance,
			mtpRequestId requestId,
			WordReader &reader) {
		const auto flags = reader.u32();
		const auto userId = ReadInputPeerUser(reader);
		auto quotedId = 0;
		if (flags & 1) {
			const auto replyType = reader.u32();
			if (replyType == mtpc_inputReplyToMessage) {
				const auto replyFlags = reader.u32();
				quotedId = reader.i32();
				if (replyFlags & 1) {
					reader.i32();
				}
				if (replyFlags & 2) {
					ReadInputPeerUser(reader);
				}
				if (replyFlags & 4) {
					reader.string();
				}
				if (replyFlags & 8) {
					return fail(instance, requestId, u"DELTA_REPLY_UNSUPPORTED"_q);
				}
				if (replyFlags & 16) {
					reader.i32();
				}
			} else if (replyType == mtpc_inputReplyToEphemeralMessage) {
				quotedId = reader.i32();
			} else if (replyType == mtpc_inputReplyToStory) {
				ReadInputPeerUser(reader);
				reader.i32();
			} else if (replyType == mtpc_inputReplyToMonoForum) {
				ReadInputPeerUser(reader);
			}
		}
		const auto text = reader.string();
		if (!reader.ok() || !userId) {
			return fail(instance, requestId, u"DELTA_BAD_REQUEST"_q);
		}
		QJsonObject data;
		data.insert(u"text"_q, text);
		if (quotedId > 0) {
			data.insert(u"quotedMessageId"_q, quotedId);
		}
		const auto guard = _alive;
		_rpc->call(
			u"send_msg"_q,
			{ _accountId, chatForPeerUser(userId), data },
			[=](QJsonObject result) {
				if (guard.expired()) {
					return;
				}
				if (HasError(result)) {
					return fail(instance, requestId, u"DELTA_SEND_FAILED"_q);
				}
				const auto id = result.value(u"result"_q).toInt();
				const auto date = int(QDateTime::currentSecsSinceEpoch());
				const auto boxed = MTPUpdates(MTP_updateShortSentMessage(
					MTP_flags(MTPDupdateShortSentMessage::Flag::f_out),
					MTP_int(id),
					MTP_int(0),
					MTP_int(0),
					MTP_int(date),
					MTPMessageMedia(),
					MTPVector<MTPMessageEntity>(),
					MTPint()));
				this->reply(instance, requestId, [=](mtpBuffer &to) {
					boxed.write(to);
				});
			});
	}

	void answerForward(
			not_null<MTP::Instance*> instance,
			mtpRequestId requestId,
			WordReader &reader) {
		reader.u32();
		const auto fromUser = ReadInputPeerUser(reader);
		auto count = reader.i32();
		if (uint32(count) == mtpc_vector) {
			count = reader.i32();
		}
		if (count < 1 || count > 100) {
			return fail(instance, requestId, u"DELTA_BAD_REQUEST"_q);
		}
		auto ids = QJsonArray();
		for (auto i = 0; i < count; ++i) {
			const auto id = reader.i32();
			if (!IsClientMsgId(MsgId(id))) {
				ids.append(id);
			}
		}
		auto randomCount = reader.i32();
		if (uint32(randomCount) == mtpc_vector) {
			randomCount = reader.i32();
		}
		if (randomCount < 0 || randomCount > 100) {
			return fail(instance, requestId, u"DELTA_BAD_REQUEST"_q);
		}
		auto randomIds = QVector<uint64>();
		randomIds.reserve(randomCount);
		for (auto i = 0; i < randomCount; ++i) {
			randomIds.push_back(uint64(reader.i64()));
		}
		const auto toUser = ReadInputPeerUser(reader);
		if (!reader.ok() || !fromUser || !toUser || ids.isEmpty()) {
			return fail(instance, requestId, u"DELTA_BAD_REQUEST"_q);
		}
		const auto guard = _alive;
		const auto chatId = chatForPeerUser(toUser);
		_rpc->call(
			u"forward_messages"_q,
			{ _accountId, ids, chatId },
			[=](QJsonObject result) {
				if (guard.expired()) {
					return;
				}
				if (HasError(result)) {
					return fail(instance, requestId, ErrorText(result));
				}
				_rpc->call(
					u"get_message_ids"_q,
					{ _accountId, chatId, false, false },
					[=](QJsonObject reply) {
						if (guard.expired()) {
							return;
						}
						auto fresh = std::vector<int>();
						for (const auto &value : reply.value(u"result"_q).toArray()) {
							fresh.push_back(value.toInt());
						}
						std::sort(fresh.begin(), fresh.end(), std::greater<int>());
						if (int(fresh.size()) > ids.size()) {
							fresh.resize(ids.size());
						}
						auto messages = std::make_shared<QVector<MTPMessage>>();
						auto left = std::make_shared<int>(int(fresh.size()));
						const auto finish = [=] {
							for (const auto randomId : randomIds) {
								const auto localId = _session->data().messageIdByRandomId(randomId);
								if (const auto local = localId
										? _session->data().message(localId)
										: nullptr) {
									local->destroy();
								}
								_session->data().unregisterMessageRandomId(randomId);
							}
							if (!messages->isEmpty()) {
								_session->data().processMessages(
									*messages,
									NewMessageType::Unread);
							}
							const auto date = int(QDateTime::currentSecsSinceEpoch());
							const auto boxed = MTPUpdates(MTP_updates(
								MTP_vector<MTPUpdate>(),
								MTP_vector<MTPUser>(),
								MTP_vector<MTPChat>(),
								MTP_int(date),
								MTP_int(0)));
							this->reply(instance, requestId, [=](mtpBuffer &to) {
								boxed.write(to);
							});
						};
						if (fresh.empty()) {
							return finish();
						}
						for (const auto id : fresh) {
							fetchMessage(id, [=](QJsonObject one) {
								if (!guard.expired()) {
									if (!HasError(one)) {
										messages->push_back(MessageFromJson(
											toUser,
											one.value(u"result"_q).toObject()));
									}
									if (--(*left) == 0) {
										finish();
									}
								}
							});
						}
					});
			});
	}

	void answerRead(
			not_null<MTP::Instance*> instance,
			mtpRequestId requestId,
			WordReader &reader) {
		const auto userId = ReadInputPeerUser(reader);
		const auto maxId = reader.i32();
		if (!reader.ok() || !userId) {
			return fail(instance, requestId, u"DELTA_BAD_REQUEST"_q);
		}
		const auto chatId = chatForPeerUser(userId);
		const auto guard = _alive;
		_rpc->call(
			u"get_message_ids"_q,
			{ _accountId, chatId, false, false },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				auto seen = QJsonArray();
				if (!HasError(reply)) {
					for (const auto &value : reply.value(u"result"_q).toArray()) {
						if (maxId <= 0 || value.toInt() <= maxId) {
							seen.append(value);
						}
					}
				}
				_rpc->call(u"markseen_msgs"_q, { _accountId, seen }, [=](QJsonObject) {
					_rpc->call(
						u"marknoticed_chat"_q,
						{ _accountId, chatId },
						[=](QJsonObject) {});
				});
				const auto boxed = MTPmessages_AffectedMessages(
					MTP_messages_affectedMessages(MTP_int(0), MTP_int(0)));
				this->reply(instance, requestId, [=](mtpBuffer &to) {
					boxed.write(to);
				});
			});
	}

	void pump() {
		if (_pumping) {
			return;
		}
		_pumping = true;
		const auto guard = _alive;
		_rpc->call(u"get_next_event"_q, {}, [=](QJsonObject reply) {
			if (guard.expired()) {
				return;
			}
			_pumping = false;
			if (HasError(reply)) {
				QTimer::singleShot(1000, [=] {
					if (!guard.expired()) {
						pump();
					}
				});
				return;
			}
			handleEvent(reply.value(u"result"_q).toObject());
			pump();
		});
	}

	void handleEvent(const QJsonObject &envelope) {
		const auto event = envelope.value(u"event"_q).toObject();
		const auto kind = event.value(u"kind"_q).toString();
		if (_ignoreEvents) {
			return;
		}
		const auto context = envelope.value(u"contextId"_q);
		if (!context.isUndefined()
			&& !context.isNull()
			&& context.toInt() != _accountId) {
			return;
		}
		if (kind == u"ConnectivityChanged"_q) {
			refreshConnectivity();
		}
		if (kind == u"SelfavatarChanged"_q
			|| (kind == u"ConfigSynced"_q
				&& event.value(u"key"_q).toString() == u"selfavatar"_q)) {
			loadSelfAvatar();
		}
		if (kind == u"MsgDeleted"_q) {
			const auto chatId = event.value(u"chatId"_q).toInt();
			const auto msgId = event.value(u"msgId"_q).toInt();
			if (chatId > 0 && msgId > 0) {
				_deletedMessages.insert(msgId);
				const auto peerId = peerFromUser(UserId(peerUserForChat(chatId)));
				if (const auto item = _session->data().message(peerId, MsgId(msgId))) {
					item->destroy();
				}
			}
			_reload.start();
		}
		if (kind == u"IncomingMsg"_q
			|| kind == u"MsgsChanged"_q
			|| kind == u"ReactionsChanged"_q
			|| kind == u"IncomingReaction"_q
			|| kind == u"MsgRead"_q
			|| kind == u"MsgReadCountChanged"_q
			|| kind == u"MsgDelivered"_q
			|| kind == u"MsgFailed"_q) {
			const auto chatId = event.value(u"chatId"_q).toInt();
			const auto msgId = event.value(u"msgId"_q).toInt();
			if (chatId > 0 && msgId > 0) {
				addIncoming(chatId, msgId);
			}
		}
		if (kind == u"IncomingMsg"_q
			|| kind == u"MsgsChanged"_q
			|| kind == u"MsgDelivered"_q
			|| kind == u"MsgFailed"_q
			|| kind == u"MsgRead"_q
			|| kind == u"MsgReadCountChanged"_q
			|| kind == u"MsgsNoticed"_q
			|| kind == u"ChatModified"_q
			|| kind == u"ChatlistChanged"_q
			|| kind == u"ChatlistItemChanged"_q
			|| kind == u"ContactsChanged"_q) {
			_reload.start();
		}
	}

	void addIncoming(int chatId, int msgId) {
		const auto userId = peerUserForChat(chatId);
		const auto peerId = peerFromUser(UserId(userId));
		const auto guard = _alive;
		fetchMessage(
			msgId,
			[=](QJsonObject one) {
				if (guard.expired() || HasError(one)) {
					return;
				}
				const auto data = one.value(u"result"_q).toObject();
				const auto built = MessageFromJson(userId, data);
				const auto existing = _session->data().message(peerId, MsgId(msgId));
				if (existing && built.type() == mtpc_message) {
					existing->updateReactions(built.c_message().vreactions());
				}
				auto hasPhoto = false;
				if (built.type() == mtpc_message) {
					const auto media = built.c_message().vmedia();
					hasPhoto = media
						&& (media->type() == mtpc_messageMediaPhoto
							|| media->type() == mtpc_messageMediaDocument);
				}
				if (existing) {
					ApplyMessageData(existing);
				}
				if (existing && !hasPhoto) {
					return;
				}
				_session->data().processUsers(MTP_vector<MTPUser>(
					QVector<MTPUser>{ MakeUser(
						userId,
						userId == kSelfUserId
							? _selfName
							: _names.value(chatId),
						userId == kSelfUserId,
						_avatars.value(chatId)) }));
				_session->data().processMessages(
					QVector<MTPMessage>{ built },
					::NewMessageType::Unread);
			});
	}

	void loadDialogs() {
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		_rpc->call(
			u"get_chatlist_entries"_q,
			{ _accountId, QJsonValue::Null, QJsonValue::Null, QJsonValue::Null },
			[=](QJsonObject reply) {
				if (guard.expired()
					|| epoch != _dialogsEpoch
					|| HasError(reply)) {
					return;
				}
				const auto ids = reply.value(u"result"_q).toArray();
				_rpc->call(
					u"get_chatlist_items_by_entries"_q,
					{ _accountId, ids },
					[=](QJsonObject items) {
						if (guard.expired()
							|| epoch != _dialogsEpoch
							|| HasError(items)) {
							return;
						}
						applyItems(ids, items.value(u"result"_q).toObject());
					});
			});
	}

	struct Entry {
		int chatId = 0;
		QString name;
		int lastMessageId = 0;
		int fresh = 0;
		bool pinned = false;
		bool muted = false;
		bool selfTalk = false;
		QString avatar;
		int contactId = 0;
		bool channel = false;
	};

	void applyItems(const QJsonArray &ids, const QJsonObject &map) {
		const auto epoch = _dialogsEpoch;
		struct State {
			std::vector<Entry> entries;
			std::vector<MTPMessage> messages;
			int remaining = 0;
		};
		const auto state = std::make_shared<State>();
		for (const auto &idValue : ids) {
			const auto item = map.value(
				QString::number(idValue.toInt())).toObject();
			if (item.value(u"kind"_q).toString() != u"ChatListItem"_q) {
				continue;
			}
			auto entry = Entry();
			entry.chatId = item.value(u"id"_q).toInt();
			entry.name = item.value(u"name"_q).toString();
			entry.lastMessageId = item.value(u"lastMessageId"_q).toInt();
			entry.fresh = item.value(u"freshMessageCounter"_q).toInt();
			entry.pinned = item.value(u"isPinned"_q).toBool();
			entry.muted = item.value(u"isMuted"_q).toBool();
			entry.selfTalk = item.value(u"isSelfTalk"_q).toBool();
			entry.avatar = item.value(u"avatarPath"_q).toString();
			entry.contactId = item.value(u"dmChatContact"_q).toInt();
			const auto type = item.value(u"chatType"_q).toString();
			entry.channel = (type == u"OutBroadcast"_q || type == u"InBroadcast"_q);
			if (entry.channel) {
				auto &info = ChannelInfos[entry.chatId];
				info.insert(u"chatType"_q, type);
				info.insert(u"name"_q, entry.name);
			}
			if (entry.selfTalk) {
				_selfChat = entry.chatId;
			}
			if (item.value(u"isDeviceTalk"_q).toBool()) {
				GlobalSystemUsers.insert(entry.chatId);
			}
			_names.insert(entry.chatId, entry.name);
			_avatars.insert(entry.chatId, entry.avatar);
			state->entries.push_back(std::move(entry));
		}
		_session->data().contactsLoaded() = true;
		auto contactFetches = 0;
		for (const auto &entry : state->entries) {
			if (entry.contactId > 1) {
				++contactFetches;
			}
		}
		const auto channelFetches = int(std::count_if(state->entries.begin(), state->entries.end(),
			[](const Entry &entry) { return entry.channel; }));
		ChannelRevision = ChannelRevision.current() + 1;
		state->remaining = int(state->entries.size()) + contactFetches + channelFetches;
		if (!state->remaining) {
			_session->data().chatsList()->setLoaded();
			return;
		}
		const auto guard = _alive;
		for (const auto &entry : state->entries) {
			if (entry.channel) {
				channelRequest(entry.chatId, u"get_full_chat_by_id"_q, {},
					[=](QJsonValue, QString) {
						if (guard.expired() || epoch != _dialogsEpoch) return;
						if (--state->remaining == 0) finishItems(state->entries, state->messages);
					});
			}
			if (entry.contactId > 1) {
				const auto userId = peerUserForChat(entry.chatId);
				_rpc->call(
					u"get_contact"_q,
					{ _accountId, entry.contactId },
					[=](QJsonObject one) {
						if (guard.expired() || epoch != _dialogsEpoch) {
							return;
						}
						if (!HasError(one)) {
							const auto contact = one.value(u"result"_q).toObject();
							const auto status = contact.value(u"status"_q)
								.toString()
								.trimmed();
							const auto address = contact.value(u"address"_q)
								.toString();
							const auto image = contact.value(u"profileImage"_q)
								.toString();
							RememberSubtitle(
								userId,
								contact.value(u"lastSeen"_q).toVariant().toLongLong(),
								!status.isEmpty() ? status : address);
							if (!image.isEmpty()) {
								const auto chatId = chatForPeerUser(userId);
								_avatars.insert(chatId, image);
								for (auto &stored : state->entries) {
									if (peerUserForChat(stored.chatId) == userId) {
										stored.avatar = image;
									}
								}
								const auto name = contact.value(u"displayName"_q)
									.toString();
								_session->data().processUsers(MTP_vector<MTPUser>(
									QVector<MTPUser>{ MakeUser(
										userId,
										name.isEmpty()
											? _names.value(chatId)
											: name,
										false,
										image) }));
							}
						}
						if (--state->remaining == 0) {
							finishItems(state->entries, state->messages);
						}
					});
			}
			if (!entry.lastMessageId) {
				if (--state->remaining == 0) {
					finishItems(state->entries, state->messages);
				}
				continue;
			}
			fetchMessage(
				entry.lastMessageId,
				[=, chatId = entry.chatId](QJsonObject one) {
					if (guard.expired() || epoch != _dialogsEpoch) {
						return;
					}
					if (!HasError(one)) {
						state->messages.push_back(MessageFromJson(
							peerUserForChat(chatId),
							one.value(u"result"_q).toObject()));
					}
					if (--state->remaining == 0) {
						finishItems(state->entries, state->messages);
					}
				});
		}
	}

	void finishItems(
			const std::vector<Entry> &entries,
			const std::vector<MTPMessage> &messages) {
		auto users = QVector<MTPUser>();
		auto dialogs = QVector<MTPDialog>();
		auto list = QVector<MTPMessage>();
		for (const auto &message : messages) {
			list.push_back(message);
		}
		users.push_back(selfUser());
		for (const auto &entry : entries) {
			const auto userId = peerUserForChat(entry.chatId);
			if (userId != kSelfUserId) {
				users.push_back(MakeUser(
					userId,
					entry.name,
					false,
					entry.avatar));
			}
			if (entry.lastMessageId) {
				dialogs.push_back(MakeDialog(
					userId,
					entry.lastMessageId,
					entry.fresh,
					entry.pinned,
					entry.muted));
			}
		}
		auto &owner = _session->data();
		owner.processUsers(MTP_vector<MTPUser>(users));
		showSelfAvatar();
		owner.applyDialogs(nullptr, list, dialogs, int(dialogs.size()));
		for (const auto &entry : entries) {
			const auto history = owner.history(
				peerFromUser(UserId(peerUserForChat(entry.chatId))));
			if (!history->folderKnown()) {
				history->clearFolder();
			}
			if (!history->inChatList()) {
				owner.refreshChatListEntry(history);
			}
		}
		owner.chatsList()->setLoaded();
	}

public:
	[[nodiscard]] std::shared_ptr<DeltaRpc> rpc() const {
		return _rpc;
	}

	const not_null<Main::Session*> _session;
	const std::shared_ptr<DeltaRpc> _rpc;
	int _accountId = 0;
	QString _selfName;
	QString _selfAvatar;
	int _selfAvatarRevision = 0;
	int _dialogsEpoch = 0;
	QSet<int> _deletedMessages;
	bool _ignoreEvents = false;
	const std::shared_ptr<bool> _aliveOwner = std::make_shared<bool>(true);
	const std::weak_ptr<bool> _alive = _aliveOwner;
	QHash<int, QString> _names;
	QHash<int, QString> _avatars;
	int _selfChat = 0;
	QTimer _reload;
	bool _pumping = false;
	rpl::variable<int> _connectivity = 0;

};

} // namespace

bool Active() {
	return GlobalBridge != nullptr;
}

std::optional<int> ChannelViewCount(int messageId) {
	const auto found = ChannelViews.constFind(messageId);
	return found == ChannelViews.cend() ? std::nullopt : std::optional<int>(found.value());
}

void MessageInfo(int messageId, Fn<void(QString, QString)> done) {
	if (!GlobalBridge) {
		done({}, u"No active account."_q);
		return;
	}
	const auto bridge = GlobalBridge;
	const auto guard = bridge->_alive;
	const auto epoch = bridge->_dialogsEpoch;
	bridge->_rpc->call(u"get_message_info"_q, { bridge->_accountId, messageId },
		[=](QJsonObject reply) {
			if (guard.expired() || epoch != bridge->_dialogsEpoch) return;
			done(reply.value(u"result"_q).toString(), HasError(reply) ? ErrorText(reply) : QString());
		});
}

QJsonObject SharedContactData(int messageId) {
	return SharedContacts.value(messageId);
}

void DeleteContact(int contactId, bool deleteChat, Fn<void(QString)> done) {
	if (!GlobalBridge || contactId <= 9) {
		done(u"No contact selected."_q);
		return;
	}
	const auto bridge = GlobalBridge;
	const auto guard = bridge->_alive;
	const auto epoch = bridge->_dialogsEpoch;
	const auto valid = [=] { return !guard.expired() && epoch == bridge->_dialogsEpoch; };
	const auto remove = [=] {
		if (!valid()) return;
		bridge->_rpc->call(u"delete_contact"_q, { bridge->_accountId, contactId }, [=](QJsonObject reply) {
			if (!valid()) return;
			bridge->_reload.start();
			done(HasError(reply) ? ErrorText(reply) : QString());
		});
	};
	if (!deleteChat) {
		remove();
		return;
	}
	bridge->_rpc->call(u"get_chat_id_by_contact_id"_q, { bridge->_accountId, contactId }, [=](QJsonObject reply) {
		if (!valid()) return;
		if (HasError(reply)) { done(ErrorText(reply)); return; }
		const auto chatId = reply.value(u"result"_q).toInt();
		if (!chatId) { remove(); return; }
		bridge->_rpc->call(u"delete_chat"_q, { bridge->_accountId, chatId }, [=](QJsonObject reply) {
			if (!valid()) return;
			if (HasError(reply)) { done(ErrorText(reply)); return; }
			if (const auto peer = bridge->_session->data().userLoaded(UserId(chatId))) {
				bridge->_session->data().deleteConversationLocally(peer);
			}
			bridge->_reload.start();
			remove();
		});
	});
}

bool IsChannel(uint64 userId) {
	const auto type = ChannelInfos.value(int(userId)).value(u"chatType"_q).toString();
	return Active() && (type == u"OutBroadcast"_q || type == u"InBroadcast"_q);
}

bool IsReadOnlyChannel(uint64 userId) {
	return IsChannel(userId) && ChannelInfos.value(int(userId)).value(u"chatType"_q).toString() == u"InBroadcast"_q;
}

rpl::producer<bool> ChannelWriteAccess(uint64 userId) {
	return ChannelRevision.value() | rpl::map([=](int) { return !IsReadOnlyChannel(userId); });
}

rpl::producer<QString> ChannelStatusValue(uint64 userId) {
	return ChannelRevision.value() | rpl::map([=](int) { return StatusSubtitle(userId).value_or(u"channel"_q); });
}

void ChannelRequest(int userId, const QString &method, const QJsonArray &args,
		Fn<void(QJsonValue, QString)> done) {
	if (!GlobalBridge) { done({}, u"Delta Chat is not running."_q); return; }
	GlobalBridge->channelRequest(userId, method, args, std::move(done));
}

std::optional<QString> StatusSubtitle(uint64 userId) {
	if (!GlobalBridge) {
		return std::nullopt;
	}
	if (IsChannel(userId)) {
		if (IsReadOnlyChannel(userId)) return u"channel"_q;
		const auto info = ChannelInfos.value(int(userId));
		if (!info.contains(u"contactIds"_q)) return u"channel"_q;
		const auto ids = info.value(u"contactIds"_q).toArray();
		const auto count = std::count_if(ids.begin(), ids.end(), [](const QJsonValue &id) { return id.toInt() > 9; });
		return count == 1 ? u"1 subscriber"_q : u"%1 subscribers"_q.arg(count);
	}
	const auto known = PeerSubtitles.constFind(int(userId));
	if (known != PeerSubtitles.cend() && known->lastSeen > 0) {
		return std::nullopt;
	}
	if (known != PeerSubtitles.cend()) {
		return known->line;
	}
	return QString();
}

std::shared_ptr<DeltaRpc> SharedRpc() {
	return GlobalBridge ? GlobalBridge->rpc() : nullptr;
}

QString SelfAddress() {
	return GlobalSelfAddress;
}

bool IsSystemUser(int userId) {
	return GlobalSystemUsers.contains(userId);
}

rpl::producer<int> Connectivity() {
	if (!GlobalBridge) {
		return rpl::single(0);
	}
	return GlobalBridge->connectivity();
}

QImage AvatarImage(quint64 photoId) {
	const auto path = AvatarFiles.value(qint64(photoId));
	if (path.isEmpty()) {
		return {};
	}
	return Images::Read({
		.path = path,
		.maxSize = QSize(256, 256),
	}).image;
}

std::optional<DeliveryState> MessageDelivery(int messageId) {
	const auto i = CoreMessages.constFind(messageId);
	if (i == CoreMessages.cend()
		|| (i->contains(u"fromId"_q) && i->value(u"fromId"_q).toInt() != 1)) {
		return std::nullopt;
	}
	const auto state = i->value(u"state"_q).toInt();
	return (state == 28 || (state == 26 && CoreReadReceipts.value(messageId) > 0))
		? DeliveryState::Read
		: state == 26 ? DeliveryState::Sent
		: state == 24 ? DeliveryState::Failed
		: DeliveryState::Pending;
}

void ApplyMessageData(not_null<HistoryItem*> item) {
	if (!Active()) {
		return;
	}
	const auto data = CoreMessages.value(item->id.bare);
	const auto path = data.value(u"file"_q).toString();
	const auto mime = data.value(u"fileMime"_q).toString();
	if (LooksLikeVideo(data.value(u"viewType"_q).toString(), mime, path)
		&& !path.isEmpty() && QFileInfo::exists(path)) {
		const auto id = DocumentId(0x6000000000000000ULL | uint64(item->id.bare));
		const auto document = item->history()->owner().document(id);
		if (document->filepath(true) != path) {
			document->setLocalVideo(
				data.value(u"fileName"_q).toString(),
				mime.isEmpty() ? u"video/mp4"_q : mime,
				QSize(data.value(u"dimensionsWidth"_q).toInt(),
					data.value(u"dimensionsHeight"_q).toInt()),
				data.value(u"duration"_q).toInt());
			document->size = QFileInfo(path).size();
			document->setLocation(Core::FileLocation(path));
		}
		item->setLocalVideo(document, data.value(u"text"_q).toString());
	}
	item->history()->owner().notifyItemDataChange(item);
	item->history()->updateChatListEntry();
	item->history()->owner().requestItemViewRefresh(item);
}

QColor SelfAvatarColor() {
	return GlobalSelfAvatarColor;
}

QImage PhotoImage(uint64 photoId) {
	const auto path = PhotoFiles.value(qint64(photoId));
	if (path.isEmpty() || !QFileInfo::exists(path)) {
		return {};
	}
	return Images::Read({
		.path = path,
		.maxSize = QSize(1280, 1280),
	}).image;
}

void FetchUrl(const QString &url, Fn<void(QByteArray, QString)> done) {
	if (!GlobalBridge) {
		done(QByteArray(), u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->fetchUrl(url, std::move(done));
}

void FetchConnectivityHtml(Fn<void(QString)> done) {
	if (!GlobalBridge) {
		done(QString());
		return;
	}
	GlobalBridge->fetchConnectivityHtml(std::move(done));
}

void AnswerRequest(
		not_null<MTP::Instance*> instance,
		mtpRequestId requestId,
		const mtpBuffer &request) {
	if (GlobalBridge) {
		GlobalBridge->answer(instance, requestId, request);
		return;
	}
	crl::on_main([=] {
		if (!instance->hasCallback(requestId)) {
			return;
		}
		auto response = MTP::Response();
		response.requestId = requestId;
		MTPRpcError(MTP_rpc_error(
			MTP_int(400),
			MTP_string("DELTA_NOT_SUPPORTED")
		)).write(response.reply);
		instance->processCallback(response);
	});
}

void AddContact(
		const QString &name,
		const QString &email,
		Fn<void(int, QString)> done) {
	if (!GlobalBridge) {
		done(0, u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->addContact(name, email, std::move(done));
}

void RememberPendingFile(uint64 peer, int64 msg, const QString &path) {
	if (!path.isEmpty()) {
		PendingUploads.insert(PendingKey(peer, msg), path);
	}
}

bool SendItemText(
		not_null<HistoryItem*> item,
		const QString &text,
		Fn<void(bool)> done) {
	if (!GlobalBridge || text.trimmed().isEmpty()) {
		return false;
	}
	const auto peer = item->history()->peer;
	const auto userId = peer->isSelf() ? kSelfUserId : int(peer->id.value);
	const auto replyId = item->replyToId();
	const auto quotedId = (!replyId || IsClientMsgId(replyId))
		? 0
		: int(replyId.bare);
	const auto fullId = item->fullId();
	const auto weak = base::make_weak(&item->history()->session());
	GlobalBridge->sendText(
		userId,
		text,
		quotedId,
		[=](int id, QString error) {
			const auto session = weak.get();
			if (!session) {
				if (done) {
					done(false);
				}
				return;
			}
			const auto local = session->data().message(fullId);
			if (!error.isEmpty() || id <= 0) {
				if (local) {
					local->sendFailed();
				}
				if (done) {
					done(false);
				}
				return;
			}
			if (local && local->isSending()) {
				local->setRealId(MsgId(id));
			}
			if (done) {
				done(true);
			}
		});
	return true;
}

void SendReaction(int messageId, const QJsonArray &reactions) {
	if (!GlobalBridge) {
		return;
	}
	GlobalBridge->sendReaction(messageId, reactions);
}

bool SendItemMedia(not_null<HistoryItem*> item, Fn<void(bool)> done) {
	if (!GlobalBridge) {
		return false;
	}
	const auto peer = item->history()->peer;
	const auto userId = peer->isSelf() ? kSelfUserId : int(peer->id.value);
	auto path = PendingUploads.take(PendingKey(peer->id.value, item->id.bare));
	if (path.isEmpty()) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				path = document->filepath(true);
			}
		}
	}
	if (path.isEmpty() || !QFileInfo::exists(path)) {
		return false;
	}
	auto decoded = ::Images::Read({
		.path = path,
		.maxSize = QSize(1280, 1280),
		.returnContent = true,
	});
	if (!decoded.image.isNull()) {
		if (const auto media = item->media()) {
			if (const auto photo = media->photo()) {
				if (const auto view = photo->activeMediaView()) {
					view->set(
						Data::PhotoSize::Large,
						Data::PhotoSize::Large,
						decoded.image,
						decoded.content);
				} else {
					::KeepReadyPhoto(
						photo->id,
						decoded.image,
						decoded.content);
				}
				item->history()->owner().requestItemRepaint(item);
			}
		}
	}
	const auto caption = item->originalText().text;
	auto viewType = u"File"_q;
	const auto lower = path.toLower();
	if (LooksLikeImage(QString(), QString(), path)) {
		viewType = u"Image"_q;
	} else if (LooksLikeVoice(QString(), QString(), path)) {
		viewType = u"Voice"_q;
	}
	if (const auto media = item->media()) {
		if (media->photo()) {
			viewType = u"Image"_q;
		} else if (const auto document = media->document()) {
			if (document->isVoiceMessage()) {
				viewType = u"Voice"_q;
			} else if (document->isVideoFile() || document->isVideoMessage()
				|| (document->isAnimation()
					&& LooksLikeVideo(QString(), document->mimeString(), path))) {
				viewType = u"Video"_q;
			}
		}
	}
	const auto replyId = item->replyToId();
	const auto quotedId = (!replyId || IsClientMsgId(replyId))
		? 0
		: int(replyId.bare);
	const auto fullId = item->fullId();
	const auto weak = base::make_weak(&item->history()->session());
	GlobalBridge->sendFile(
		userId,
		path,
		caption,
		viewType,
		quotedId,
		[=](int id, QString error) {
			const auto session = weak.get();
			if (!session) {
				if (done) {
					done(false);
				}
				return;
			}
			const auto local = session->data().message(fullId);
			if (!error.isEmpty() || id <= 0) {
				if (local) {
					local->sendFailed();
				}
				if (done) {
					done(false);
				}
				return;
			}
			if (local && local->isSending()) {
				local->setRealId(MsgId(id));
			}
			if (done) {
				done(true);
			}
		});
	return true;
}

void SetSelfAvatar(const QString &path, Fn<void(QString)> done) {
	if (!GlobalBridge) {
		if (done) {
			done(u"Delta Chat is not running."_q);
		}
		return;
	}
	GlobalBridge->setSelfAvatar(path, std::move(done));
}

void SearchMessages(
		int userId,
		const QString &query,
		Fn<void(QVector<MTPMessage>, QVector<MTPUser>)> done) {
	if (!GlobalBridge) {
		done({}, {});
		return;
	}
	GlobalBridge->searchMessages(userId, query, std::move(done));
}

void ListTransports(Fn<void(QJsonArray, QString)> done) {
	if (!GlobalBridge) {
		done({}, u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->listTransports(std::move(done));
}

void UpdateTransport(const QJsonObject &transport, Fn<void(QString)> done) {
	if (!GlobalBridge) {
		if (done) {
			done(u"Delta Chat is not running."_q);
		}
		return;
	}
	GlobalBridge->updateTransport(transport, std::move(done));
}

void DeleteTransport(const QString &addr, Fn<void(QString)> done) {
	if (!GlobalBridge) {
		if (done) {
			done(u"Delta Chat is not running."_q);
		}
		return;
	}
	GlobalBridge->deleteTransport(addr, std::move(done));
}

void SetChatArchived(
		bool self,
		int userId,
		bool archived,
		Fn<void(QString)> done) {
	if (!GlobalBridge) {
		if (done) {
			done(u"Delta Chat is not running."_q);
		}
		return;
	}
	GlobalBridge->setChatArchived(self, userId, archived, std::move(done));
}

void EditMessageText(int messageId, const QString &text, Fn<void(QString)> done) {
	if (!GlobalBridge) {
		if (done) {
			done(u"Delta Chat is not running."_q);
		}
		return;
	}
	GlobalBridge->editMessageText(messageId, text, std::move(done));
}

void AddTransportFromQr(const QString &qr, Fn<void(QString)> done) {
	if (!GlobalBridge) {
		if (done) {
			done(u"Delta Chat is not running."_q);
		}
		return;
	}
	GlobalBridge->addTransportFromQr(qr, std::move(done));
}

void StartSecondDevice(
		Fn<void(QString, QString)> ready,
		Fn<void(QString)> finished) {
	if (!GlobalBridge) {
		if (ready) {
			ready(QString(), u"Delta Chat is not running."_q);
		}
		return;
	}
	GlobalBridge->startSecondDevice(std::move(ready), std::move(finished));
}

void StopSecondDevice() {
	if (GlobalBridge) {
		GlobalBridge->stopSecondDevice();
	}
}

void LoadInviteQr(
		Fn<void(QString, QString, QString, QString)> done) {
	if (!GlobalBridge) {
		done(QString(), QString(), QString(), u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->loadInviteQr(std::move(done));
}

void JoinInviteQr(const QString &qr, Fn<void(QString)> done) {
	if (!GlobalBridge) {
		done(u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->joinInviteQr(qr, std::move(done));
}

void AccountRequest(const QString &method, const QJsonArray &args,
		Fn<void(QJsonValue, QString)> done) {
	if (!GlobalBridge) { done({}, u"Delta Chat is not running."_q); return; }
	GlobalBridge->accountRequest(method, args, std::move(done));
}

void JoinInviteChat(const QString &qr, Fn<void(int, QString)> done) {
	if (!GlobalBridge) { done(0, u"Delta Chat is not running."_q); return; }
	GlobalBridge->joinInviteChat(qr, std::move(done));
}

void SetSelfName(const QString &name, Fn<void(QString)> done) {
	if (!GlobalBridge) {
		done(u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->setConfig(u"displayname"_q, name, std::move(done));
}

void SetSelfAbout(const QString &about, Fn<void(QString)> done) {
	if (!GlobalBridge) {
		done(u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->setConfig(u"selfstatus"_q, about, std::move(done));
}

void ListContacts(Fn<void(std::vector<ContactRow>, QString)> done, bool addressOnly) {
	if (!GlobalBridge) {
		done({}, u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->listContacts(std::move(done), addressOnly);
}

void OpenContact(const ContactRow &contact, Fn<void(int, QString)> done) {
	if (!GlobalBridge) {
		done(0, u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->openContact(contact, std::move(done));
}

void ShareContact(int contactId, int peerUserId, Fn<void(QString)> done) {
	if (!GlobalBridge) { done(u"Delta Chat is not running."_q); return; }
	GlobalBridge->shareContact(contactId, peerUserId, std::move(done));
}

void CreateGroup(
		const QString &name,
		const QString &description,
		const std::vector<int> &contactIds,
		const QString &imagePath,
		Fn<void(int, QString)> done) {
	if (!GlobalBridge) {
		done(0, u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->createGroup(
		name,
		description,
		contactIds,
		imagePath,
		std::move(done));
}

void CreateChannel(const QString &name, Fn<void(int, QString)> done) {
	if (!GlobalBridge) {
		done(0, u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->createNamedChat(true, name, std::move(done));
}

void CreateChannel(const QString &name, const QString &description,
		const QString &imagePath, Fn<void(int, QString)> done) {
	if (!GlobalBridge) { done(0, u"Delta Chat is not running."_q); return; }
	GlobalBridge->createGroup(name, description, {}, imagePath, std::move(done), true);
}

void SendChannelInvite(int channelUserId, int recipientUserId, Fn<void(QString)> done) {
	if (!GlobalBridge) { done(u"Delta Chat is not running."_q); return; }
	GlobalBridge->sendChannelInvite(channelUserId, recipientUserId, std::move(done));
}

void LoadChannelInvite(int userId, Fn<void(QString, QString, QString)> done) {
	if (!GlobalBridge) { done({}, {}, u"Delta Chat is not running."_q); return; }
	GlobalBridge->loadChannelInvite(userId, std::move(done));
}

void RemoveProfile(int accountId, Fn<void(QString)> done) {
	if (!GlobalBridge) {
		if (done) {
			done(u"Delta Chat is not running."_q);
		}
		return;
	}
	GlobalBridge->removeProfile(accountId, std::move(done));
}

void MoveProfileToTop(int accountId, Fn<void(QString)> done) {
	if (!GlobalBridge) {
		if (done) {
			done(u"Delta Chat is not running."_q);
		}
		return;
	}
	GlobalBridge->moveProfileToTop(accountId, std::move(done));
}

void ListProfiles(Fn<void(QJsonArray)> done) {
	if (!GlobalBridge) {
		done({});
		return;
	}
	GlobalBridge->listProfiles(std::move(done));
}

void SwitchProfile(int accountId, Fn<void(QString)> done) {
	if (!GlobalBridge) {
		done(u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->switchProfile(accountId, std::move(done));
}

void AddProfile(const QString &name, Fn<void(QString)> done) {
	if (!GlobalBridge) {
		done(u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->addProfile(name, std::move(done));
}

void StartSession(
		not_null<Main::Account*> account,
		std::shared_ptr<DeltaRpc> rpc,
		int accountId,
		const QString &displayName) {
	crl::on_main([=] {
		if (account->sessionExists()) {
			return;
		}
		account->createSession(
			MakeUser(kSelfUserId, displayName, true),
			std::make_unique<Main::SessionSettings>());
		const auto session = &account->session();
		const auto bridge = session->lifetime().make_state<Bridge>(
			session,
			rpc,
			accountId,
			displayName);
		bridge->start();
	});
}

} // namespace Delta
