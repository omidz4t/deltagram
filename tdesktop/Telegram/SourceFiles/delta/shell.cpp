#include "shell.h"

#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

QString errorText(const QJsonObject &reply) {
	const auto error = reply.value(u"error"_s);
	if (error.isObject()) {
		const auto message = error.toObject().value(u"message"_s).toString();
		if (!message.isEmpty()) {
			return message;
		}
	}
	if (error.isString()) {
		return error.toString();
	}
	return QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact));
}

} // namespace

Shell::Shell(DeltaRpc *rpc, QWidget *parent)
: QWidget(parent)
, _rpc(rpc) {
	setWindowTitle(u"Delta"_s);
	resize(880, 560);

	_status = new QLabel(u"Connecting…"_s);
	_status->setWordWrap(true);
	_email = new QLineEdit();
	_email->setPlaceholderText(u"email address"_s);
	_password = new QLineEdit();
	_password->setEchoMode(QLineEdit::Password);
	_password->setPlaceholderText(u"mail password"_s);
	_configure = new QPushButton(u"Configure account"_s);
	_chats = new QListWidget();
	_transcript = new QPlainTextEdit();
	_transcript->setReadOnly(true);
	_composer = new QLineEdit();
	_composer->setPlaceholderText(u"Message"_s);
	_send = new QPushButton(u"Send"_s);

	auto *accountRow = new QHBoxLayout();
	accountRow->addWidget(_email, 1);
	accountRow->addWidget(_password, 1);
	accountRow->addWidget(_configure);

	auto *composeRow = new QHBoxLayout();
	composeRow->addWidget(_composer, 1);
	composeRow->addWidget(_send);

	auto *right = new QVBoxLayout();
	right->addWidget(_transcript, 1);
	right->addLayout(composeRow);
	auto *rightWrap = new QWidget();
	rightWrap->setLayout(right);

	auto *split = new QSplitter();
	split->addWidget(_chats);
	split->addWidget(rightWrap);
	split->setStretchFactor(1, 1);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(_status);
	layout->addLayout(accountRow);
	layout->addWidget(split, 1);

	connect(_configure, &QPushButton::clicked, this, [this] { configure(); });
	connect(_send, &QPushButton::clicked, this, [this] { sendText(); });
	connect(_composer, &QLineEdit::returnPressed, this, [this] { sendText(); });
	connect(_chats, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
		if (!item) {
			return;
		}
		openChat(item->data(Qt::UserRole).toUInt());
	});

	_rpc->setEventHandler([this](QJsonObject event) { onEvent(event); });
}

void Shell::setStatus(const QString &text) {
	_status->setText(text);
}

void Shell::call(
		const QString &method,
		const QJsonArray &params,
		DeltaRpc::Callback done) {
	_rpc->call(method, params, std::move(done));
}

void Shell::bootstrap() {
	call(u"get_all_account_ids"_s, {}, [this](QJsonObject reply) {
		if (reply.contains(u"error"_s)) {
			setStatus(errorText(reply));
			return;
		}
		const auto ids = reply.value(u"result"_s).toArray();
		if (ids.isEmpty()) {
			call(u"add_account"_s, {}, [this](QJsonObject created) {
				if (created.contains(u"error"_s)) {
					setStatus(errorText(created));
					return;
				}
				_accountId = created.value(u"result"_s).toInt();
				setStatus(u"Account %1 is not configured."_s.arg(_accountId));
				pumpEvents();
			});
			return;
		}
		_accountId = ids.first().toInt();
		call(u"is_configured"_s, {static_cast<int>(_accountId)}, [this](QJsonObject configured) {
			const auto ready = configured.value(u"result"_s).toBool();
			setStatus(ready
				? u"Account %1 is configured."_s.arg(_accountId)
				: u"Account %1 is not configured."_s.arg(_accountId));
			if (ready) {
				call(u"start_io"_s, {static_cast<int>(_accountId)}, [](QJsonObject) {});
				loadChats();
			}
			pumpEvents();
		});
	});
}

void Shell::configure() {
	if (_accountId == 0) {
		setStatus(u"No account yet."_s);
		return;
	}
	const auto email = _email->text().trimmed();
	const auto password = _password->text();
	if (email.isEmpty() || password.isEmpty()) {
		setStatus(u"Email and password are required."_s);
		return;
	}
	setStatus(u"Saving login…"_s);
	call(u"set_config"_s, {static_cast<int>(_accountId), u"addr"_s, email}, [this, password](QJsonObject reply) {
		if (reply.contains(u"error"_s)) {
			setStatus(errorText(reply));
			return;
		}
		call(u"set_config"_s, {static_cast<int>(_accountId), u"mail_pw"_s, password}, [this](QJsonObject reply) {
			if (reply.contains(u"error"_s)) {
				setStatus(errorText(reply));
				return;
			}
			setStatus(u"Contacting the mail server…"_s);
			call(u"configure"_s, {static_cast<int>(_accountId)}, [this](QJsonObject reply) {
				if (reply.contains(u"error"_s)) {
					setStatus(errorText(reply));
					return;
				}
				call(u"start_io"_s, {static_cast<int>(_accountId)}, [this](QJsonObject reply) {
					if (reply.contains(u"error"_s)) {
						setStatus(errorText(reply));
						return;
					}
					setStatus(u"Account %1 is configured."_s.arg(_accountId));
					loadChats();
				});
			});
		});
	});
}

void Shell::loadChats() {
	call(
		u"get_chatlist_entries"_s,
		{static_cast<int>(_accountId), QJsonValue::Null, QJsonValue::Null, QJsonValue::Null},
		[this](QJsonObject reply) {
			if (reply.contains(u"error"_s)) {
				setStatus(errorText(reply));
				return;
			}
			const auto ids = reply.value(u"result"_s).toArray();
			call(u"get_chatlist_items_by_entries"_s, {static_cast<int>(_accountId), ids}, [this](QJsonObject reply) {
				if (reply.contains(u"error"_s)) {
					setStatus(errorText(reply));
					return;
				}
				_chats->clear();
				const auto items = reply.value(u"result"_s).toObject();
				for (auto it = items.begin(); it != items.end(); ++it) {
					const auto item = it.value().toObject();
					if (item.value(u"kind"_s).toString() != u"ChatListItem"_s) {
						continue;
					}
					const auto title = item.value(u"name"_s).toString();
					const auto preview = item.value(u"summaryText2"_s).toString();
					const auto fresh = item.value(u"freshMessageCounter"_s).toInt();
					auto *row = new QListWidgetItem(
						fresh > 0
							? u"%1 (%2)\n%3"_s.arg(title).arg(fresh).arg(preview)
							: u"%1\n%2"_s.arg(title, preview));
					row->setData(Qt::UserRole, item.value(u"id"_s).toInt());
					_chats->addItem(row);
				}
				if (_chats->count() == 0) {
					setStatus(u"No chats yet."_s);
				}
			});
		});
}

void Shell::openChat(uint chatId) {
	_openChatId = chatId;
	_transcript->clear();
	call(
		u"get_message_ids"_s,
		{static_cast<int>(_accountId), static_cast<int>(chatId), false, false},
		[this, chatId](QJsonObject reply) {
			if (chatId != _openChatId) {
				return;
			}
			if (reply.contains(u"error"_s)) {
				setStatus(errorText(reply));
				return;
			}
			const auto ids = reply.value(u"result"_s).toArray();
			for (const auto &idValue : ids) {
				const auto messageId = idValue.toInt();
				call(u"get_message"_s, {static_cast<int>(_accountId), messageId}, [this, chatId](QJsonObject reply) {
					if (chatId != _openChatId || reply.contains(u"error"_s)) {
						return;
					}
					showMessage(reply.value(u"result"_s).toObject());
				});
			}
			markOpenChatSeen();
		});
}

void Shell::showMessage(const QJsonObject &message) {
	const auto outgoing = message.value(u"fromId"_s).toInt() == 1;
	const auto text = message.value(u"text"_s).toString();
	_transcript->appendPlainText(
		(outgoing ? u"You: "_s : u"Them: "_s) + text);
}

void Shell::sendText() {
	const auto text = _composer->text().trimmed();
	if (_accountId == 0 || _openChatId == 0 || text.isEmpty()) {
		return;
	}
	QJsonObject data{{u"text"_s, text}};
	_composer->clear();
	call(
		u"send_msg"_s,
		{static_cast<int>(_accountId), static_cast<int>(_openChatId), data},
		[this](QJsonObject reply) {
			if (reply.contains(u"error"_s)) {
				setStatus(errorText(reply));
				return;
			}
			openChat(_openChatId);
		});
}

void Shell::markOpenChatSeen() {
	if (_accountId == 0 || _openChatId == 0) {
		return;
	}
	call(
		u"get_fresh_msgs"_s,
		{static_cast<int>(_accountId)},
		[this](QJsonObject reply) {
			if (reply.contains(u"error"_s)) {
				return;
			}
			QJsonArray inChat;
			for (const auto &idValue : reply.value(u"result"_s).toArray()) {
				inChat.append(idValue);
			}
			if (!inChat.isEmpty()) {
				call(u"markseen_msgs"_s, {static_cast<int>(_accountId), inChat}, [](QJsonObject) {});
			}
			call(u"marknoticed_chat"_s, {static_cast<int>(_accountId), static_cast<int>(_openChatId)}, [this](QJsonObject) {
				loadChats();
			});
		});
}

void Shell::pumpEvents() {
	if (_pumping) {
		return;
	}
	_pumping = true;
	call(u"get_next_event"_s, {}, [this](QJsonObject reply) {
		_pumping = false;
		if (reply.contains(u"error"_s)) {
			const auto message = errorText(reply);
			if (!message.contains(u"stopped"_s) && !message.contains(u"not running"_s)) {
				setStatus(message);
				QTimer::singleShot(1000, this, [this] { pumpEvents(); });
			}
			return;
		}
		onEvent(reply.value(u"result"_s).toObject());
		pumpEvents();
	});
}

void Shell::onEvent(const QJsonObject &event) {
	if (event.contains(u"error"_s)) {
		setStatus(errorText(event));
		return;
	}
	const auto payload = event.value(u"event"_s).toObject();
	const auto kind = payload.value(u"kind"_s).toString();
	if (kind == u"Error"_s || kind == u"ErrorSelfNotInGroup"_s) {
		setStatus(payload.value(u"msg"_s).toString());
		return;
	}
	if (kind == u"ConfigureProgress"_s) {
		setStatus(u"Configure %1%"_s.arg(payload.value(u"progress"_s).toInt()));
		return;
	}
	if (kind == u"ConnectivityChanged"_s) {
		return;
	}
	if (kind == u"IncomingMsg"_s
		|| kind == u"MsgsChanged"_s
		|| kind == u"ChatlistChanged"_s
		|| kind == u"ChatlistItemChanged"_s
		|| kind == u"MsgDelivered"_s
		|| kind == u"MsgFailed"_s) {
		loadChats();
		if (_openChatId != 0) {
			openChat(_openChatId);
		}
	}
}
