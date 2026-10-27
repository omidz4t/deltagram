#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

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
	class DispatchGuard;
	friend void CallOwnedDeltaRpc(std::shared_ptr<DeltaRpc>, const QString &,
		const QJsonArray &, Callback, int);
	void deferOwnedRelease(std::shared_ptr<DeltaRpc> rpc);
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
	int _activeDispatches = 0;
	std::vector<std::shared_ptr<DeltaRpc>> _releaseWhenIdle;
};

// Retain an unparented shared RPC through its terminal callback. Release it on
// an application turn with no active RPC dispatch, including nested loops.
void CallOwnedDeltaRpc(
	std::shared_ptr<DeltaRpc> rpc,
	const QString &method,
	const QJsonArray &params,
	DeltaRpc::Callback done,
	int timeoutMs = -1);
