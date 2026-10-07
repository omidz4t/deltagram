#pragma once

#include "delta_rpc.h"

#include <QJsonArray>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

class Shell final : public QWidget {
public:
	Shell(DeltaRpc *rpc, QWidget *parent = nullptr);

	void bootstrap();

private:
	void setStatus(const QString &text);
	void onEvent(const QJsonObject &event);
	void pumpEvents();
	void configure();
	void loadChats();
	void openChat(uint chatId);
	void showMessage(const QJsonObject &message);
	void sendText();
	void markOpenChatSeen();

	void call(
		const QString &method,
		const QJsonArray &params,
		DeltaRpc::Callback done);

	DeltaRpc *_rpc = nullptr;
	uint _accountId = 0;
	uint _openChatId = 0;
	bool _pumping = false;

	QLabel *_status = nullptr;
	QLineEdit *_email = nullptr;
	QLineEdit *_password = nullptr;
	QPushButton *_configure = nullptr;
	QListWidget *_chats = nullptr;
	QPlainTextEdit *_transcript = nullptr;
	QLineEdit *_composer = nullptr;
	QPushButton *_send = nullptr;
};
