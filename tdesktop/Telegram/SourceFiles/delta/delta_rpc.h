#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

#include <functional>

class QTimer;

// JSON Lines client for deltachat-rpc-server. Stdout is RPC. Stderr is logs.
class DeltaRpc final : public QObject {
public:
	using Callback = std::function<void(QJsonObject)>;

	explicit DeltaRpc(QObject *parent = nullptr);
	~DeltaRpc();

	void start(const QString &program, const QString &accountsPath);
	void stop();

	[[nodiscard]] bool running() const;

	void call(const QString &method, const QJsonArray &params, Callback done,
		int timeoutMs = -1);

	void setEventHandler(Callback handler);

private:
	void onReadyRead();
	void onFinished(int exitCode, QProcess::ExitStatus status);
	void failPending(const QString &reason);
	void handleLine(const QByteArray &line);
	void failTransport(const QString &reason);

	QProcess _process;
	QByteArray _buffer;
	QByteArray _stderrTail;
	quint64 _nextId = 1;
	struct Pending {
		Callback done;
		QTimer *timer = nullptr;
	};
	QHash<quint64, Pending> _pending;
	Callback _onEvent;
	QTimer *_startup = nullptr;
	bool _stopping = false;
	bool _failed = false;
};
