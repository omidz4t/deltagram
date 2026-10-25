/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#include "delta/delta_bridge.h"
#include "delta/compose_queue_sender.h"
#include "delta/profile_activity.h"
#include "delta/chat_mute.h"

#include "apiwrap.h"
#include "delta/account_scope.h"
#include "delta/backup_transfer.h"
#include "delta/message_order.h"
#include "delta/message_visibility.h"
#include "delta/message_download.h"
#include "delta/message_capabilities.h"
#include "delta/media_identity.h"
#include "delta/avatar_image.h"

#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/file_location.h"
#include "data/components/recent_peers.h"
#include "data/components/top_peers.h"
#include "data/data_document.h"
#include "data/data_drafts.h"
#include "data/data_changes.h"
#include "data/data_histories.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/notify/data_notify_settings.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "media/player/media_player_instance.h"
#include "mtproto/details/mtproto_serialized_request.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_response.h"
#include "rpl/variable.h"
#include "storage/storage_account.h"
#include "storage/file_upload.h"
#include "ui/image/image_location.h"
#include "ui/image/image_prepare.h"
#include "window/window_session_controller.h"
#include "window/notifications_manager.h"

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
#include <map>
#include <memory>

namespace Delta {

class PreparedSend final {
public:
	~PreparedSend() { if (released) released(); }
	int account = 0;
	int chat = 0;
	int epoch = 0;
	bool owned = false;
	bool finished = false;
	ComposeDraft saved;
	Fn<void()> released;
};

namespace {

constexpr auto kSelfUserId = 1'000'000;
constexpr auto kContactUserIdBase = uint64(1) << 40;
[[nodiscard]] qint64 MessageMediaId(int messageId);

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

QHash<uint64, PeerSubtitle> PeerSubtitles;
QHash<uint64, QJsonObject> ChannelInfos;
QHash<int, int> ChannelViews;
QHash<int, QJsonObject> SharedContacts;
QSet<uint64> GroupChats;
QSet<uint64> DirectChats;
QHash<uint64, ContactRow> SenderContacts;
rpl::variable<int> ChannelRevision = 0;
rpl::variable<int> AccountViewRevision = 0;
rpl::variable<int> ComposerRevision = 0;
rpl::variable<int> ProfileActivityRevision = 0;

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
struct PendingUpload {
	QString path;
	QString filename;
	std::shared_ptr<void> owner;
	bool forceFile = false;
	std::shared_ptr<PreparedSend> preparation;
};
struct PendingComposeBatch {
	std::shared_ptr<ComposeBatch> batch;
	ComposeBatch::State lastState = ComposeBatch::State::Preparing;
};
QHash<QString, PendingUpload> PendingUploads;

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
	const auto photoId = MessageMediaId(id);
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
	const auto documentId = MessageMediaId(id);
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
	return MTP_message(
		MTP_flags(flags),
		MTP_int(id),
		MTP_peerUser(MTP_long(out ? kSelfUserId : peerUser)),
		MTPint(), // from_boosts_applied
		MTPstring(), // from_rank
		MTP_peerUser(MTP_long(peerUser)),
		MTPPeer(), // saved_peer_id
		MTPMessageFwdHeader(),
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
		MTP_flags(0), // The donor reactor-list RPC is not a Core capability.
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
		ReactionsFromJson(data),
		replyId,
		quoteText);
}

[[nodiscard]] MTPDialog MakeDialog(
		int peerUser,
		int topMessage,
		int unread,
		int firstUnread,
		bool pinned,
		bool muted) {
	auto flags = MTPDdialog::Flags(0);
	if (pinned) {
		flags |= MTPDdialog::Flag::f_pinned;
	}
	// The unread snapshot can race a read/deletion, or its lookup can fail.
	// Keep a real history anchor instead of inventing message ID 1.
	const auto unreadAnchor = firstUnread > 0 ? firstUnread : topMessage;
	const auto readInbox = unread > 0 ? std::max(unreadAnchor - 1, 0) : topMessage;
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
		_backups = std::make_unique<BackupTransfers>(_rpc);
		_replies = std::make_unique<ReplyStore>(
			[rpc = _rpc](QString method, QJsonArray params, DeltaRpc::Callback done) {
				rpc->call(method, params, std::move(done));
			}, nullptr);
		_drafts = std::make_shared<DraftStore>(_rpc, [=](QString error) {
			reportError(error);
		});
		_queues = std::make_shared<ComposeQueueStore>(_rpc);
		_queueSender = std::make_unique<ComposeQueueSender>(_rpc, _queues, _drafts);
		_profileActivity = std::make_unique<ProfileActivity>(
			[rpc = _rpc](QString method, QJsonArray params, DeltaRpc::Callback done) {
				rpc->call(method, params, std::move(done));
			}, [] { ProfileActivityRevision = ProfileActivityRevision.current() + 1; },
			[=](QString error) { reportError(error); });
		_reload.setSingleShot(true);
		_reload.setInterval(150);
		QObject::connect(&_reload, &QTimer::timeout, [=] {
			loadDialogs();
		});
	}
	~Bridge() {
		_accountScope.invalidate();
		_backups.reset();
		if (GlobalBridge == this) {
			PendingUploads.clear();
			GlobalBridge = nullptr;
			ProfileActivityRevision = ProfileActivityRevision.current() + 1;
			GlobalSelfAvatarColor = QColor();
			CoreMessages.clear();
			CoreReadReceipts.clear();
		}
	}

	void call(const QString &method, const QJsonArray &params, DeltaRpc::Callback done) {
		_rpc->call(method, params, _accountScope.bind([=](QJsonObject reply) {
			if (done) done(std::move(reply));
		}));
	}

	void reportError(const QString &error) {
		for (const auto &window : _session->windows()) {
			window->showToast(error);
		}
	}

	void deleteHistory(
			bool self,
			int userId,
			bool justClear,
			bool revoke,
			Fn<void(HistoryDeletionResult)> done,
			std::optional<HistoryDateRange> dates) {
		DeleteChatHistory(_rpc, _accountScope, _accountId,
			chatForPeerUser(self ? kSelfUserId : userId),
			!justClear && !self, revoke, [=](HistoryDeletionResult result) {
				for (const auto id : result.deletedIds) _deletedMessages.insert(id);
				_reload.start();
				if (!result.error.isEmpty()) reportError(result.error);
				if (done) done(std::move(result));
			}, dates);
	}

	void saveDraft(int userId, const QString &text, int quotedMessageId) {
		const auto chatId = chatForPeerUser(userId);
		if (_queueRuns.contains({ _accountId, chatId }) || _batches.contains({ _accountId, chatId })) return;
		if (chatId > 0) _drafts->save(_accountId, chatId, text, quotedMessageId);
	}

	void sendNotificationReply(uint64 userId, QString text) {
		if (!userId || userId > uint64(std::numeric_limits<int>::max())) return;
		const auto chat = chatForPeerUser(int(userId));
		const auto current = _accountScope.guard();
		const auto alive = _alive;
		_replies->submit(_accountId, chat, std::move(text), [=](int id, QString error) {
			if (alive.expired()) return;
			if (!current()) {
				if (!error.isEmpty()) reportError(u"A notification reply in your previous profile needs review. Switch back and open Saved replies."_q);
				return;
			}
			if (id > 0) addIncoming(chat, id);
			if (!error.isEmpty()) reportError(error);
		});
	}

	void loadSavedReplies(Fn<void(std::vector<SavedReply>, QString)> done) {
		const auto current = _accountScope.guard();
		_replies->load(_accountId, [=](std::vector<SavedReply> replies, QString error) {
			if (!current()) return;
			for (auto &reply : replies) {
				reply.chatName = _names.value(reply.chat, u"Conversation unavailable"_q);
				reply.userId = _names.contains(reply.chat) ? peerUserForChat(reply.chat) : 0;
			}
			done(std::move(replies), std::move(error));
		});
	}

	void changeSavedReply(SavedReply reviewed, bool discard, Fn<void(QString)> done) {
		if (_switching) { done(u"Wait for the profile switch to finish."_q); return; }
		const auto current = _accountScope.guard();
		const auto chat = reviewed.chat;
		const auto finished = [=](int id, QString error) {
			if (!current()) return;
			if (!discard && id > 0) addIncoming(chat, id);
			done(std::move(error));
		};
		if (discard) _replies->discard(_accountId, std::move(reviewed), finished);
		else _replies->send(_accountId, std::move(reviewed), finished);
	}

	void noticeSavedReplies() {
		const auto current = _accountScope.guard();
		_replies->load(_accountId, [=](std::vector<SavedReply> replies, QString error) {
			if (!current()) return;
			if (!error.isEmpty()) reportError(error);
			else if (!replies.empty()) reportError(u"Notification replies need review. Open Saved replies from the main menu."_q);
		});
	}

	void loadDraft(int userId, Fn<void(ComposeDraft)> done) {
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		_drafts->load(_accountId, chatForPeerUser(userId), [=](ComposeDraft draft, QString error) {
			if (guard.expired() || epoch != _dialogsEpoch || !error.isEmpty()) return;
			done(std::move(draft));
			ComposerRevision = ComposerRevision.current() + 1;
		});
	}

	bool composerWritable(int userId) const {
		const auto chat = chatForPeerUser(userId);
		const auto i = _preparations.find({ _accountId, chat });
		const auto preparation = (i != _preparations.end()) ? i->second.lock() : nullptr;
		return !_switching && _rpc->running()
			&& (!preparation || preparation->finished)
			&& !_batches.contains({ _accountId, chat })
			&& !_queueRuns.contains({ _accountId, chat })
			&& !_drafts->sending(_accountId, chatForPeerUser(userId));
	}

	void loadComposeQueue(int userId, Fn<void(ComposeQueue, QString)> done) {
		const auto guard = _alive;
		const auto current = _accountScope.guard();
		const auto revision = ComposerRevision.current();
		_queues->inspect(_accountId, chatForPeerUser(userId),
			[=](ComposeQueue queue, QString error) {
				if (guard.expired() || !current() || !done) return;
				if (revision != ComposerRevision.current()) {
					done({}, u"The compose state changed. Review the queued messages again."_q);
				} else done(std::move(queue), std::move(error));
			});
	}

	void finishComposeQueue(int account, int chat, int userId,
			std::function<bool()> current, QString error, Fn<void(bool)> done) {
		const auto guard = _alive;
		_drafts->reload(account, chat, [=](ComposeDraft draft, QString readError) {
			if (guard.expired()) return;
			if (!current()) {
				_queueRuns.erase({ account, chat });
				ComposerRevision = ComposerRevision.current() + 1;
				return;
			}
			const auto peer = peerFromUser(UserId(userId));
			if (readError.isEmpty()) {
				if (const auto history = _session->data().historyLoaded(peer)) {
					history->clearLocalDraft({}, {});
					history->clearCloudDraft({}, {});
					if (!draft.text.isEmpty() || draft.quotedMessageId) {
						auto restored = Data::Draft(TextWithTags{ draft.text },
							FullReplyTo{ .messageId = draft.quotedMessageId > 0
								? FullMsgId(peer, MsgId(draft.quotedMessageId)) : FullMsgId() },
							SuggestOptions(), MessageCursor(), Data::WebPageDraft());
						history->createCloudDraft({}, {}, &restored);
					}
					history->applyCloudDraft({}, {});
				}
			}
			const auto failure = !error.isEmpty() ? error : readError;
			if (!failure.isEmpty()) reportError(failure);
			_queueRuns.erase({ account, chat });
			ComposerRevision = ComposerRevision.current() + 1;
			if (done) done(failure.isEmpty());
		});
	}

	void advanceComposeQueue(int account, int chat, int userId,
			std::function<bool()> current, ComposeQueue queue, Fn<void(bool)> done) {
		if (!current() || queue.entries.empty()) {
			finishComposeQueue(account, chat, userId, current, {}, std::move(done));
			return;
		}
		const auto guard = _alive;
		_queueSender->sendNext(account, chat, queue.id, queue.entries.front().id,
			[=](int id, ComposeQueue remaining, QString error) {
				if (guard.expired()) return;
				if (current() && id > 0 && !_deletedMessages.contains(id)) addIncoming(chat, id);
				if (!error.isEmpty()) {
					finishComposeQueue(account, chat, userId, current, std::move(error), done);
				} else {
					advanceComposeQueue(account, chat, userId, current, std::move(remaining), done);
				}
			});
	}

	void sendComposeQueue(int userId, ComposeQueue reviewed, Fn<void(bool)> done) {
		if (!composerWritable(userId) || reviewed.entries.empty()) return;
		const auto chat = chatForPeerUser(userId);
		_queueRuns.insert({ _accountId, chat });
		ComposerRevision = ComposerRevision.current() + 1;
		advanceComposeQueue(_accountId, chat, userId, _accountScope.guard(), std::move(reviewed), std::move(done));
	}

	void discardComposeQueueEntry(int userId, ComposeQueue reviewed, Fn<void(bool)> done) {
		if (!composerWritable(userId) || reviewed.entries.empty()) return;
		const auto account = _accountId;
		const auto chat = chatForPeerUser(userId);
		const auto guard = _alive;
		const auto current = _accountScope.guard();
		_queueRuns.insert({ account, chat });
		ComposerRevision = ComposerRevision.current() + 1;
		_queueSender->discardNext(account, chat, reviewed.id, reviewed.entries.front().id,
			[=](int, ComposeQueue, QString error) {
				if (!guard.expired()) finishComposeQueue(account, chat, userId, current, std::move(error), done);
			});
	}

	std::shared_ptr<ComposeBatch> pendingPreparedBatch(int userId) const {
		const auto i = _batches.find({ _accountId, chatForPeerUser(userId) });
		return (i != _batches.end()) ? i->second.batch : nullptr;
	}

	void checkNewAttachmentBatch(int userId, Fn<void(QString)> done) {
		const auto preparation = beginPreparedSend(userId);
		if (!preparation) { done(u"Wait for the current compose operation before adding a batch."_q); return; }
		const auto account = _accountId;
		const auto chat = chatForPeerUser(userId);
		const auto guard = _alive;
		const auto current = _accountScope.guard();
		const auto finish = [=](QString error) {
			preparation->owned = true;
			preparation->finished = true;
			done(std::move(error));
			ComposerRevision = ComposerRevision.current() + 1;
		};
		_queues->load(account, chat, [=](ComposeQueue queue, QString error) {
			if (guard.expired() || !current()) return;
			if (!error.isEmpty() || !queue.entries.empty()) {
				finish(error.isEmpty() ? u"Review the saved message queue before adding another batch."_q : error);
				return;
			}
			_drafts->settle(account, chat, [=](QString error) {
				if (guard.expired() || !current()) return;
				if (!error.isEmpty()) { finish(error); return; }
				_drafts->reload(account, chat, [=](ComposeDraft draft, QString error) {
					if (guard.expired() || !current()) return;
					finish(!error.isEmpty() ? error : !draft.file.isEmpty()
						? u"Review the saved attachment before adding another batch."_q : QString());
				});
			});
		});
	}

	std::shared_ptr<ComposeBatch> beginPreparedBatch(int userId,
			std::vector<ComposeDraft> selection, Fn<void(bool)> done,
			std::vector<ForwardSource> forwards) {
		if (!composerWritable(userId)) return {};
		const auto offset = selection.size();
		for (const auto &source : forwards) {
			if (source.chat <= 0 || source.message <= 0) return {};
			selection.push_back({ u"Forwarded message"_q });
		}
		const auto chat = chatForPeerUser(userId);
		const auto key = std::make_pair(_accountId, chat);
		const auto guard = _alive;
		const auto current = _accountScope.guard();
		const auto identity = std::make_shared<std::weak_ptr<ComposeBatch>>();
		auto batch = ComposeBatch::Create(_queues, _accountId, chat, std::move(selection), [=] {
			if (guard.expired()) return;
			const auto batch = identity->lock();
			const auto i = _batches.find(key);
			if (!batch || i == _batches.end() || i->second.batch != batch) return;
			const auto state = batch->state();
			const auto changed = i->second.lastState != state;
			i->second.lastState = state;
			if (state == ComposeBatch::State::Saved && current() && !batch->reconciled()) {
				_drafts->settle(key.first, key.second, [=](QString error) {
					if (guard.expired()) return;
					const auto i = _batches.find(key);
					if (i == _batches.end() || i->second.batch != batch) return;
					_batches.erase(i);
					if (current() && error.isEmpty()) sendComposeQueue(userId, batch->queue(), done);
					else {
						ComposerRevision = ComposerRevision.current() + 1;
						if (current()) {
							reportError(error);
							if (done) done(false);
						}
					}
				});
				return;
			}
			if (state == ComposeBatch::State::Saved || state == ComposeBatch::State::Discarded) {
				_batches.erase(i);
				if (state == ComposeBatch::State::Discarded) _session->uploader().cancelComposeBatch(batch);
				ComposerRevision = ComposerRevision.current() + 1;
				if (current() && state == ComposeBatch::State::Saved) {
					reportError(u"The messages are saved. Use the attachment button to review and send them."_q);
				}
				if (done && current()) done(false);
			} else {
				ComposerRevision = ComposerRevision.current() + 1;
				if (changed && current() && state == ComposeBatch::State::Failed) reportError(batch->error());
			}
		});
		if (!batch) return {};
		*identity = batch;
		_batches.emplace(key, PendingComposeBatch{ batch });
		ComposerRevision = ComposerRevision.current() + 1;
		for (auto index = size_t(0); index != forwards.size(); ++index) {
			batch->preparedForward(offset + index, forwards[index]);
		}
		return batch;
	}

	std::optional<std::vector<ForwardSource>> collectForwardSources(
			const std::vector<not_null<HistoryItem*>> &items) const {
		auto result = std::vector<ForwardSource>();
		for (const auto item : items) {
			if (&item->history()->session() != _session.get() || !item->history()->peer->isUser()
				|| !IsServerMsgId(item->id) || _deletedMessages.contains(int(item->id.bare))) return std::nullopt;
			const auto chat = chatForPeerUser(int(peerToUser(item->history()->peer->id).bare));
			if (chat <= 0) return std::nullopt;
			result.push_back({ chat, int(item->id.bare) });
		}
		return result;
	}

	std::shared_ptr<PreparedSend> beginPreparedSend(int userId) {
		if (!composerWritable(userId)) return {};
		const auto chat = chatForPeerUser(userId);
		if (chat <= 0) return {};
		auto result = std::make_shared<PreparedSend>();
		result->account = _accountId;
		result->chat = chat;
		result->epoch = _dialogsEpoch;
		const auto guard = _alive;
		result->released = [=] {
			// The final preparation owner may be released by a worker thread.
			crl::on_main([=] {
				if (!guard.expired()) ComposerRevision = ComposerRevision.current() + 1;
			});
		};
		_preparations[{ _accountId, chat }] = result;
		ComposerRevision = ComposerRevision.current() + 1;
		return result;
	}

	bool hasUnsavedPreparation() const {
		if (!_batches.empty()) return true;
		for (const auto &[key, weak] : _preparations) {
			const auto preparation = weak.lock();
			if (preparation && !preparation->owned) return true;
		}
		return false;
	}

	bool preparingSendPreventsQuit() {
		if (_replies->hasUnsaved()) {
			reportError(u"A notification reply has not finished saving. Open Saved replies in its profile to send or dismiss it before quitting."_q);
			return true;
		}
		if (!_drafts->preparingSend() && !hasUnsavedPreparation()) return false;
		reportError(u"A message has not finished saving. Wait for it to finish, or remove the failed attachment before quitting."_q);
		return true;
	}

	void sendPreparedFile(std::shared_ptr<PreparedSend> preparation,
			ComposeDraft draft, Fn<void(int, QString)> done) {
		if (preparation->account != _accountId || preparation->epoch != _dialogsEpoch) {
			done(0, u"The attachment belongs to an earlier profile view."_q);
			return;
		}
		const auto guard = _alive;
		const auto finished = [=](int id, QString error) {
			if (guard.expired()) return;
			if (preparation->account == _accountId && preparation->epoch == _dialogsEpoch) {
				if (id > 0 && !_deletedMessages.contains(id) && !CoreMessages.contains(id)) {
					CoreMessages.insert(id, QJsonObject{{ u"state"_q, 20 }});
				}
				done(id, std::move(error));
				if (id > 0 && !_deletedMessages.contains(id)) addIncoming(preparation->chat, id);
			}
			preparation->finished = preparation->owned;
			ComposerRevision = ComposerRevision.current() + 1;
		};
		_drafts->sendAttachment(preparation->account, preparation->chat, std::move(draft),
			[=](int id, QString error) {
				if (guard.expired()) return;
				if (id <= 0 && preparation->owned) {
					// A failed reply may follow a committed send. Restore only the
					// draft Core still reports, never the old pre-send snapshot.
					_drafts->load(preparation->account, preparation->chat,
						[=](ComposeDraft current, QString loadError) {
							if (guard.expired()) return;
							preparation->saved = loadError.isEmpty() ? std::move(current) : ComposeDraft();
							finished(id, error);
						});
				} else {
					finished(id, std::move(error));
				}
			}, [=](ComposeDraft saved) {
				if (guard.expired()) return;
				preparation->saved = std::move(saved);
				preparation->owned = true;
			});
		ComposerRevision = ComposerRevision.current() + 1;
	}

	void sendComposerDraft(int userId, ComposeDraft draft, Fn<void(bool)> done) {
		if (!composerWritable(userId)) return;
		const auto account = _accountId;
		const auto chat = chatForPeerUser(userId);
		const auto epoch = _dialogsEpoch;
		const auto guard = _alive;
		const auto completed = [=](int id, QString error) {
			if (guard.expired()) return;
			if (!error.isEmpty()) reportError(error);
			if (account == _accountId) {
				if (id > 0) {
					if (const auto history = _session->data().historyLoaded(peerFromUser(UserId(userId)))) {
						history->clearLocalDraft({}, {});
						history->clearCloudDraft({}, {});
						history->applyCloudDraft({}, {});
					}
					if (!_deletedMessages.contains(id)) addIncoming(chat, id);
				}
				if (epoch == _dialogsEpoch && done) done(id > 0);
			}
			ComposerRevision = ComposerRevision.current() + 1;
		};
		if (draft.file.isEmpty()) {
			_drafts->sendText(account, chat, draft.text, draft.quotedMessageId, completed);
		} else {
			_drafts->sendAttachment(account, chat, std::move(draft), completed);
		}
		ComposerRevision = ComposerRevision.current() + 1;
	}

	void discardDraftAttachment(int userId, const QString &file, Fn<void(bool)> done) {
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		_drafts->discardAttachment(_accountId, chatForPeerUser(userId), file, [=](bool removed) {
			if (guard.expired()) return;
			if (!removed) reportError(u"Attachment removal was not confirmed. Review the saved draft before trying again."_q);
			if (epoch == _dialogsEpoch && done) done(removed);
			ComposerRevision = ComposerRevision.current() + 1;
		});
		ComposerRevision = ComposerRevision.current() + 1;
	}

	QString draftAttachmentName(int userId) const {
		return _drafts->attachmentName(_accountId, chatForPeerUser(userId));
	}

	void start() {
		Core::App().notifications().resetSession(_session);
		noticeSavedReplies();
		const auto guard = _alive;
		_profileActivity->start();
		_rpc->setEventHandler([=](QJsonObject event) {
			if (!guard.expired() && HasError(event)) {
				_connectivity = 1000;
				ComposerRevision = ComposerRevision.current() + 1;
				reportError(u"Messaging stopped. Restart Deltagram to reconnect."_q);
			}
		});
		call(
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
		call(
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
		call(
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
		call(
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
		call(
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
		call(
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

	void downloadMessage(int messageId) {
		if (_deletedMessages.contains(messageId)) return;
		const auto chatId = CoreMessages.value(messageId).value(u"chatId"_q).toInt();
		DownloadCoreMessage(_rpc, _accountScope, _accountId, messageId,
			[=](QString error) {
				if (!error.isEmpty()) reportError(error);
				if (_rpc->running() && chatId > 0) addIncoming(chatId, messageId);
			});
	}

	void requestMessageData(int userId, int messageId, Fn<void()> done) {
		ReadVisibleMessage(_rpc, _accountScope,
			_accountId, userId ? chatForPeerUser(userId) : 0,
			messageId, [=](QJsonObject message) {
				if (!message.isEmpty() && !_deletedMessages.contains(messageId)) {
					const auto chatId = message.value(u"chatId"_q).toInt();
					const auto peerUser = peerUserForChat(chatId);
					// Existing items must not generate incoming notifications.
					_session->data().processMessages(
						QVector<MTPMessage>{ MessageFromJson(peerUser, message) },
						NewMessageType::Existing);
				}
				if (done) done();
			});
	}

	void fetchMessage(int msgId, Fn<void(QJsonObject)> done) {
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		const auto accountId = _accountId;
		call(
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
				const auto data = one.value(u"result"_q).toObject();
				const auto chatId = data.value(u"chatId"_q).toInt();
				if (!HasError(one) && !data.value(u"isInfo"_q).toBool()
					&& (ChannelInfos.value(chatId).value(u"chatType"_q).toString() == u"OutBroadcast"_q
						|| (data.value(u"fromId"_q).toInt() == 1
							&& data.value(u"state"_q).toInt() >= 26))) {
					call(u"get_message_read_receipt_count"_q, { accountId, msgId }, [=](QJsonObject count) {
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
		DeleteCoreMessages(_rpc, _accountScope, _accountId, ids, revoke,
			[=](QString error) {
				if (!error.isEmpty()) {
					reportError(error);
					return fail(instance, requestId, u"DELTA_DELETE_FAILED"_q);
				}
				for (const auto &id : ids) _deletedMessages.insert(id.toInt());
				_reload.start();
				const auto boxed = MTPmessages_AffectedMessages(
					MTP_messages_affectedMessages(MTP_int(0), MTP_int(0)));
				reply(instance, requestId, [=](mtpBuffer &to) {
					boxed.write(to);
				});
			});
	}

	void sendText(
			int userId,
			const QString &text,
			int quotedId,
			Fn<void(int, QString)> done) {
		QJsonObject data;
		data.insert(u"text"_q, text);
		if (quotedId > 0) {
			data.insert(u"quotedMessageId"_q, quotedId);
		}
		const auto guard = _alive;
		const auto accountId = _accountId;
		const auto chatId = chatForPeerUser(userId);
		const auto epoch = _dialogsEpoch;
		_rpc->call(
			u"send_msg"_q,
			{ accountId, chatId, data },
			[=](QJsonObject result) {
				if (guard.expired()) {
					return;
				}
				if (epoch != _dialogsEpoch) {
					// The view callback belongs to the old profile, but rejected
					// text still belongs in that profile's Core draft.
					if (HasError(result) || result.value(u"result"_q).toInt() <= 0) {
						_drafts->recoverText(accountId, chatId, text, quotedId,
							[=](ComposeDraft recovered) {
								if (guard.expired() || accountId != _accountId) return;
								const auto peerId = peerFromUser(UserId(userId));
								const auto history = _session->data().historyLoaded(peerId);
								if (!history) return;
								auto draft = Data::Draft(
									TextWithTags{ recovered.text },
									FullReplyTo{ .messageId = recovered.quotedMessageId > 0
										? FullMsgId(peerId, MsgId(recovered.quotedMessageId)) : FullMsgId() },
									SuggestOptions(), MessageCursor(), Data::WebPageDraft());
								history->createCloudDraft({}, {}, &draft);
								history->applyCloudDraft({}, {});
							});
						reportError(u"An earlier message could not be sent. Check the sending profile's draft."_q);
					}
					return;
				}
				if (HasError(result)) {
					done(0, ErrorText(result));
					return;
				}
				const auto id = result.value(u"result"_q).toInt();
				if (!_deletedMessages.contains(id) && !CoreMessages.contains(id)) {
					CoreMessages.insert(id, QJsonObject{{ u"state"_q, 20 }});
				}
				done(id, QString());
				if (!_deletedMessages.contains(id)) {
					addIncoming(chatForPeerUser(userId), id);
				}
			});
	}

	void sendFile(
			int userId,
			const QString &path,
			const QString &caption,
			const QString &viewType,
			int quotedId,
			Fn<void(int, QString)> done,
			const QString &filename = {}) {
		QJsonObject data;
		if (!caption.isEmpty()) {
			data.insert(u"text"_q, caption);
		}
		if (quotedId > 0) {
			data.insert(u"quotedMessageId"_q, quotedId);
		}
		data.insert(u"file"_q, path);
		if (!filename.isEmpty()) data.insert(u"filename"_q, filename);
		if (!viewType.isEmpty()) {
			data.insert(u"viewtype"_q, viewType);
		}
		const auto guard = _alive;
		call(
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
				if (!_deletedMessages.contains(id) && !CoreMessages.contains(id)) {
					CoreMessages.insert(id, QJsonObject{{ u"state"_q, 20 }});
				}
				done(id, QString());
				if (!_deletedMessages.contains(id)) {
					addIncoming(chatForPeerUser(userId), id);
				}
			});
	}

	void sendReaction(int messageId, const QJsonArray &reactions) {
		if (messageId <= 0 || IsClientMsgId(MsgId(messageId))) {
			return;
		}
		const auto guard = _alive;
		call(
			u"send_reaction"_q,
			{ _accountId, messageId, reactions },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				if (HasError(reply)) reportError(ErrorText(reply));
				const auto chatId = CoreMessages.value(messageId).value(u"chatId"_q).toInt();
				if (chatId > 0 && _rpc->running()) addIncoming(chatId, messageId);
			});
	}

	Fn<void()> startSecondDevice(
			Fn<void(QString, QString)> ready,
			Fn<void(QString)> finished) {
		const auto guard = _alive;
		const auto account = _accountScope.guard();
		return _backups->start(_accountId,
			[=](QString svg, QString error) {
				if (!guard.expired() && account() && ready) {
					ready(std::move(svg), std::move(error));
				}
			},
			[=](QString error) {
				if (!guard.expired() && account() && finished) {
					finished(std::move(error));
				}
			});
	}

	void listTransports(Fn<void(QJsonArray, QString)> done) {
		const auto guard = _alive;
		call(
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
		call(
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
		call(
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
		call(
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
		call(
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
		call(
			u"add_transport_from_qr"_q,
			{ _accountId, qr },
			[=](QJsonObject reply) {
				if (!guard.expired() && done) {
					done(HasError(reply) ? ErrorText(reply) : QString());
				}
			});
	}

	void setSelfAvatar(const QString &path, Fn<void(QString)> done) {
		const auto guard = _alive;
		const auto accountId = _accountId;
		const auto epoch = _dialogsEpoch;
		++_selfAvatarRevision;
		call(
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
				call(
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

	void findMessageIds(
			int userId,
			const QString &query,
			Fn<void(QVector<int>, QString)> done) {
		const auto chat = userId
			? QJsonValue(chatForPeerUser(userId))
			: QJsonValue(QJsonValue::Null);
		const auto guard = _alive;
		call(
			u"search_messages"_q,
			{ _accountId, query, chat },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				if (HasError(reply)) {
					done({}, ErrorText(reply));
					return;
				}
				if (!reply.value(u"result"_q).isArray()) {
					done({}, u"Core returned invalid search results."_q);
					return;
				}
				auto ids = QVector<int>();
				for (const auto &value : reply.value(u"result"_q).toArray()) {
					const auto id = value.toInt();
					if (!value.isDouble() || id <= 0 || value.toDouble() != id) {
						done({}, u"Core returned invalid search results."_q);
						return;
					}
					ids.push_back(id);
				}
				done(std::move(ids), {});
			});
	}

	void loadSearchResults(
			const QVector<int> &ids,
			Fn<void(QVector<MTPMessage>, QVector<MTPUser>)> done) {
		// Keep legacy callers bounded too; widgets normally ask for one page.
		struct State {
			int offset = 0;
			QVector<MTPMessage> messages;
			QVector<MTPUser> users;
		};
		const auto state = std::make_shared<State>();
		const auto next = std::make_shared<Fn<void()>>();
		const auto weakNext = std::weak_ptr<Fn<void()>>(next);
		*next = [=] {
			if (state->offset >= ids.size()) {
				done(std::move(state->messages), std::move(state->users));
				return;
			}
			const auto page = ids.mid(state->offset, 50);
			state->offset += page.size();
			const auto continuation = weakNext.lock();
			loadSearchHits(0, std::vector<int>(page.begin(), page.end()), [=](
					QVector<MTPMessage> messages, QVector<MTPUser> users) {
				state->messages += messages;
				state->users += users;
				if (continuation) (*continuation)();
			});
		};
		(*next)();
	}

	void searchMessages(
			int userId,
			const QString &query,
			Fn<void(QVector<MTPMessage>, QVector<MTPUser>)> done) {
		findMessageIds(userId, query, [=](QVector<int> ids, QString error) {
			if (!error.isEmpty()) {
				reportError(error);
				done({}, {});
				return;
			}
			loadSearchResults(ids, std::move(done));
		});
	}

	void loadInviteQr(
			Fn<void(QString, QString, QString, QString)> done) {
		const auto guard = _alive;
		call(
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
				call(
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
				call(
					u"remove_account"_q,
					{ accountId },
					[=](QJsonObject reply) {
						if (guard.expired()) {
							return;
						}
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
			call(
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
		call(u"get_all_accounts"_q, {}, [=](QJsonObject reply) {
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
		Core::App().notifications().resetSession(_session);
		Core::App().closeMediaView();
		Media::Player::instance()->resetForAccountChange();
		// Detach the old account's chat and cancel its loading requests first.
		for (const auto &window : _session->windows()) {
			window->hideLayer(anim::type::instant);
			window->clearSectionStack(Window::SectionShow(
				Window::SectionShow::Way::ClearStack, anim::type::instant));
		}
		++_dialogsEpoch;
		_accountScope.invalidate();
		_session->api().resetMediaForAccountChange();
		_session->recentPeers().resetForAccountChange();
		_session->topPeers().resetForAccountChange();
		_session->topBotApps().resetForAccountChange();
		_session->topGuestChatBots().resetForAccountChange();
		AccountViewRevision = AccountViewRevision.current() + 1;
		ComposerRevision = ComposerRevision.current() + 1;
		_session->data().histories().resetBackendRequests();
		++_selfAvatarRevision;
		_deletedMessages.clear();
		_pendingMuteChats.clear();
		_pendingContactBlocks.clear();
		_knownListedChats.clear();
		++_dialogsRevision;
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
		GroupChats.clear();
		DirectChats.clear();
		SenderContacts.clear();
		CoreMessages.clear();
		CoreReadReceipts.clear();
		PhotoFiles.clear();
		DocumentFiles.clear();
		AvatarFiles.clear();
		AvatarPng.clear();
		PendingUploads.clear();
		ChannelRevision = ChannelRevision.current() + 1;
		auto users = std::vector<not_null<PeerData*>>();
		_session->data().enumerateUsers([&](not_null<UserData*> user) {
			users.push_back(user);
		});
		for (const auto &user : users) {
			if (const auto history = _session->data().historyLoaded(user)) {
				const auto pending = history->clientSideMessages();
				for (const auto item : pending) item->destroy();
				history->clearDrafts();
			}
			_session->data().deleteConversationLocally(user);
		}
		// DeleteChat marks an empty history as fully loaded. Profile changes
		// must leave these reused peers unloaded so opening them fetches Core.
		_session->data().histories().unloadAll();
		// Draft and history cleanup can reinsert dialogs. Detach every remaining
		// row through the session before clearing the list's counters: clear()
		// alone destroys rows but leaves Entry::chatListLinks pointing at them.
		auto entries = std::vector<not_null<Dialogs::Entry*>>();
		for (const auto &row : _session->data().chatsList()->indexed()->all()) {
			entries.push_back(row->entry());
		}
		for (const auto &entry : entries) {
			_session->data().removeChatListEntry(entry);
		}
		_session->data().chatsList()->clear();
		_session->data().chatsList()->setLoaded(false);
	}

	[[nodiscard]] MTPUser selfUser() const {
		return MakeUser(kSelfUserId, _selfName, true, _selfAvatar);
	}

	void switchProfile(int accountId, Fn<void(QString)> done) {
		if (_replies->hasUnsaved(_accountId)) {
			done(u"A notification reply has not finished saving. Open Saved replies before switching profiles."_q);
			return;
		}
		if (hasUnsavedPreparation()) {
			done(u"An attachment has not finished saving. Wait for it to finish, or remove the failed attachment before switching profiles."_q);
			return;
		}
		if (_switching) {
			done(u"A profile switch is already in progress."_q);
			return;
		}
		_switching = true;
		const auto guard = _alive;
		call(u"select_account"_q, { accountId }, [=](QJsonObject selected) {
			if (guard.expired()) {
				return;
			}
			if (HasError(selected)) {
				_switching = false;
				done(ErrorText(selected));
				return;
			}
			resetAccountView();
			_accountId = accountId;
			ProfileActivityRevision = ProfileActivityRevision.current() + 1;
			GlobalSelfAddress.clear();
			_connectivity = 0;
			refreshConnectivity();
			const auto epoch = _dialogsEpoch;
			struct Self {
				QString name;
				QString avatar;
				QColor color;
				QString error;
				int left = 5;
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
				_switching = false;
				noticeSavedReplies();
				done(self->error);
			};
			call(u"start_io"_q, { accountId }, [=](QJsonObject reply) {
				if (HasError(reply)) self->error = ErrorText(reply);
				finish();
			});
			call(u"get_config"_q, { accountId, u"configured_addr"_q }, [=](QJsonObject reply) {
				if (HasError(reply)) self->error = ErrorText(reply);
				else GlobalSelfAddress = reply.value(u"result"_q).toString();
				finish();
			});
			call(
				u"get_account_info"_q,
				{ accountId },
				[=](QJsonObject reply) {
					if (HasError(reply)) self->error = ErrorText(reply);
					else {
						self->color = QColor(reply.value(u"result"_q)
							.toObject().value(u"color"_q).toString());
					}
					finish();
				});
			call(
				u"get_config"_q,
				{ accountId, u"displayname"_q },
				[=](QJsonObject reply) {
					if (HasError(reply)) self->error = ErrorText(reply);
					else {
						self->name = reply.value(u"result"_q).toString();
					}
					finish();
				});
			call(
				u"get_config"_q,
				{ accountId, u"selfavatar"_q },
				[=](QJsonObject reply) {
					if (HasError(reply)) self->error = ErrorText(reply);
					else {
						self->avatar = reply.value(u"result"_q).toString();
					}
					finish();
				});
		});
	}

	void addProfile(const QString &name, Fn<void(QString)> done) {
		const auto guard = _alive;
		call(u"add_account"_q, {}, [=](QJsonObject created) {
			if (guard.expired()) {
				return;
			}
			if (HasError(created)) {
				done(ErrorText(created));
				return;
			}
			const auto id = created.value(u"result"_q).toInt();
			call(
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
					call(
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
		call(
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
		call(
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
		call(
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
				call(
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
		call(
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
				call(
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

	void listBlockedContacts(Fn<void(std::vector<BlockedContact>, QString)> done) {
		ReadCoreBlockedContacts([rpc = _rpc](QString method, QJsonArray params, DeltaRpc::Callback callback) {
			rpc->call(method, params, std::move(callback));
		}, _accountScope, _accountId, [=](std::vector<BlockedContact> contacts, QString error) {
			done(std::move(contacts), error);
		});
	}

	void setContactBlocked(int contact, bool blocked, Fn<void(QString)> done) {
		if (_pendingContactBlocks.contains(contact)) {
			done(u"Wait for the current blocking change to finish."_q);
			return;
		}
		_pendingContactBlocks.insert(contact);
		ChangeCoreContactBlocked([rpc = _rpc](QString method, QJsonArray params, DeltaRpc::Callback callback) {
			rpc->call(method, params, std::move(callback));
		}, _accountScope, _accountId, contact, blocked, [=](int chat, QString error) {
			_pendingContactBlocks.remove(contact);
			if (error.isEmpty() && blocked && chat > 0) {
				const auto peer = peerFromUser(UserId(peerUserForChat(chat)));
				for (const auto &window : _session->windows()) {
					if (window->activeChatCurrent().peer() == _session->data().peer(peer)) {
						window->clearSectionStack(Window::SectionShow(
							Window::SectionShow::Way::ClearStack, anim::type::instant));
					}
				}
			}
			loadDialogs();
			done(error);
		});
	}

	void listContacts(Fn<void(std::vector<ContactRow>, QString)> done, bool addressOnly) {
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		call(
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
		call(u"create_chat_by_contact_id"_q, { _accountId, contact.id },
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
		call(u"make_vcard"_q, { _accountId, QJsonArray{ contactId } },
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
		call(method, params, [=](QJsonObject reply) {
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

	void setChatMute(int userId, int seconds) {
		const auto chat = chatForPeerUser(userId);
		if (_pendingMuteChats.contains(chat)) {
			reportError(u"Wait for the current notification change to finish."_q);
			return;
		}
		_pendingMuteChats.insert(chat);
		SetCoreChatMute([rpc = _rpc](QString method, QJsonArray params, DeltaRpc::Callback done) {
			rpc->call(method, params, std::move(done));
		}, _accountScope, _accountId, chat, seconds, [=](QString error) {
			_pendingMuteChats.remove(chat);
			if (!error.isEmpty()) reportError(error);
			_reload.start();
		});
	}

	void channelRequest(int userId, const QString &method, const QJsonArray &args,
			Fn<void(QJsonValue, QString)> done) {
		auto params = QJsonArray{ _accountId, chatForPeerUser(userId) };
		for (const auto &arg : args) params.append(arg);
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		call(method, params, [=](QJsonObject reply) {
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
			call(u"create_qr_svg"_q, { text }, [=](QJsonObject reply) {
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
		call(
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
					call(
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
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		crl::on_main([=] {
			const auto session = weak.get();
			if (guard.expired() || !session || &session->mtp() != instance.get()) {
				return;
			}
			if (!instance->hasCallback(requestId)) {
				return;
			}
			if (epoch != _dialogsEpoch) {
				return fail(instance, requestId, u"DELTA_ACCOUNT_CHANGED"_q);
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
			QString error;
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
					} else if (state->error.isEmpty()) {
						state->error = ErrorText(one);
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
						if (!state->error.isEmpty()) reportError(state->error);
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
		const auto epoch = _dialogsEpoch;
		call(
			u"get_message_ids"_q,
			{ _accountId, chatId, false, false },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				if (epoch != _dialogsEpoch) {
					return fail(instance, requestId, u"DELTA_ACCOUNT_CHANGED"_q);
				}
				if (HasError(reply)) {
					return fail(instance, requestId, u"DELTA_RPC"_q);
				}
				auto ids = std::vector<int>();
				for (const auto &value : reply.value(u"result"_q).toArray()) {
					ids.push_back(value.toInt());
				}
				const auto page = HistoryPage(std::move(ids), offsetId, addOffset, limit);
				if (!page) return fail(instance, requestId, u"DELTA_MESSAGE_NOT_FOUND"_q);
				ids = *page;
				const auto start = 0;
				const auto end = int(ids.size());
				struct State {
					std::vector<MTPMessage> messages;
					QString error;
					int remaining = 0;
				};
				const auto state = std::make_shared<State>();
				const auto count = end - start;
				state->remaining = count;
				state->messages.resize(count);
				const auto finish = [=] {
					if (!state->error.isEmpty()) {
						reportError(state->error);
						return fail(instance, requestId, u"DELTA_HISTORY_FAILED"_q);
					}
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
							if (epoch != _dialogsEpoch) {
								return fail(instance, requestId, u"DELTA_ACCOUNT_CHANGED"_q);
							}
							if (HasError(one)) {
								if (!_deletedMessages.contains(ids[start + i])) state->error = ErrorText(one);
							} else {
								state->messages[i] = MessageFromJson(
									userId, one.value(u"result"_q).toObject());
							}
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
		call(
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
		const auto accountId = _accountId;
		const auto epoch = _dialogsEpoch;
		call(
			u"get_message_ids"_q,
			{ accountId, chatId, false, false },
			[=](QJsonObject reply) {
				if (guard.expired()) {
					return;
				}
				if (epoch != _dialogsEpoch) {
					return fail(instance, requestId, u"DELTA_ACCOUNT_CHANGED"_q);
				}
				if (HasError(reply)) {
					reportError(ErrorText(reply));
					return fail(instance, requestId, u"DELTA_READ_FAILED"_q);
				}
				auto seen = QJsonArray();
				auto found = (maxId <= 0);
				for (const auto &value : reply.value(u"result"_q).toArray()) {
					seen.append(value);
					if (maxId > 0 && value.toInt() == maxId) {
						found = true;
						break;
					}
				}
				if (!found) return fail(instance, requestId, u"DELTA_MESSAGE_NOT_FOUND"_q);
				call(u"markseen_msgs"_q, { accountId, seen }, [=](QJsonObject marked) {
					if (HasError(marked)) {
						reportError(ErrorText(marked));
						return fail(instance, requestId, u"DELTA_READ_FAILED"_q);
					}
					for (const auto &value : seen) {
						const auto i = CoreMessages.find(value.toInt());
						if (i != CoreMessages.end() && i->value(u"fromId"_q).toInt() != 1) {
							i->insert(u"state"_q, 16);
						}
					}
					call(u"marknoticed_chat"_q, { accountId, chatId }, [=](QJsonObject noticed) {
						if (HasError(noticed)) {
							reportError(ErrorText(noticed));
							return fail(instance, requestId, u"DELTA_READ_FAILED"_q);
						}
						const auto boxed = MTPmessages_AffectedMessages(
							MTP_messages_affectedMessages(MTP_int(0), MTP_int(0)));
						this->reply(instance, requestId, [=](mtpBuffer &to) { boxed.write(to); });
					});
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
				_connectivity = 1000;
				if (!_rpc->running()) return;
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
		_profileActivity->handleEvent(envelope);
		const auto event = envelope.value(u"event"_q).toObject();
		const auto kind = event.value(u"kind"_q).toString();
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
		const auto load = [=] {
			fetchMessage(
				msgId,
				[=](QJsonObject one) {
					if (guard.expired() || HasError(one)) {
						return;
					}
					const auto data = one.value(u"result"_q).toObject();
					if (data.value(u"state"_q).toInt() == 19) return;
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
					if (existing && !hasPhoto) {
						if (built.type() == mtpc_message && !existing->isService()) {
							existing->setText(TextWithEntities{
								qs(built.c_message().vmessage()),
							});
							existing->invalidateChatListEntry();
							_session->data().notifyItemDataChange(existing);
							_session->data().updateDependentMessages(existing);
							_session->data().requestItemViewRefresh(existing);
						}
						// Native file/video presentation owns its real caption. Apply
						// it after the fallback text used by the donor text adapter.
						ApplyMessageData(existing);
						return;
					}
					const auto apply = [=](bool notify) {
						if (_deletedMessages.contains(msgId)) return;
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
							notify ? ::NewMessageType::Unread : ::NewMessageType::Existing);
					};
					if (data.value(u"state"_q).toInt() != 10) {
						apply(true);
						return;
					}
					ReadCoreChatMute([rpc = _rpc](QString method, QJsonArray params, DeltaRpc::Callback done) {
						rpc->call(method, params, std::move(done));
					}, _accountScope, _accountId, chatId, [=](std::optional<bool> muted) {
						if (muted) {
							const auto history = _session->data().history(peerId);
							history->owner().notifySettings().applyBackendMute(history->peer, *muted);
						} else {
							reportError(u"The message arrived, but its notification setting could not be checked."_q);
						}
						apply(muted.has_value());
					});
				});
		};
		if (_session->data().message(peerId, MsgId(msgId))) {
			load();
		} else {
			CheckMessageVisibility(_rpc, _accountScope, _accountId, chatId, msgId,
				[=](bool visible) {
					if (visible && !_deletedMessages.contains(msgId)) load();
				});
		}
	}

	void loadDialogs() {
		const auto guard = _alive;
		const auto epoch = _dialogsEpoch;
		const auto revision = ++_dialogsRevision;
		call(
			u"get_chatlist_entries"_q,
			{ _accountId, QJsonValue::Null, QJsonValue::Null, QJsonValue::Null },
			[=](QJsonObject reply) {
				if (guard.expired()
					|| epoch != _dialogsEpoch
					|| revision != _dialogsRevision
					|| HasError(reply)) {
					return;
				}
				if (!reply.value(u"result"_q).isArray()) return;
				const auto ids = reply.value(u"result"_q).toArray();
				for (const auto &id : ids) {
					if (!id.isDouble() || id.toInt() <= 0 || id.toDouble() != id.toInt()) return;
				}
				call(
					u"get_chatlist_items_by_entries"_q,
					{ _accountId, ids },
					[=](QJsonObject items) {
						if (guard.expired()
							|| epoch != _dialogsEpoch
							|| revision != _dialogsRevision
							|| HasError(items)) {
							return;
						}
						if (!items.value(u"result"_q).isObject()) return;
						applyItems(ids, items.value(u"result"_q).toObject(), revision);
					});
			});
	}

	struct Entry {
		int chatId = 0;
		QString name;
		int lastMessageId = 0;
		int fresh = 0;
		int firstUnread = 0;
		bool pinned = false;
		bool muted = false;
		bool muteKnown = false;
		bool selfTalk = false;
		QString avatar;
		int contactId = 0;
		bool channel = false;
		QJsonObject draft;
		bool previewFailed = false;
	};

	void applyItems(const QJsonArray &ids, const QJsonObject &map, uint64 revision) {
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
			const auto kind = item.value(u"kind"_q).toString();
			if (kind == u"ArchiveLink"_q) continue;
			if (kind != u"ChatListItem"_q || item.value(u"id"_q) != idValue) return;
			auto entry = Entry();
			entry.chatId = item.value(u"id"_q).toInt();
			entry.name = item.value(u"name"_q).toString();
			entry.lastMessageId = item.value(u"lastMessageId"_q).toInt();
			entry.fresh = item.value(u"freshMessageCounter"_q).toInt();
			entry.pinned = item.value(u"isPinned"_q).toBool();
			entry.muted = item.value(u"isMuted"_q).toBool();
			entry.muteKnown = item.value(u"isMuted"_q).isBool();
			entry.selfTalk = item.value(u"isSelfTalk"_q).toBool();
			entry.avatar = item.value(u"avatarPath"_q).toString();
			entry.contactId = item.value(u"dmChatContact"_q).toInt();
			const auto type = item.value(u"chatType"_q).toString();
			if (type == u"Single"_q) DirectChats.insert(entry.chatId);
			else DirectChats.remove(entry.chatId);
			if (type == u"Group"_q) GroupChats.insert(entry.chatId);
			else GroupChats.remove(entry.chatId);
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
		const auto unreadFetches = int(std::count_if(
			state->entries.begin(), state->entries.end(),
			[](const Entry &entry) { return entry.fresh > 0; }));
		state->remaining = int(state->entries.size()) + contactFetches + channelFetches + unreadFetches;
		if (!state->remaining) {
			finishItems(state->entries, state->messages, ids, revision);
			return;
		}
		const auto guard = _alive;
		for (const auto &entry : state->entries) {
			if (entry.fresh > 0) {
				call(u"get_first_unread_message_of_chat"_q, { _accountId, entry.chatId },
					[=, chatId = entry.chatId](QJsonObject one) {
						if (guard.expired() || epoch != _dialogsEpoch || revision != _dialogsRevision) return;
						if (HasError(one)) {
							reportError(ErrorText(one));
						} else {
							for (auto &stored : state->entries) {
								if (stored.chatId == chatId) stored.firstUnread = one.value(u"result"_q).toInt();
							}
						}
						if (--state->remaining == 0) finishItems(state->entries, state->messages, ids, revision);
					});
			}
			if (entry.channel) {
				channelRequest(entry.chatId, u"get_full_chat_by_id"_q, {},
					[=](QJsonValue, QString) {
						if (guard.expired() || epoch != _dialogsEpoch || revision != _dialogsRevision) return;
						if (--state->remaining == 0) finishItems(state->entries, state->messages, ids, revision);
					});
			}
			if (entry.contactId > 1) {
				const auto userId = peerUserForChat(entry.chatId);
				call(
					u"get_contact"_q,
					{ _accountId, entry.contactId },
					[=](QJsonObject one) {
						if (guard.expired() || epoch != _dialogsEpoch || revision != _dialogsRevision) {
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
							finishItems(state->entries, state->messages, ids, revision);
						}
					});
			}
			if (!entry.lastMessageId) {
				if (--state->remaining == 0) {
					finishItems(state->entries, state->messages, ids, revision);
				}
				continue;
			}
			const auto finishPreview = [=, chatId = entry.chatId](QJsonObject one) {
				if (guard.expired() || epoch != _dialogsEpoch || revision != _dialogsRevision) return;
				const auto failed = HasError(one);
				if (failed) reportError(ErrorText(one));
				const auto message = one.value(u"result"_q).toObject();
				const auto id = (!failed && message.value(u"state"_q).toInt() != 19)
					? message.value(u"id"_q).toInt() : 0;
				for (auto &stored : state->entries) {
					if (stored.chatId == chatId) {
						stored.lastMessageId = id;
						stored.previewFailed = failed;
					}
				}
				if (id) {
					state->messages.push_back(MessageFromJson(peerUserForChat(chatId), message));
				}
				if (--state->remaining == 0) finishItems(state->entries, state->messages, ids, revision);
			};
			fetchMessage(entry.lastMessageId,
				[=, chatId = entry.chatId](QJsonObject one) {
					if (guard.expired() || epoch != _dialogsEpoch || revision != _dialogsRevision) {
						return;
					}
					const auto message = one.value(u"result"_q).toObject();
					if (HasError(one) || message.value(u"state"_q).toInt() != 19) {
						finishPreview(one);
						return;
					}
					for (auto &stored : state->entries) {
						if (stored.chatId == chatId) stored.draft = message;
					}
					// Core includes drafts in chat-list previews, but excludes them
					// from history. Never use a draft ID as a pagination anchor.
					call(u"get_message_ids"_q, { _accountId, chatId, false, false },
						[=](QJsonObject history) {
							if (guard.expired() || epoch != _dialogsEpoch || revision != _dialogsRevision) return;
							if (HasError(history)) {
								finishPreview(history);
								return;
							}
							const auto ids = history.value(u"result"_q).toArray();
							if (ids.empty()) finishPreview({});
							else fetchMessage(ids.last().toInt(), finishPreview);
						});
				});
		}
	}

	void finishItems(
			const std::vector<Entry> &entries,
			const std::vector<MTPMessage> &messages,
			const QJsonArray &ids,
			uint64 revision) {
		if (revision != _dialogsRevision) return;
		auto users = QVector<MTPUser>();
		auto dialogs = QVector<MTPDialog>();
		auto list = QVector<MTPMessage>();
		for (const auto &message : messages) {
			list.push_back(message);
		}
		users.push_back(selfUser());
		for (const auto &entry : entries) {
			if (entry.previewFailed) continue;
			const auto userId = peerUserForChat(entry.chatId);
			if (userId != kSelfUserId) {
				users.push_back(MakeUser(
					userId,
					entry.name,
					false,
					entry.avatar));
			}
			dialogs.push_back(MakeDialog(
				userId,
				entry.lastMessageId,
				entry.fresh,
				entry.firstUnread,
				entry.pinned,
				entry.muted));
		}
		auto &owner = _session->data();
		owner.processUsers(MTP_vector<MTPUser>(users));
		showSelfAvatar();
		owner.applyDialogs(nullptr, list, dialogs, int(dialogs.size()));
		for (const auto &entry : entries) {
			if (entry.previewFailed) continue;
			const auto history = owner.history(
				peerFromUser(UserId(peerUserForChat(entry.chatId))));
			if (entry.muteKnown) owner.notifySettings().applyBackendMute(history->peer, entry.muted);
			if (!entry.draft.isEmpty()) {
				const auto quoted = entry.draft.value(u"quote"_q).toObject()
					.value(u"messageId"_q).toInt();
				auto draft = std::make_unique<Data::Draft>(
					TextWithTags{ entry.draft.value(u"text"_q).toString() },
					FullReplyTo{ .messageId = quoted > 0
						? FullMsgId(history->peer->id, MsgId(quoted)) : FullMsgId() },
					SuggestOptions(), MessageCursor(), Data::WebPageDraft());
				draft->date = TimeId(entry.draft.value(u"timestamp"_q).toInteger());
				history->setCloudDraft(std::move(draft));
			} else {
				history->clearCloudDraft({}, {});
			}
			history->updateChatListSortPosition();
			if (!history->folderKnown()) {
				history->clearFolder();
			}
			if (!history->inChatList()) {
				owner.refreshChatListEntry(history);
			}
		}
		auto listed = QSet<int>();
		for (const auto &id : ids) listed.insert(id.toInt());
		for (const auto chat : _knownListedChats) {
			if (listed.contains(chat)) continue;
			if (const auto history = owner.historyLoaded(peerFromUser(UserId(peerUserForChat(chat))))) {
				owner.removeChatListEntry(history);
			}
		}
		_knownListedChats.unite(listed);
		owner.chatsList()->setLoaded();
	}

public:
	[[nodiscard]] std::shared_ptr<DeltaRpc> rpc() const {
		return _rpc;
	}

	const not_null<Main::Session*> _session;
	const std::shared_ptr<DeltaRpc> _rpc;
	std::shared_ptr<DraftStore> _drafts;
	std::unique_ptr<ReplyStore> _replies;
	std::shared_ptr<ComposeQueueStore> _queues;
	std::unique_ptr<ComposeQueueSender> _queueSender;
	std::set<std::pair<int, int>> _queueRuns;
	std::map<std::pair<int, int>, PendingComposeBatch> _batches;
	std::map<std::pair<int, int>, std::weak_ptr<PreparedSend>> _preparations;
	std::unique_ptr<BackupTransfers> _backups;
	std::unique_ptr<ProfileActivity> _profileActivity;
	int _accountId = 0;
	QString _selfName;
	QString _selfAvatar;
	int _selfAvatarRevision = 0;
	int _dialogsEpoch = 0;
	uint64 _dialogsRevision = 0;
	QSet<int> _knownListedChats;
	AccountScope _accountScope;
	QSet<int> _deletedMessages;
	QSet<int> _pendingMuteChats;
	QSet<int> _pendingContactBlocks;
	bool _switching = false;
	const std::shared_ptr<bool> _aliveOwner = std::make_shared<bool>(true);
	const std::weak_ptr<bool> _alive = _aliveOwner;
	QHash<int, QString> _names;
	QHash<int, QString> _avatars;
	int _selfChat = 0;
	QTimer _reload;
	bool _pumping = false;
	rpl::variable<int> _connectivity = 0;

};

// Core events may arrive before the send reply. Keep the authoritative item,
// or remove the pending item if Core has already confirmed its deletion.
bool ApplySendResult(
		not_null<Bridge*> bridge,
		not_null<Main::Session*> session,
		FullMsgId pendingId,
		int id,
		const QString &error) {
	const auto notify = gsl::finally([&] {
		session->data().sendHistoryChangeNotifications();
	});
	const auto local = session->data().message(pendingId);
	if (!error.isEmpty() || id <= 0) {
		if (local) local->sendFailed();
		bridge->reportError(error.isEmpty() ? u"Message was not sent."_q : error);
		return false;
	}
	if (local && local->isSending()) {
		const auto existing = session->data().message(pendingId.peer, MsgId(id));
		if (bridge->_deletedMessages.contains(id) || existing) {
			const auto history = local->history();
			local->destroy();
			history->requestChatListMessage();
		} else {
			local->setRealId(MsgId(id));
		}
	}
	return true;
}

qint64 MessageMediaId(int messageId) {
	return MediaIdentity(GlobalBridge ? GlobalBridge->_accountId : 0, messageId);
}

} // namespace

bool Active() {
	return GlobalBridge != nullptr;
}

void ListBlockedContacts(Fn<void(std::vector<BlockedContact>, QString)> done) {
	if (GlobalBridge) GlobalBridge->listBlockedContacts(std::move(done));
	else done({}, u"No profile selected."_q);
}

void SetContactBlocked(int contactId, bool blocked, Fn<void(QString)> done) {
	if (GlobalBridge) GlobalBridge->setContactBlocked(contactId, blocked, std::move(done));
	else done(u"No profile selected."_q);
}

void SetChatMute(int userId, int durationSeconds) {
	if (GlobalBridge) GlobalBridge->setChatMute(userId, durationSeconds);
}

std::shared_ptr<PreparedSend> BeginPreparedSend(int userId) {
	return GlobalBridge ? GlobalBridge->beginPreparedSend(userId) : nullptr;
}

std::shared_ptr<ComposeBatch> BeginPreparedBatch(int userId,
		std::vector<ComposeDraft> selection, Fn<void(bool)> done, std::vector<ForwardSource> forwards) {
	return GlobalBridge ? GlobalBridge->beginPreparedBatch(userId, std::move(selection), std::move(done), std::move(forwards)) : nullptr;
}

std::optional<std::vector<ForwardSource>> CollectForwardSources(
		const std::vector<not_null<HistoryItem*>> &items) {
	return GlobalBridge ? GlobalBridge->collectForwardSources(items) : std::nullopt;
}

std::shared_ptr<ComposeBatch> PendingPreparedBatch(int userId) {
	return GlobalBridge ? GlobalBridge->pendingPreparedBatch(userId) : nullptr;
}

void CheckNewAttachmentBatch(int userId, Fn<void(QString)> done) {
	if (GlobalBridge) GlobalBridge->checkNewAttachmentBatch(userId, std::move(done));
}

int AccountGeneration() {
	return GlobalBridge ? GlobalBridge->_dialogsEpoch : -1;
}

std::function<bool()> CaptureAccountGuard() {
	return GlobalBridge ? GlobalBridge->_accountScope.guard() : std::function<bool()>();
}

rpl::producer<int> AccountViewResets() {
	return AccountViewRevision.changes();
}

void SendNotificationReply(uint64 userId, QString text) {
	if (GlobalBridge) GlobalBridge->sendNotificationReply(userId, std::move(text));
}

void LoadSavedReplies(Fn<void(std::vector<SavedReply>, QString)> done) {
	if (GlobalBridge) GlobalBridge->loadSavedReplies(std::move(done));
}

void SendSavedReply(SavedReply reviewed, Fn<void(QString)> done) {
	if (GlobalBridge) GlobalBridge->changeSavedReply(std::move(reviewed), false, std::move(done));
}

void DiscardSavedReply(SavedReply reviewed, Fn<void(QString)> done) {
	if (GlobalBridge) GlobalBridge->changeSavedReply(std::move(reviewed), true, std::move(done));
}

rpl::producer<int> ComposerChanges() {
	return ComposerRevision.changes();
}

bool ComposerWritable(int userId) {
	return GlobalBridge && GlobalBridge->composerWritable(userId);
}

bool PreparingSendPreventsQuit() {
	return GlobalBridge && GlobalBridge->preparingSendPreventsQuit();
}

void SendComposerText(int userId, const QString &text, int quote, Fn<void(bool)> done) {
	if (GlobalBridge) GlobalBridge->sendComposerDraft(userId, ComposeDraft{ text, quote }, std::move(done));
}

void SendComposerAttachment(int userId, ComposeDraft draft, Fn<void(bool)> done) {
	if (GlobalBridge) GlobalBridge->sendComposerDraft(userId, std::move(draft), std::move(done));
}

void DiscardDraftAttachment(int userId, const QString &file, Fn<void(bool)> done) {
	if (GlobalBridge) GlobalBridge->discardDraftAttachment(userId, file, std::move(done));
}

QString DraftAttachmentName(int userId) {
	return GlobalBridge ? GlobalBridge->draftAttachmentName(userId) : QString();
}

void SaveDraft(int userId, const QString &text, int quotedMessageId) {
	if (GlobalBridge) GlobalBridge->saveDraft(userId, text, quotedMessageId);
}

void LoadDraft(int userId, Fn<void(ComposeDraft)> done) {
	if (GlobalBridge) GlobalBridge->loadDraft(userId, std::move(done));
}

void LoadComposeQueue(int userId, Fn<void(ComposeQueue, QString)> done) {
	if (GlobalBridge) GlobalBridge->loadComposeQueue(userId, std::move(done));
}

void SendComposeQueue(int userId, ComposeQueue reviewed, Fn<void(bool)> done) {
	if (GlobalBridge) GlobalBridge->sendComposeQueue(userId, std::move(reviewed), std::move(done));
}

void DiscardComposeQueueEntry(int userId, ComposeQueue reviewed, Fn<void(bool)> done) {
	if (GlobalBridge) GlobalBridge->discardComposeQueueEntry(userId, std::move(reviewed), std::move(done));
}

void RequestMessageData(int userId, int messageId, Fn<void()> done) {
	if (GlobalBridge) GlobalBridge->requestMessageData(userId, messageId, std::move(done));
	else if (done) done();
}

bool IsGroup(uint64 userId) {
	return GroupChats.contains(userId);
}

bool IsDirectChat(uint64 userId) {
	return userId == kSelfUserId || DirectChats.contains(userId);
}

std::optional<ContactRow> ContactForPeer(uint64 userId) {
	const auto i = SenderContacts.constFind(userId);
	return i == SenderContacts.cend() ? std::nullopt : std::make_optional(*i);
}

std::optional<bool> MessageUnread(int messageId) {
	const auto i = CoreMessages.constFind(messageId);
	if (i == CoreMessages.cend() || i->value(u"fromId"_q).toInt() <= 1) {
		return std::nullopt;
	}
	const auto state = i->value(u"state"_q).toInt();
	return state == 10 || state == 13;
}

void DeleteMessages(const QVector<int> &ids, bool revoke, Fn<void()> done) {
	const auto bridge = GlobalBridge;
	if (!bridge) return;
	auto messages = QJsonArray();
	for (const auto id : ids) messages.append(id);
	DeleteCoreMessages(bridge->_rpc, bridge->_accountScope,
		bridge->_accountId, messages, revoke, [=](QString error) {
			if (!error.isEmpty()) {
				bridge->reportError(error);
			} else {
				for (const auto id : ids) bridge->_deletedMessages.insert(id);
				bridge->_reload.start();
				if (done) done();
			}
		});
}

void DeleteHistory(
		bool self,
		int userId,
		bool justClear,
		bool revoke,
		Fn<void(HistoryDeletionResult)> done,
		std::optional<HistoryDateRange> dates) {
	const auto bridge = GlobalBridge;
	if (!bridge) return;
	bridge->deleteHistory(self, userId, justClear, revoke, std::move(done), dates);
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
	bridge->call(u"get_message_info"_q, { bridge->_accountId, messageId },
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
		bridge->call(u"delete_contact"_q, { bridge->_accountId, contactId }, [=](QJsonObject reply) {
			if (!valid()) return;
			bridge->_reload.start();
			done(HasError(reply) ? ErrorText(reply) : QString());
		});
	};
	if (!deleteChat) {
		remove();
		return;
	}
	bridge->call(u"get_chat_id_by_contact_id"_q, { bridge->_accountId, contactId }, [=](QJsonObject reply) {
		if (!valid()) return;
		if (HasError(reply)) { done(ErrorText(reply)); return; }
		const auto chatId = reply.value(u"result"_q).toInt();
		if (!chatId) { remove(); return; }
		bridge->call(u"delete_chat"_q, { bridge->_accountId, chatId }, [=](QJsonObject reply) {
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
	const auto type = ChannelInfos.value(userId).value(u"chatType"_q).toString();
	return Active() && (type == u"OutBroadcast"_q || type == u"InBroadcast"_q);
}

bool IsReadOnlyChannel(uint64 userId) {
	return IsChannel(userId) && ChannelInfos.value(userId).value(u"chatType"_q).toString() == u"InBroadcast"_q;
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
		const auto info = ChannelInfos.value(userId);
		if (!info.contains(u"contactIds"_q)) return u"channel"_q;
		const auto ids = info.value(u"contactIds"_q).toArray();
		const auto count = std::count_if(ids.begin(), ids.end(), [](const QJsonValue &id) { return id.toInt() > 9; });
		return count == 1 ? u"1 subscriber"_q : u"%1 subscribers"_q.arg(count);
	}
	const auto known = PeerSubtitles.constFind(userId);
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

bool CanEditMessage(int messageId) {
	return Active() && CanEditCoreMessage(CoreMessages.value(messageId));
}

bool CanDownloadMessage(int messageId) {
	return Active() && CanDownloadMessageState(CoreMessages.value(messageId)
		.value(u"downloadState"_q).toString());
}

void DownloadMessage(int messageId) {
	if (GlobalBridge) GlobalBridge->downloadMessage(messageId);
}

void ApplyMessageData(not_null<HistoryItem*> item) {
	if (!Active()) {
		return;
	}
	const auto data = CoreMessages.value(item->id.bare);
	item->setAnonymousForwarded(data.value(u"isForwarded"_q).toBool());
	const auto fromId = data.value(u"fromId"_q).toInt();
	if (fromId > 1 && !data.value(u"isInfo"_q).toBool()) {
		const auto sender = data.value(u"sender"_q).toObject();
		const auto peerId = kContactUserIdBase + uint64(fromId);
		auto contact = ContactRow();
		contact.id = fromId;
		contact.name = sender.value(u"displayName"_q).toString();
		contact.address = sender.value(u"address"_q).toString();
		contact.avatar = sender.value(u"profileImage"_q).toString();
		if (contact.name.isEmpty()) contact.name = contact.address;
		if (contact.name.isEmpty()) contact.name = u"Unknown sender"_q;
		SenderContacts.insert(peerId, contact);
		const auto user = item->history()->owner().user(UserId(peerId));
		user->setName(contact.name, QString(), QString(), QString());
		item->setBackendSender(user);
	}
	const auto path = data.value(u"file"_q).toString();
	const auto mime = data.value(u"fileMime"_q).toString();
	const auto kind = data.value(u"viewType"_q).toString();
	if (const auto media = item->media()) {
		if (const auto document = media->document();
			document && document->isVoiceMessage()
			&& !path.isEmpty() && QFileInfo::exists(path)) {
			document->setBackendFileLocation(Core::FileLocation(path));
		}
	}
	const auto video = LooksLikeVideo(kind, mime, path);
	const auto file = kind == u"File"_q
		&& !LooksLikeImage(kind, mime, path)
		&& !LooksLikeVoice(kind, mime, path);
	if ((video || file)
		&& !path.isEmpty() && QFileInfo::exists(path)) {
		const auto id = DocumentId(MessageMediaId(int(item->id.bare)));
		const auto document = item->history()->owner().document(id);
		if (document->filepath(true) != path) {
			const auto name = data.value(u"fileName"_q).toString();
			if (video) {
				document->setLocalVideo(name,
					mime.isEmpty() ? u"video/mp4"_q : mime,
					QSize(data.value(u"dimensionsWidth"_q).toInt(),
						data.value(u"dimensionsHeight"_q).toInt()),
					data.value(u"duration"_q).toInt());
			} else {
				document->setLocalFile(name.isEmpty() ? QFileInfo(path).fileName() : name,
					mime.isEmpty() ? u"application/octet-stream"_q : mime);
			}
			document->size = QFileInfo(path).size();
			document->setBackendFileLocation(Core::FileLocation(path));
		}
		item->setLocalDocument(document, data.value(u"text"_q).toString());
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

void RememberPendingFile(uint64 peer, int64 msg, const QString &path,
		const QString &filename, std::shared_ptr<void> owner, bool forceFile,
		std::shared_ptr<PreparedSend> preparation) {
	if (!path.isEmpty()) {
		PendingUploads.insert(PendingKey(peer, msg), {
			path, filename, std::move(owner), forceFile, std::move(preparation),
		});
	}
}

void ForgetPendingFile(uint64 peer, int64 msg) {
	PendingUploads.remove(PendingKey(peer, msg));
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
	const auto bridge = GlobalBridge;
	bridge->sendText(
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
			const auto success = ApplySendResult(bridge, session, fullId, id, error);
			if (done) done(success);
		});
	return true;
}

void SendReaction(int messageId, const QJsonArray &reactions) {
	if (!GlobalBridge) {
		return;
	}
	GlobalBridge->sendReaction(messageId, reactions);
}

bool CanRetryPreparedAttachment(not_null<HistoryItem*> item) {
	const auto bridge = GlobalBridge;
	if (!bridge || bridge->_switching || !bridge->_rpc->running()
		|| &item->history()->session() != bridge->_session.get()
		|| !item->hasFailed() || !IsClientMsgId(item->id)) {
		return false;
	}
	const auto prepared = PendingUploads.value(
		PendingKey(item->history()->peer->id.value, item->id.bare));
	const auto preparation = prepared.preparation;
	return preparation
		&& preparation->account == bridge->_accountId
		&& preparation->epoch == bridge->_dialogsEpoch
		&& !preparation->owned
		&& !preparation->finished
		&& !bridge->_drafts->sending(preparation->account, preparation->chat)
		&& QFileInfo(prepared.path).isFile();
}

void RetryPreparedAttachment(not_null<HistoryItem*> item, Fn<void(bool)> done) {
	if (!CanRetryPreparedAttachment(item)) return;
	const auto history = item->history();
	const auto current = CaptureAccountGuard();
	// Mark pending before dispatch so duplicate actions cannot start a send.
	item->retrySend();
	SendItemMedia(item, [=](bool sent) {
		if (!current || !current()) return;
		if (sent) history->setNotLoadedAtBottom();
		if (done) done(sent);
	});
}

bool SendItemMedia(not_null<HistoryItem*> item, Fn<void(bool)> done) {
	if (!GlobalBridge) {
		return false;
	}
	const auto peer = item->history()->peer;
	const auto userId = peer->isSelf() ? kSelfUserId : int(peer->id.value);
	const auto pendingKey = PendingKey(peer->id.value, item->id.bare);
	const auto prepared = PendingUploads.value(pendingKey);
	auto path = prepared.path;
	if (path.isEmpty()) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				path = document->filepath(true);
			}
		}
	}
	if (path.isEmpty() || !QFileInfo::exists(path)) {
		item->sendFailed();
		GlobalBridge->reportError(u"The prepared attachment is no longer available. Nothing was sent."_q);
		if (done) done(false);
		return true;
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
	const auto bridge = GlobalBridge;
	const auto completed = [=](int id, QString error) {
		const auto session = weak.get();
		if (!session) {
			if (done) {
				done(false);
			}
			return;
		}
		// Capturing the preparation object keeps generated files alive until
		// Core has finished reading them, including across a profile switch.
		// Until Core owns a durable copy, rejection retains the local source.
		const auto owned = prepared.preparation && prepared.preparation->owned;
		if ((id > 0 && error.isEmpty()) || owned) {
			PendingUploads.remove(pendingKey);
		} else if (session->data().message(fullId)) {
			PendingUploads.insert(pendingKey, prepared);
		}
		if (owned) {
			const auto history = session->data().historyLoaded(fullId.peer);
			if (history) {
				history->clearLocalDraft({}, {});
				history->clearCloudDraft({}, {});
				if (id <= 0 && !prepared.preparation->saved.file.isEmpty()) {
					const auto &saved = prepared.preparation->saved;
					auto draft = Data::Draft(TextWithTags{ saved.text },
						FullReplyTo{ .messageId = saved.quotedMessageId > 0
							? FullMsgId(fullId.peer, MsgId(saved.quotedMessageId)) : FullMsgId() },
						SuggestOptions(), MessageCursor(), Data::WebPageDraft());
					history->createCloudDraft({}, {}, &draft);
				}
				history->applyCloudDraft({}, {});
			}
			if (id <= 0) {
				if (const auto local = session->data().message(fullId)) local->destroy();
				if (history) history->requestChatListMessage();
				session->data().sendHistoryChangeNotifications();
				bridge->reportError(u"The send was not confirmed. Check the chat and use the attachment button to review its saved draft."_q);
				if (done) done(false);
				return;
			}
		}
		const auto success = ApplySendResult(bridge, session, fullId, id, error);
		if (done) done(success);
	};
	if (prepared.preparation) {
		bridge->sendPreparedFile(prepared.preparation, ComposeDraft{
			caption, quotedId, path, prepared.filename,
			prepared.forceFile ? u"File"_q : viewType,
		}, completed);
	} else {
		bridge->sendFile(userId, path, caption,
			prepared.forceFile ? u"File"_q : viewType,
			quotedId, completed, prepared.filename);
	}
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

void FindMessageIds(
		int userId,
		const QString &query,
		Fn<void(QVector<int>, QString)> done) {
	if (!GlobalBridge) {
		done({}, u"Delta Chat is not running."_q);
		return;
	}
	GlobalBridge->findMessageIds(userId, query, std::move(done));
}

void LoadSearchResults(
		const QVector<int> &ids,
		Fn<void(QVector<MTPMessage>, QVector<MTPUser>)> done) {
	if (!GlobalBridge) {
		done({}, {});
		return;
	}
	GlobalBridge->loadSearchResults(ids, std::move(done));
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

Fn<void()> StartSecondDevice(
		Fn<void(QString, QString)> ready,
		Fn<void(QString)> finished) {
	if (!GlobalBridge) {
		if (finished) finished(u"Delta Chat is not running."_q);
		return {};
	}
	return GlobalBridge->startSecondDevice(std::move(ready), std::move(finished));
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

rpl::producer<int> ProfileUnreadCountValue(int accountId) {
	return ProfileActivityRevision.value() | rpl::map([=] {
		return GlobalBridge ? GlobalBridge->_profileActivity->unread(accountId) : 0;
	}) | rpl::distinct_until_changed();
}

int TotalProfileUnreadCount() {
	return GlobalBridge ? GlobalBridge->_profileActivity->totalUnread() : 0;
}

int InactiveProfileUnreadCount() {
	return GlobalBridge ? GlobalBridge->_profileActivity->totalUnread()
		- GlobalBridge->_profileActivity->unread(GlobalBridge->_accountId) : 0;
}

rpl::producer<> ProfileActivityChanges() {
	return ProfileActivityRevision.changes() | rpl::to_empty;
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
		// WHY: The donor suggestion file has no Core account identity. Do not
		// import or persist reused peer IDs across Core profiles.
		account->local().useTransientSearchPeers();
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
