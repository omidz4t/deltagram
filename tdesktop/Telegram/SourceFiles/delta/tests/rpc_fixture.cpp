#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QFileInfo>

#include <iostream>
#include <optional>

using namespace Qt::StringLiterals;

void Reply(const QJsonObject &request, QJsonValue value) {
	std::cout << QJsonDocument(QJsonObject{
		{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, request[u"id"_s]},
		{u"result"_s, value},
	}).toJson(QJsonDocument::Compact).constData() << std::endl;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	auto drafts = QMap<QString, QJsonObject>();
	auto held = std::optional<QJsonObject>();
	auto heldResult = std::optional<QJsonValue>();
	auto writes = 0;
	auto sends = 0;
	auto removals = 0;
	auto discarded = false;
	auto draftReads = 0;
	auto sentMessages = QMap<int, QJsonObject>();
	auto sourceMessages = QMap<QString, QJsonObject>();
	auto visibleSources = QSet<QString>();
	auto forwardCalls = QJsonArray();
	auto forwardFault = QString();
	auto historyIds = QJsonArray{10, 80, 12};
	auto historyCalls = QJsonArray();
	auto backupCalls = QJsonArray();
	auto backupProviders = QMap<int, QJsonObject>();
	auto backupReads = QMap<int, QList<QJsonObject>>();
	auto queueConfig = QMap<QString, QJsonValue>();
	auto queueCopies = QJsonArray();
	auto queuePublished = false;
	const auto mode = qEnvironmentVariable("DC_ACCOUNTS_PATH");
	auto queueFault = mode;
	auto handoffFault = mode;
	if (mode == u"history-large"_s) {
		historyIds = {};
		for (auto id = 1; id <= 205; ++id) historyIds.append(id);
	}
	for (std::string line; std::getline(std::cin, line);) {
		const auto request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
		const auto method = request[u"method"_s].toString();
		const auto args = request[u"params"_s].toArray();
		if (method == u"forward_fault"_s) { forwardFault = args[0].toString(); Reply(request, true); continue; }
		if (method == u"forward_calls"_s) { Reply(request, forwardCalls); continue; }
		if (method == u"set_source_message"_s) {
			const auto message = args[1].toObject();
			const auto key = QString::number(args[0].toInt()) + ':' + QString::number(message[u"id"_s].toInt());
			sourceMessages[key] = message;
			if (args[2].toBool()) visibleSources.insert(key);
			else visibleSources.remove(key);
			Reply(request, true);
			continue;
		}
		if (method == u"forward_messages"_s) {
			forwardCalls.append(args);
			if (forwardFault != u"rejected"_s) {
				for (const auto &id : args[1].toArray()) {
					auto message = sourceMessages.value(QString::number(args[0].toInt()) + ':' + QString::number(id.toInt()));
					const auto next = 700 + sentMessages.size();
					message[u"id"_s] = next;
					message[u"chatId"_s] = args[2];
					message[u"state"_s] = 20;
					message[u"isForwarded"_s] = true;
					sentMessages[next] = std::move(message);
				}
			}
			if (forwardFault == u"hold"_s) { held = request; heldResult = QJsonValue(QJsonValue::Null); continue; }
			if (forwardFault == u"confirmation-rejected"_s) queueFault = u"queue-reject-publish"_s;
			if (forwardFault == u"rejected"_s || forwardFault == u"lost-ack"_s) {
				std::cout << QJsonDocument(QJsonObject{
					{u"id"_s, request[u"id"_s]},
					{u"error"_s, QJsonObject{{u"message"_s, u"Forward not confirmed"_s}}},
				}).toJson(QJsonDocument::Compact).constData() << std::endl;
			} else Reply(request, QJsonValue(QJsonValue::Null));
			continue;
		}
		if (method == u"handoff_fault"_s) {
			handoffFault = args[0].toString();
			Reply(request, true);
			continue;
		}
		if (mode.startsWith(u"lookup-"_s)) {
			if (method == u"lookup_calls"_s) { Reply(request, historyCalls); continue; }
			if (method == u"get_message"_s || method == u"get_message_ids"_s) {
				historyCalls.append(method);
				if ((method == u"get_message"_s && mode == u"lookup-read-error"_s)
					|| (method == u"get_message_ids"_s && mode == u"lookup-list-error"_s)) {
					std::cout << QJsonDocument(QJsonObject{
						{u"id"_s, request[u"id"_s]},
						{u"error"_s, QJsonObject{{u"message"_s, u"Lookup failed"_s}}},
					}).toJson(QJsonDocument::Compact).constData() << std::endl;
				} else if (method == u"get_message"_s) {
					Reply(request, QJsonObject{
						{u"id"_s, mode == u"lookup-wrong-id"_s ? 81 : args[1].toInt()},
						{u"chatId"_s, mode == u"lookup-wrong-chat"_s ? 11 : 10},
						{u"state"_s, mode == u"lookup-draft"_s ? 19 : 16},
						{u"text"_s, u"quote from account %1"_s.arg(args[0].toInt())},
					});
				} else if (mode == u"lookup-hold-list"_s) {
					held = request;
				} else {
					Reply(request, mode == u"lookup-hidden"_s ? QJsonArray() : historyIds);
				}
				continue;
			}
		}
		if (mode.startsWith(u"queue-"_s)) {
			const auto reject = [&] {
				std::cout << QJsonDocument(QJsonObject{
					{u"id"_s, request[u"id"_s]},
					{u"error"_s, QJsonObject{{u"message"_s, u"Queue storage fault"_s}}},
				}).toJson(QJsonDocument::Compact).constData() << std::endl;
			};
			if (method == u"queue_fault"_s) {
				queueFault = args[0].toString();
				queuePublished = false;
				Reply(request, true);
				continue;
			}
			if (method == u"get_all_ui_config_keys"_s) {
				auto keys = QJsonArray();
				const auto prefix = QString::number(args[0].toInt()) + ':';
				for (auto i = queueConfig.begin(); i != queueConfig.end(); ++i) {
					if (i.key().startsWith(prefix)) keys.append(i.key().sliced(prefix.size()));
				}
				Reply(request, keys);
				continue;
			}
			if (method == u"queue_config"_s) {
				auto values = QJsonObject();
				for (auto i = queueConfig.begin(); i != queueConfig.end(); ++i) values[i.key()] = i.value();
				Reply(request, values);
				continue;
			}
			if (method == u"get_message_ids"_s) {
				auto ids = QJsonArray();
				const auto prefix = QString::number(args[0].toInt()) + ':';
				for (auto i = sourceMessages.begin(); i != sourceMessages.end(); ++i) {
					if (i.key().startsWith(prefix) && visibleSources.contains(i.key())
						&& i.value()[u"chatId"_s] == args[1]) ids.append(i.value()[u"id"_s]);
				}
				Reply(request, ids);
				continue;
			}
			if (method == u"queue_copies"_s) {
				Reply(request, queueCopies);
				continue;
			}
			if (method == u"get_blob_dir"_s) {
				Reply(request, u"/fixture/blobs/%1"_s.arg(args[0].toInt()));
				continue;
			}
			if (method == u"copy_to_blob_dir"_s) {
				const auto info = QFileInfo(args[1].toString());
				queueCopies.append(QJsonObject{
					{u"path"_s, info.absoluteFilePath()},
					{u"size"_s, info.size()},
					{u"permissions"_s, int(info.permissions())},
					{u"directoryPermissions"_s, int(QFileInfo(info.absolutePath()).permissions())},
				});
				if (queueFault == u"queue-reject-copy"_s) reject();
				else Reply(request, u"/fixture/blobs/%1/%2"_s.arg(args[0].toInt()).arg(QFileInfo(args[1].toString()).fileName()));
				continue;
			}
			if (method == u"get_config"_s || method == u"set_config"_s) {
				const auto key = QString::number(args[0].toInt()) + ':' + args[1].toString();
				const auto pin = args[1].toString().contains(u".blob."_s);
				if (method == u"get_config"_s) {
					if (queueFault == u"queue-hold-inspection"_s && !pin) {
						held = request;
						heldResult = queueConfig.value(key, QJsonValue(QJsonValue::Null));
						queueFault = u"queue-normal"_s;
					} else if (queueFault == u"queue-unknown-publish"_s && queuePublished && !pin) reject();
					else Reply(request, queueConfig.value(key, QJsonValue(QJsonValue::Null)));
				} else {
					++writes;
					const auto value = args[2];
					if (!value.isNull() && ((pin && queueFault == u"queue-reject-pin"_s)
						|| (!pin && queueFault == u"queue-reject-publish"_s))) {
						reject();
					} else {
						if (value.isNull()) queueConfig.remove(key);
						else queueConfig[key] = value;
						if (!pin && !value.isNull()) queuePublished = true;
						if (!pin && queueFault == u"queue-hold-checkpoint"_s) { held = request; continue; }
						if (queueFault == u"queue-lost-ack"_s || queueFault == u"queue-unknown-publish"_s) reject();
						else Reply(request, QJsonValue(QJsonValue::Null));
					}
				}
				continue;
			}
		}
		if (method == u"crash"_s) return 17;
		if (method == u"backup_calls"_s) { Reply(request, backupCalls); continue; }
		if (mode == u"backup"_s) {
			const auto account = args[0].toInt();
			if (method == u"provide_backup"_s || method == u"get_backup_qr"_s
				|| method == u"create_qr_svg"_s || method == u"stop_ongoing_process"_s) {
				backupCalls.append(QJsonObject{{u"method"_s, method}, {u"params"_s, args}});
				if (method == u"provide_backup"_s) {
					backupProviders[account] = request;
				} else if (method == u"get_backup_qr"_s) {
					backupReads[account].append(request);
				} else {
					Reply(request, method == u"create_qr_svg"_s ? QJsonValue(u"<svg/>"_s) : QJsonValue());
					if (method == u"stop_ongoing_process"_s && backupProviders.contains(account)) {
						Reply(backupProviders.take(account), QJsonValue());
					}
				}
				continue;
			}
			if (method == u"release_backup_qr"_s || method == u"error_backup_qr"_s) {
				if (!backupReads[account].isEmpty()) {
					const auto read = backupReads[account].takeFirst();
					if (method == u"release_backup_qr"_s) {
						Reply(read, u"fixture-backup-%1"_s.arg(account));
					} else {
						std::cout << QJsonDocument(QJsonObject{
							{u"id"_s, read[u"id"_s]},
							{u"error"_s, QJsonObject{{u"message"_s, u"Read timed out"_s}}},
						}).toJson(QJsonDocument::Compact).constData() << std::endl;
					}
				}
				Reply(request, true);
				continue;
			}
			if (method == u"finish_backup"_s) {
				if (backupProviders.contains(account)) Reply(backupProviders.take(account), QJsonValue());
				Reply(request, true);
				continue;
			}
		}
		if (method == u"malformed"_s) { std::cout << "{broken\n" << std::flush; continue; }
		if (method == u"incomplete"_s) {
			std::cout << QJsonDocument(QJsonObject{{u"id"_s, request[u"id"_s]}})
				.toJson(QJsonDocument::Compact).constData() << std::endl;
			continue;
		}
		if (method == u"partial"_s) { std::cout << "{unfinished" << std::flush; return 0; }
		if (method == u"hang"_s || method == u"get_next_event"_s) continue;
		if (method == u"hold"_s) { held = request; continue; }
		if (method == u"is_held"_s) { Reply(request, held.has_value()); continue; }
		if (method == u"event"_s) {
			std::cout << "{\"method\":\"fixture_event\",\"params\":{}}" << std::endl;
			Reply(request, true);
			continue;
		}
		if (method == u"release"_s) {
			if (held) {
				const auto params = (*held)[u"params"_s].toArray();
				Reply(*held, heldResult ? *heldResult : (*held)[u"method"_s] == u"get_draft"_s
					? QJsonValue(drafts[QString::number(params[0].toInt()) + ':' + QString::number(params[1].toInt())])
					: QJsonValue(u"released"_s));
				held.reset();
				heldResult.reset();
			}
			Reply(request, true);
			continue;
		}
		if (method == u"writes"_s) { Reply(request, writes); continue; }
		if (method == u"sends"_s) { Reply(request, sends); continue; }
		if (method == u"amend_sent_message"_s) {
			auto &message = sentMessages[args[0].toInt()];
			const auto changes = args[1].toObject();
			for (auto i = changes.begin(); i != changes.end(); ++i) message[i.key()] = i.value();
			Reply(request, true);
			continue;
		}
		if (method == u"removals"_s) { Reply(request, removals); continue; }
		if ((mode.startsWith(u"handoff-"_s) || mode.startsWith(u"queue-"_s)) && method == u"get_message"_s) {
			auto message = sourceMessages.value(QString::number(args[0].toInt()) + ':' + QString::number(args[1].toInt()), sentMessages.value(args[1].toInt()));
			for (const auto &draft : drafts) {
				if (draft[u"id"_s] == args[1]) message = draft;
			}
			if (handoffFault == u"handoff-missing-message"_s) message = {};
			Reply(request, message);
			continue;
		}
		if (method == u"misc_send_draft"_s) {
			++sends;
			if (handoffFault == u"handoff-reject-removal"_s
				|| handoffFault == u"handoff-recode-reject-removal"_s) queueFault = u"queue-reject-publish"_s;
			if (handoffFault == u"handoff-uncertain-removal"_s) {
				queueFault = u"queue-unknown-publish"_s;
				queuePublished = false;
			}
			const auto key = QString::number(args[0].toInt()) + ':' + QString::number(args[1].toInt());
			const auto id = drafts[key][u"id"_s].toInt();
			if (handoffFault != u"handoff-reject-send"_s) {
				auto message = drafts[key];
				message[u"state"_s] = 20;
				if (handoffFault.startsWith(u"handoff-recode-"_s)) {
					message[u"file"_s] = u"/core/recoded.jpg"_s;
					message[u"fileName"_s] = u"image.jpg"_s;
					message[u"viewType"_s] = u"Image"_s;
				}
				sentMessages[id] = message;
				drafts[key] = {};
			}
			if (handoffFault == u"handoff-reject-send"_s || handoffFault == u"handoff-lost-ack"_s) {
				std::cout << QJsonDocument(QJsonObject{
					{u"id"_s, request[u"id"_s]},
					{u"error"_s, QJsonObject{{u"message"_s, u"send failed"_s}}},
				}).toJson(QJsonDocument::Compact).constData() << std::endl;
			} else {
				Reply(request, id);
			}
			continue;
		}
		if (method == u"download_requests"_s) { Reply(request, historyCalls); continue; }
		if (mode.startsWith(u"download-"_s)
			&& (method == u"get_message"_s || method == u"download_full_message"_s)) {
			historyCalls.append(QJsonObject{{u"method"_s, method}, {u"params"_s, args}});
			if ((mode == u"download-reject-read"_s && method == u"get_message"_s)
				|| (mode == u"download-reject-write"_s && method == u"download_full_message"_s)) {
				std::cout << QJsonDocument(QJsonObject{
					{u"id"_s, request[u"id"_s]},
					{u"error"_s, QJsonObject{{u"message"_s, u"download rejected"_s}}},
				}).toJson(QJsonDocument::Compact).constData() << std::endl;
			} else if (method == u"get_message"_s) {
				Reply(request, QJsonObject{{u"downloadState"_s,
					mode == u"download-reject-write"_s ? u"Available"_s : mode.mid(9)}});
			} else {
				Reply(request, QJsonValue());
			}
			continue;
		}
		if (method == u"history_state"_s) {
			Reply(request, QJsonObject{{u"ids"_s, historyIds}, {u"calls"_s, historyCalls}});
			continue;
		}
		if (method == u"get_messages"_s) {
			historyCalls.append(method);
			auto messages = QJsonObject();
			for (const auto &value : args[1].toArray()) {
				const auto id = value.toInt();
				messages.insert(QString::number(id), QJsonObject{
					{u"kind"_s, mode == u"history-bad-message"_s ? u"loadingError"_s : u"message"_s},
					{u"timestamp"_s, mode == u"history-large"_s ? id : id == 10 ? 100 : id == 80 ? 300 : 200},
				});
			}
			Reply(request, messages);
			continue;
		}
		if (method == u"get_message_ids"_s || method == u"delete_messages"_s
			|| method == u"delete_messages_for_all"_s || method == u"delete_chat"_s) {
			historyCalls.append(method);
			const auto reject = (mode == u"history-reject-list"_s && method == u"get_message_ids"_s)
				|| (mode == u"history-reject-delete"_s && method == u"delete_messages_for_all"_s)
				|| (mode == u"history-reject-chat"_s && method == u"delete_chat"_s);
			if (reject) {
				std::cout << QJsonDocument(QJsonObject{
					{u"id"_s, request[u"id"_s]},
					{u"error"_s, QJsonObject{{u"message"_s, u"deletion rejected"_s}}},
				}).toJson(QJsonDocument::Compact).constData() << std::endl;
				continue;
			}
			if (method == u"get_message_ids"_s) {
				Reply(request, historyIds);
				if (mode == u"history-new-message"_s) historyIds.append(999);
				continue;
			}
			if (method == u"delete_chat"_s) {
				historyIds = {};
			} else {
				for (const auto &id : args[1].toArray()) {
					for (auto i = historyIds.size(); i > 0; --i) {
						if (historyIds[i - 1] == id) historyIds.removeAt(i - 1);
					}
				}
				if (mode == u"history-hold-delete"_s) { held = request; continue; }
			}
			Reply(request, QJsonValue());
			continue;
		}
		if (method == u"get_draft"_s || method == u"inspect_draft"_s
			|| method == u"misc_set_draft"_s || method == u"remove_draft"_s) {
			const auto key = QString::number(args[0].toInt()) + ':' + QString::number(args[1].toInt());
			if (!drafts.contains(key)) drafts[key] = {
				{u"text"_s, u"account-%1"_s.arg(args[0].toInt())},
				{u"file"_s, (mode == u"plain"_s || mode.startsWith(u"handoff-"_s) || mode.startsWith(u"queue-"_s)) ? QString() : u"/fixture/attachment.bin"_s},
				{u"fileName"_s, u"attachment.bin"_s},
				{u"viewType"_s, u"File"_s},
			};
			if (method == u"get_draft"_s || method == u"inspect_draft"_s) {
				if (method == u"get_draft"_s && ++draftReads == 2 && mode == u"hold-second-draft"_s) {
					held = request;
					continue;
				}
				auto value = drafts[key];
				if (method == u"get_draft"_s && ((writes > 0
					&& handoffFault == u"handoff-save-unreadable"_s)
					|| (discarded && handoffFault == u"handoff-discard-unreadable"_s))) {
					std::cout << QJsonDocument(QJsonObject{
						{u"id"_s, request[u"id"_s]},
						{u"error"_s, QJsonObject{{u"message"_s, u"Read failed"_s}}},
					}).toJson(QJsonDocument::Compact).constData() << std::endl;
					continue;
				}
				if (method == u"get_draft"_s && mode == u"handoff-wrong-quote"_s
					&& value[u"id"_s].toInt() > 0) {
					value[u"quote"_s] = QJsonObject{{u"messageId"_s, 999}};
				}
				Reply(request, value);
				continue;
			}
			++writes;
			if (method == u"remove_draft"_s) ++removals;
			if (mode == u"reject"_s || handoffFault == u"handoff-reject-save"_s
				|| (method == u"remove_draft"_s && handoffFault == u"handoff-discard-rejected"_s)) {
				std::cout << QJsonDocument(QJsonObject{
					{u"id"_s, request[u"id"_s]},
					{u"error"_s, QJsonObject{{u"message"_s, u"write rejected"_s}}},
				}).toJson(QJsonDocument::Compact).constData() << std::endl;
				continue;
			}
			drafts[key] = method == u"remove_draft"_s ? QJsonObject() : QJsonObject{
				{u"id"_s, 100 + writes}, {u"state"_s, 19}, {u"chatId"_s, args[1]},
				{u"text"_s, args[2]}, {u"file"_s, args[3]}, {u"fileName"_s, args[4]},
				{u"quote"_s, QJsonObject{{u"messageId"_s, args[5]}}}, {u"viewType"_s, args[6]},
			};
			if (method == u"remove_draft"_s) discarded = true;
			if (mode == u"discard-lost-ack"_s || handoffFault == u"handoff-save-lost-ack"_s
				|| handoffFault == u"handoff-save-unreadable"_s
				|| (method == u"remove_draft"_s && handoffFault == u"handoff-discard-lost-ack"_s)) {
				if (handoffFault.startsWith(u"handoff-save-"_s)) drafts[key][u"file"_s] = u"/core/managed.bin"_s;
				std::cout << QJsonDocument(QJsonObject{
					{u"id"_s, request[u"id"_s]},
					{u"error"_s, QJsonObject{{u"message"_s, u"reply lost after commit"_s}}},
				}).toJson(QJsonDocument::Compact).constData() << std::endl;
				continue;
			}
			if (mode == u"hold-first-save"_s && writes == 1) { held = request; continue; }
			if (handoffFault == u"handoff-hold-save"_s) { held = request; continue; }
			Reply(request, true);
			continue;
		}
		Reply(request, args);
	}
}
