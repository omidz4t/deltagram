#include "delta_rpc.h"

#include <QJsonDocument>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <utility>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

using namespace Qt::StringLiterals;

DeltaRpc::DeltaRpc(QObject *parent)
: QObject(parent)
, _process(this) {
	connect(&_process, &QProcess::readyReadStandardOutput, this, [this] {
		onReadyRead();
	});
#if defined(__linux__)
	_process.setChildProcessModifier([] {
		prctl(PR_SET_PDEATHSIG, SIGKILL);
	});
#endif
	_startup = new QTimer(this);
	_startup->setSingleShot(true);
	connect(_startup, &QTimer::timeout, this, [this] {
		failTransport(u"rpc server startup timed out"_s);
	});
	connect(&_process, &QProcess::started, this, [this] {
		_startup->stop();
	});
	connect(&_process, &QProcess::readyReadStandardError, this, [this] {
		const auto text = _process.readAllStandardError();
		if (!text.isEmpty()) {
			_stderrTail = (_stderrTail + text).right(4000);
			fprintf(stderr, "[deltachat-rpc-server] %s", text.constData());
		}
	});
	connect(&_process, &QProcess::finished, this, [this](
			int code,
			QProcess::ExitStatus status) {
		onFinished(code, status);
	});
	connect(&_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
		if (_stopping) {
			return;
		}
		const auto err = _process.readAllStandardError();
		auto reason = _process.errorString();
		if (!err.isEmpty()) {
			reason += u"\n"_s + QString::fromUtf8(err);
		}
		failTransport(reason);
	});
}

DeltaRpc::~DeltaRpc() {
	stop();
}

void DeltaRpc::start(const QString &program, const QString &accountsPath) {
	stop();
	_stopping = false;
	_failed = false;
	_buffer.clear();
	_stderrTail.clear();
	const auto loader = qEnvironmentVariable("DELTA_RPC_LDSO");
	const auto libraryPath = qEnvironmentVariable("DELTA_RPC_LIB");
	if (!loader.isEmpty()) {
		_process.setProgram(loader);
		_process.setArguments({
			u"--library-path"_s,
			libraryPath,
			program,
		});
	} else {
		_process.setProgram(program);
		_process.setArguments({});
	}
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.remove(u"LD_LIBRARY_PATH"_s);
	env.insert(u"DC_ACCOUNTS_PATH"_s, accountsPath);
	_process.setProcessEnvironment(env);
	_process.setProcessChannelMode(QProcess::SeparateChannels);
	_startup->start(15000);
	_process.start();
}

void DeltaRpc::stop() {
	_stopping = true;
	_startup->stop();
	failPending(u"rpc stopped"_s);
	if (_process.state() == QProcess::NotRunning) {
		return;
	}
	_process.terminate();
	if (!_process.waitForFinished(2000)) {
		_process.kill();
		_process.waitForFinished(1000);
	}
}

bool DeltaRpc::running() const {
	return !_stopping && !_failed && _process.state() != QProcess::NotRunning;
}

void DeltaRpc::setEventHandler(Callback handler) {
	_onEvent = std::move(handler);
}

void DeltaRpc::call(
		const QString &method,
		const QJsonArray &params,
		Callback done,
		int timeoutMs) {
	if (!running()) {
		QJsonObject error{{u"message"_s, u"rpc server is not running"_s}};
		if (done) {
			done(QJsonObject{{u"error"_s, error}});
		}
		return;
	}
	const auto id = _nextId++;
	const auto longRunning = method == u"get_next_event"_s
		|| method == u"get_next_event_batch"_s
		|| method == u"provide_backup"_s
		|| method == u"get_backup_qr"_s
		|| method == u"get_backup"_s
		|| method == u"import_backup"_s
		|| method == u"export_backup"_s
		|| method == u"configure"_s
		|| method == u"init_transports"_s
		|| method == u"add_transport"_s
		|| method == u"add_transport_from_qr"_s
		|| method == u"secure_join"_s
		|| method == u"download_full_message"_s;
	const auto timeout = timeoutMs >= 0 ? timeoutMs : longRunning ? 0 : 120000;
	auto timer = static_cast<QTimer*>(nullptr);
	if (timeout > 0) {
		timer = new QTimer(this);
		timer->setSingleShot(true);
		connect(timer, &QTimer::timeout, this, [this] {
			failTransport(u"RPC timed out; outcome may be unknown. Restart to reconnect."_s);
		});
		timer->start(timeout);
	}
	_pending.insert(id, Pending{ std::move(done), timer });
	QJsonObject request{
		{u"jsonrpc"_s, u"2.0"_s},
		{u"id"_s, static_cast<qint64>(id)},
		{u"method"_s, method},
		{u"params"_s, params},
	};
	const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
	const auto frame = bytes + '\n';
	if (_process.write(frame) != frame.size()) {
		failTransport(u"Could not write RPC request; outcome may be unknown."_s);
	}
}

void DeltaRpc::onReadyRead() {
	_buffer.append(_process.readAllStandardOutput());
	if (_failed || _stopping) return;
	while (true) {
		const auto cut = _buffer.indexOf('\n');
		if (cut < 0) {
			break;
		}
		const auto line = _buffer.left(cut);
		_buffer.remove(0, cut + 1);
		if (!line.trimmed().isEmpty()) {
			handleLine(line);
			if (_failed) return;
		}
	}
}

void DeltaRpc::handleLine(const QByteArray &line) {
	QJsonParseError parseError;
	const auto doc = QJsonDocument::fromJson(line, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
		failTransport(u"malformed rpc line"_s);
		return;
	}
	const auto obj = doc.object();
	if (!obj.contains(u"id"_s)) {
		if (_onEvent) {
			_onEvent(obj);
		}
		return;
	}
	if (!obj.value(u"id"_s).isDouble()
		|| (obj.contains(u"result"_s) == obj.contains(u"error"_s))
		|| (obj.contains(u"error"_s) && !obj.value(u"error"_s).isObject())) {
		failTransport(u"malformed rpc response"_s);
		return;
	}
	const auto id = static_cast<quint64>(obj.value(u"id"_s).toInteger());
	const auto pending = _pending.take(id);
	if (pending.timer) {
		pending.timer->stop();
		pending.timer->deleteLater();
	}
	if (pending.done) {
		pending.done(obj);
	}
}

void DeltaRpc::onFinished(int exitCode, QProcess::ExitStatus status) {
	auto reason = (status == QProcess::CrashExit)
		? u"rpc server crashed"_s
		: u"rpc server exited (%1)"_s.arg(exitCode);
	const auto stderrText = QString::fromUtf8(_stderrTail).trimmed();
	if (!stderrText.isEmpty()) {
		reason += u": "_s + stderrText.right(300);
	}
	if (_stopping) {
		failPending(reason);
		return;
	}
	failTransport(reason);
}

void DeltaRpc::failTransport(const QString &reason) {
	if (_failed || _stopping) return;
	_failed = true;
	_startup->stop();
	if (_process.state() != QProcess::NotRunning) _process.kill();
	failPending(reason);
	if (_onEvent) _onEvent(QJsonObject{{u"error"_s,
		QJsonObject{{u"message"_s, reason}}}});
}

void DeltaRpc::failPending(const QString &reason) {
	const auto pending = std::exchange(_pending, {});
	QJsonObject error{{u"message"_s, reason}};
	const auto envelope = QJsonObject{{u"error"_s, error}};
	for (const auto &request : pending) {
		if (request.timer) {
			request.timer->stop();
			request.timer->deleteLater();
		}
		if (request.done) {
			request.done(envelope);
		}
	}
}
